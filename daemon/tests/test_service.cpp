// SPDX-License-Identifier: MIT
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>
#include <fcntl.h>
#include <functional>
#include <linux/input.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dbusservice.h"
#include "fakeremote.h"
#include "hidpp.h"
#include "keysink.h"
#include "service.h"

using namespace projecteur;
using namespace projecteur::hidpp;
using State = PresentationTimer::State;

namespace {

QByteArray hex(const char* s) { return QByteArray::fromHex(s); }
QJsonObject parse(const QString& json) { return QJsonDocument::fromJson(json.toUtf8()).object(); }

struct NullOverlay : OverlaySink {
  void apply(const Commands&) override {}
};

struct Stream {   ///< a stream socket pair: `ours` goes to the code under test, `theirs` plays the kernel
  Stream() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) qFatal("socketpair failed");
    ours = fds[0];
    theirs = fds[1];
    ::fcntl(theirs, F_SETFL, ::fcntl(theirs, F_GETFL) | O_NONBLOCK);
  }
  void key(int code, int value) {
    for (const input_event& ev : {input_event{{}, EV_KEY, static_cast<__u16>(code), value}, input_event{{}, EV_SYN, SYN_REPORT, 0}}) {
      const ssize_t n = ::write(theirs, &ev, sizeof ev);
      QVERIFY(n == static_cast<ssize_t>(sizeof ev));
    }
  }
  int ours = -1, theirs = -1;
};

struct Rig {
  explicit Rig(bool bluetooth = false, std::function<void(FakeRemote*)> prepare = {}) {
    int h[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, h) != 0) qFatal("socketpair failed");
    fake = new FakeRemote(h[1]);
    if (prepare) prepare(fake);
    keys = new FdKeySink(keyOut.ours);
    pointer = new FdPointerSink(pointerOut.ours);
    Remote::Options opt;
    opt.grab = false;
    opt.bluetooth = bluetooth;
    opt.pulseGapMs = 10;
    opt.patternGapMs = 20;
    opt.device.retryMs = 20;
    opt.device.requestTimeoutMs = 60;
    remote = new Remote({h[0], keyboard.ours, mouse.ours}, keys, pointer, &overlay, opt);
  }
  ~Rig() { delete remote; delete fake; delete keys; delete pointer; }
  bool ready() { return QTest::qWaitFor([this] { return remote->device()->isReady(); }, 3000); }
  int shortPulses() const { return fake->requests.count(hidpp::vibrate(0x09, 1, 0x80)); }
  int longPulses() const { return fake->requests.count(hidpp::vibrate(0x09, 2, 0x80)); }
  /// The lengths of all vibration requests so far, in order.
  QList<int> pulseLengths() const {
    QList<int> out;
    for (const QByteArray& m : fake->requests)
      if (m.size() == 20 && static_cast<uint8_t>(m[2]) == 0x09 && static_cast<uint8_t>(m[3]) == 0x1d && static_cast<uint8_t>(m[4]) != 3) out.append(static_cast<uint8_t>(m[4]));
    return out;
  }

  Stream keyboard, mouse, keyOut, pointerOut;
  NullOverlay overlay;
  FakeRemote* fake;
  FdKeySink* keys;
  FdPointerSink* pointer;
  Remote* remote;
};

class StatusCatcher : public QObject {   ///< receives the StatusChanged signal from the bus
  Q_OBJECT
 public slots:
  void got(const QString& json) { received.append(json); }
 public:
  QStringList received;
};

struct Clock {
  qint64 ms = 0;
  PresentationTimer::Clock fn() { return [this] { return ms; }; }
};

/// A method call to the daemon through `client`. BlockWithGui keeps the event loop running while waiting: the daemon
/// side of this test lives in the very same process. (QDBusInterface would introspect first, and wait 25 s for that.)
struct Daemon {
  QDBusConnection client;
  QVariant call(const QString& method, const QVariantList& args = {}) {
    QDBusMessage msg = QDBusMessage::createMethodCall("org.projecteur.Daemon", "/org/projecteur/Daemon", "org.projecteur.Daemon1", method);
    msg.setArguments(args);
    const QDBusMessage reply = client.call(msg, QDBus::BlockWithGui, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) return QVariant(QStringLiteral("ERROR: ") + reply.errorMessage());
    return reply.arguments().isEmpty() ? QVariant() : reply.arguments().first();
  }
};

}  // namespace

class ServiceTest : public QObject {
  Q_OBJECT

 private slots:
  // ---- configuration --------------------------------------------------------------------------------
  void settingsArriveAsJsonAndChangeOnlyWhatIsGiven() {
    Service s;
    QCOMPARE(s.applyConfigJson(R"({"pointer-speed": 60, "hold-next-action": "volume"})"), QString());
    QCOMPARE(s.config().pointerSpeed, 60);
    QCOMPARE(s.config().holdNext, HoldAction::Volume);
    QCOMPARE(s.config().holdBack, HoldAction::BlankScreen);   // untouched
    QCOMPARE(parse(s.configJson())["pointer-speed"].toInt(), 60);
  }

  void badInputIsReportedNotApplied() {
    Service s;
    QVERIFY(s.applyConfigJson("not json").contains("not a JSON object"));
    QVERIFY(s.applyConfigJson("[1,2]").contains("not a JSON object"));
    const QString problems = s.applyConfigJson(R"({"pointer-speed": 500, "freeze-effects": false})");
    QVERIFY(problems.contains("pointer-speed"));
    QCOMPARE(s.config().pointerSpeed, 35);       // the bad value was ignored ...
    QVERIFY(!s.config().freeze);                 // ... the good one was applied
  }

  void theTimerFollowsTheConfiguration() {
    Service s;
    s.applyConfigJson(R"({"timer-minutes": 45, "timer-alerts": [10, 2]})");
    QCOMPARE(s.timer()->totalSeconds(), 45 * 60);
  }

  // ---- status ---------------------------------------------------------------------------------------
  void statusWithoutARemote() {
    Service s;
    const QJsonObject o = parse(s.statusJson());
    QVERIFY(!o["connected"].toBool());
    QCOMPARE(o["connection"].toString(), QString());
    QVERIFY(!o.contains("battery"));
    QCOMPARE(o["timer"].toObject()["state"].toString(), QStringLiteral("idle"));
    QCOMPARE(o["timer"].toObject()["total"].toInt(), 30 * 60);
  }

  void statusDescribesTheConnectedRemote() {
    Service s;
    Rig r(true);
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    const QJsonObject o = parse(s.statusJson());
    QVERIFY(o["connected"].toBool());
    QCOMPARE(o["connection"].toString(), QStringLiteral("bluetooth"));
    QCOMPARE(o["battery"].toObject()["percent"].toInt(), 100);
    QCOMPARE(o["battery"].toObject()["state"].toString(), QStringLiteral("discharging"));
    QVERIFY(!o["battery"].toObject()["charging"].toBool());
    QCOMPARE(o["firmware"].toString(), QStringLiteral("1.1.32"));
    QCOMPARE(o["bootloader"].toString(), QStringLiteral("26.1.15"));
  }

  void statusChangesAreAnnouncedOnceEach() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    QSignalSpy changed(&s, &Service::statusChanged);
    r.fake->notify(hex("11010600" "4b4b00" "0000000000000000000000"));   // 75 %
    QTRY_VERIFY(changed.count() >= 1);
    QCOMPARE(parse(changed.last().at(0).toString())["battery"].toObject()["percent"].toInt(), 75);
    const int n = changed.count();
    r.fake->notify(hex("11010600" "4b4b00" "0000000000000000000000"));   // the same: nothing new
    QTest::qWait(60);
    QCOMPARE(changed.count(), n);
  }

  void aGoneRemoteIsForgottenWithoutACrash() {
    Service s;
    {
      Rig r;
      s.setRemote(r.remote);
      QVERIFY(r.ready());
    }   // the Remote is destroyed here
    QVERIFY(!s.remote());
    QVERIFY(!parse(s.statusJson())["connected"].toBool());
    s.setConfig(Config{});
  }

  void connectedRemoteReceivesTheCurrentSettingsAtOnce() {
    Service s;
    s.applyConfigJson(R"({"vibration-intensity": 100})");
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0xff)), 1);   // the connection pulse already uses it
  }

  // ---- timer and vibration -------------------------------------------------------------------------
  void timerAlertsVibrateWithTheirOwnPulseCounts() {
    Clock c;
    Service s(c.fn());
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-enabled": true, "timer-minutes": 30, "timer-alerts": [5]})");
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0x80)), 1);   // wait for the connection pulse
    s.timerStart();
    c.ms = 25 * 60 * 1000; s.timer()->tick();
    QTRY_COMPARE(r.shortPulses(), Haptics::kTimerAlertPulses);
    c.ms = 30 * 60 * 1000; s.timer()->tick();
    QTRY_COMPARE(r.shortPulses(), Haptics::kTimerAlertPulses + Haptics::kTimerEndPulses);
  }

  void noTimerVibrationWhenTheNotificationIsOff() {
    Clock c;
    Service s(c.fn());
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-notification": false, "timer-minutes": 10, "timer-alerts": [5]})");
    s.timerStart();
    c.ms = 10 * 60 * 1000; s.timer()->tick();
    QTest::qWait(100);
    QCOMPARE(r.shortPulses(), 0);
  }

  void theFirstSlideChangeStartsTheTimerWhenAutoStartIsOn() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-enabled": true})");
    QCOMPARE(s.timer()->state(), State::Idle);
    r.keyboard.key(KEY_RIGHT, 1);
    QTRY_COMPARE(s.timer()->state(), State::Running);
  }

  void startingThePresentationAlsoStartsTheTimer() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-enabled": true})");
    r.fake->notify(hex("11010700" "00da" "000000000000"));   // hold Next = start presentation
    QTRY_COMPARE(s.timer()->state(), State::Running);
  }

  void theTimerStaysIdleWhenDisabledOrNotAutomatic() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    r.keyboard.key(KEY_RIGHT, 1);                              // disabled (the default)
    QTest::qWait(60);
    QCOMPARE(s.timer()->state(), State::Idle);
    s.applyConfigJson(R"({"timer-enabled": true, "timer-auto-start": false})");
    r.keyboard.key(KEY_RIGHT, 1);
    QTest::qWait(60);
    QCOMPARE(s.timer()->state(), State::Idle);
  }

  void pauseResumeAndReset() {
    Clock c;
    Service s(c.fn());
    s.timerStart();
    QCOMPARE(s.timer()->state(), State::Running);
    s.timerTogglePause();
    QCOMPARE(s.timer()->state(), State::Paused);
    s.timerTogglePause();
    QCOMPARE(s.timer()->state(), State::Running);
    s.timerReset();
    QCOMPARE(s.timer()->state(), State::Idle);
  }

  void theMinuteCodeIsPlayedAtEveryMinuteWhenSwitchedOn() {
    Clock c;
    Service s(c.fn());
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0x80)), 1);   // the connection pulse is over
    s.applyConfigJson(R"({"timer-enabled": true, "timer-minutes": 13, "timer-alerts": [0, 0, 0], "timer-minute-pulses": true})");
    s.timerStart();
    const QList<QList<int>> expected{{1}, {1, 1}, {1, 1, 1}, {1, 1, 1, 1}, {2}, {2, 1}, {2, 1, 1}, {2, 1, 1, 1}, {2, 1, 1, 1, 1}, {2, 2}, {1}, {1, 1}};
    QList<int> all;
    for (int minute = 1; minute <= 12; ++minute) {
      c.ms = minute * 60 * 1000;
      s.timer()->tick();
      all += expected[minute - 1];
      QTRY_COMPARE_WITH_TIMEOUT(r.pulseLengths(), all, 3000);
    }
  }

  void noMinuteCodeByDefaultAndNoneAtTheEnd() {
    Clock c;
    Service s(c.fn());
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-enabled": true, "timer-minutes": 3, "timer-alerts": [0, 0, 0]})");
    s.timerStart();
    c.ms = 60 * 1000; s.timer()->tick();
    QTest::qWait(100);
    QCOMPARE(r.shortPulses(), 0);                 // off by default
    s.applyConfigJson(R"({"timer-minute-pulses": true})");
    c.ms = 2 * 60 * 1000; s.timer()->tick();
    QTRY_COMPARE(r.shortPulses(), 2);             // minute 2
    c.ms = 3 * 60 * 1000; s.timer()->tick();      // the end: three short pulses ("time is up"), not the code for minute 3
    QTRY_COMPARE(r.shortPulses(), 2 + Haptics::kTimerEndPulses);
  }

  void anAlertAndAMinuteTogetherPlayOneAfterTheOther() {
    Clock c;
    Service s(c.fn());
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.applyConfigJson(R"({"timer-enabled": true, "timer-minutes": 10, "timer-alerts": [5, 0, 0], "timer-minute-pulses": true})");
    s.timerStart();
    c.ms = 5 * 60 * 1000; s.timer()->tick();      // minute 5 (one long pulse) and "5 minutes remaining" (two short ones)
    QTRY_COMPARE(r.pulseLengths(), (QList<int>{2, 1, 1}));
  }

  void theMinuteCodeCanBeTriedOverDBusMethodsOfTheService() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.vibrateMinute(6);
    QTRY_COMPARE(r.pulseLengths(), (QList<int>{2, 1}));
    s.vibrateMinute(0);        // clamped to 1
    QTRY_COMPARE(r.pulseLengths(), (QList<int>{2, 1, 1}));
  }

  void theTestVibrationButtonBuzzesTheRequestedNumberOfPulses() {
    Service s;
    Rig r;
    s.setRemote(r.remote);
    QVERIFY(r.ready());
    s.vibrate(3);
    QTRY_COMPARE(r.shortPulses(), 3);
    s.vibrate(100);   // capped
    QTRY_COMPARE(r.shortPulses(), 3 + 6);
  }

  // ---- the D-Bus interface, on a private bus -------------------------------------------------------
  void theDBusInterface() {
    QProcess bus;
    bus.start("dbus-daemon", {"--session", "--print-address=1", "--nofork"});
    QVERIFY2(bus.waitForStarted(5000), "dbus-daemon is needed for this test");
    QVERIFY(bus.waitForReadyRead(5000));
    const QString address = QString::fromUtf8(bus.readLine()).trimmed();
    QVERIFY(!address.isEmpty());

    {
      QDBusConnection server = QDBusConnection::connectToBus(address, "server");
      QDBusConnection client = QDBusConnection::connectToBus(address, "client");
      QVERIFY(server.isConnected() && client.isConnected());

      Clock c;
      Service s(c.fn());
      QString error;
      QVERIFY2(exportService(server, &s, &error), qPrintable(error));

      Daemon daemon{client};
      QCOMPARE(parse(daemon.call("GetConfig").toString())["pointer-speed"].toInt(), 35);
      QCOMPARE(daemon.call("SetConfig", {R"({"pointer-speed": 80, "timer-minutes": 20})"}).toString(), QString());
      QCOMPARE(s.config().pointerSpeed, 80);
      QCOMPARE(parse(daemon.call("GetConfig").toString())["timer-minutes"].toInt(), 20);
      QVERIFY(daemon.call("SetConfig", {R"({"pointer-speed": -3})"}).toString().contains("pointer-speed"));

      StatusCatcher catcher;
      QVERIFY(client.connect("org.projecteur.Daemon", "/org/projecteur/Daemon", "org.projecteur.Daemon1", "StatusChanged", &catcher,
                             SLOT(got(QString))));
      daemon.call("TimerStart");
      QTRY_VERIFY(!catcher.received.isEmpty());
      QCOMPARE(parse(catcher.received.last())["timer"].toObject()["state"].toString(), QStringLiteral("running"));
      QCOMPARE(parse(daemon.call("GetStatus").toString())["timer"].toObject()["state"].toString(), QStringLiteral("running"));
      QCOMPARE(parse(daemon.call("GetStatus").toString())["timer"].toObject()["total"].toInt(), 20 * 60);
      daemon.call("TimerPause");
      QCOMPARE(parse(daemon.call("GetStatus").toString())["timer"].toObject()["state"].toString(), QStringLiteral("paused"));
      daemon.call("TimerReset");
      QCOMPARE(parse(daemon.call("GetStatus").toString())["timer"].toObject()["state"].toString(), QStringLiteral("idle"));
      QVERIFY(!daemon.call("Vibrate", {3u}).toString().startsWith("ERROR"));   // no remote: accepted, does nothing
      QVERIFY(!daemon.call("VibrateMinute", {7u}).toString().startsWith("ERROR"));

      // a second daemon must not take over the name
      Service second;
      QDBusConnection other = QDBusConnection::connectToBus(address, "other");
      QString error2;
      QVERIFY(!exportService(other, &second, &error2));
      QVERIFY(error2.contains("org.projecteur.Daemon"));
      QDBusConnection::disconnectFromBus("other");
    }
    QDBusConnection::disconnectFromBus("server");
    QDBusConnection::disconnectFromBus("client");
    bus.terminate();
    bus.waitForFinished(3000);
  }
};

QTEST_MAIN(ServiceTest)
#include "test_service.moc"
