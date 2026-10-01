// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fakeremote.h"
#include "hidpp.h"
#include "keysink.h"
#include "remote.h"
#include <functional>

using namespace projecteur;
using namespace projecteur::hidpp;
using T = Command::Type;

namespace {

QByteArray hex(const char* s) { return QByteArray::fromHex(s); }

class RecordingOverlay : public OverlaySink {
 public:
  void apply(const Commands& c) override { all.insert(all.end(), c.begin(), c.end()); }
  Commands all;
};

/// Two connected stream sockets: `ours` is handed to the code under test, `theirs` plays the kernel.
struct Pipe {
  Pipe() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) qFatal("socketpair failed");
    ours = fds[0];
    theirs = fds[1];
    ::fcntl(theirs, F_SETFL, ::fcntl(theirs, F_GETFL) | O_NONBLOCK);
  }
  void event(int type, int code, int value) {  // write one struct input_event
    input_event ev{};
    ev.type = static_cast<__u16>(type);
    ev.code = static_cast<__u16>(code);
    ev.value = value;
    const ssize_t n = ::write(theirs, &ev, sizeof ev);
    QVERIFY(n == static_cast<ssize_t>(sizeof ev));
  }
  void key(int code, int value) { event(EV_KEY, code, value); event(EV_SYN, SYN_REPORT, 0); }
  /// Wheel notches written into the pipe by an FdPointerSink on the other end.
  QList<int> wheel() {
    QList<int> out;
    input_event ev;
    while (::read(theirs, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev))
      if (ev.type == EV_REL && ev.code == REL_WHEEL) out.append(ev.value);
    return out;
  }
  /// Key presses written into the pipe by an FdKeySink on the other end.
  QList<int> pressedKeys() {
    QList<int> out;
    input_event ev;
    while (::read(theirs, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev))
      if (ev.type == EV_KEY && ev.value == 1) out.append(ev.code);
    return out;
  }
  int ours = -1, theirs = -1;
};

Command cmd(T t) { return Command{t}; }
Command show(Mode m) { Command c{T::ShowAtPointer}; c.mode = m; return c; }
Command move(double dx, double dy) { Command c{T::MoveBy}; c.dx = dx; c.dy = dy; return c; }
Command setMode(Mode m) { Command c{T::SetMode}; c.mode = m; return c; }

struct Rig {
  explicit Rig(Config config = {}, std::function<void(FakeRemote*)> prepare = {}) {
    int h[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, h) != 0) qFatal("socketpair failed");
    fake = new FakeRemote(h[1]);
    if (prepare) prepare(fake);
    keyOut = new FdKeySink(keyPipe.ours);
    pointerOut = new FdPointerSink(pointerPipe.ours);
    Remote::Options opt;
    opt.grab = false;  // sockets, not input nodes
    opt.device.retryMs = 20;
    opt.device.requestTimeoutMs = 60;
    opt.pulseGapMs = 10;
    opt.patternGapMs = 20;
    opt.config = config;
    remote = new Remote({h[0], keyboard.ours, mouse.ours}, keyOut, pointerOut, &overlay, opt);
  }
  ~Rig() { delete remote; delete fake; delete keyOut; delete pointerOut; }
  bool ready() { return QTest::qWaitFor([this] { return remote->device()->isReady(); }, 3000); }

  Pipe keyboard, mouse, keyPipe, pointerPipe;
  RecordingOverlay overlay;
  FakeRemote* fake;
  FdKeySink* keyOut;
  FdPointerSink* pointerOut;
  Remote* remote;
};

}  // namespace

class RemoteTest : public QObject {
  Q_OBJECT

 private slots:
  void theMeasuredSessionEndToEnd() {
    Rig r;
    QVERIFY(r.ready());

    r.fake->notify(hex("11010700" "00d8" "000000000000"));                      // action button held
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));
    QCOMPARE(r.overlay.all.back(), show(Mode::Highlight));

    r.fake->notify(hex("11010710" "0003" "fffb" "00000000000000000000"));        // dx=3 dy=-5
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), move(3, -5));

    r.fake->notify(hex("110107000000000000000000"));                            // released: frozen, nothing happens
    QTest::qWait(60);
    QCOMPARE(r.overlay.all.size(), size_t(2));

    r.mouse.key(BTN_LEFT, 1);                                                   // short press = plain mouse click
    r.mouse.key(BTN_LEFT, 0);
    QTRY_COMPARE(r.overlay.all.size(), size_t(3));
    QCOMPARE(r.overlay.all.back(), cmd(T::Hide));

    r.fake->notify(hex("11010700" "00df" "000000000000"));                      // double click
    QTRY_COMPARE(r.overlay.all.size(), size_t(4));
    QCOMPARE(r.overlay.all.back(), setMode(Mode::Magnify));

    r.keyboard.key(KEY_RIGHT, 1);                                               // Next
    r.keyboard.key(KEY_RIGHT, 0);
    QList<int> forwarded;                                                       // (QTRY_* re-evaluates its expression: accumulate)
    QTRY_VERIFY((forwarded += r.keyPipe.pressedKeys(), !forwarded.isEmpty()));  // re-sent through the virtual keyboard
    QCOMPARE(forwarded, (QList<int>{KEY_RIGHT}));
  }

  void nextAndBackAreForwardedAsTheSameKeys() {
    Rig r;
    QVERIFY(r.ready());
    r.keyboard.key(KEY_RIGHT, 1);
    r.keyboard.key(KEY_LEFT, 1);
    QList<int> got;
    QTRY_VERIFY((got += r.keyPipe.pressedKeys(), got.size() >= 2));
    QCOMPARE(got, (QList<int>{KEY_RIGHT, KEY_LEFT}));
  }

  void otherKeysAreForwardedUnchanged() {
    Rig r;
    QVERIFY(r.ready());
    r.keyboard.key(KEY_PAGEDOWN, 1);
    QList<int> got;
    QTRY_VERIFY((got += r.keyPipe.pressedKeys(), !got.isEmpty()));
    QCOMPARE(got, (QList<int>{KEY_PAGEDOWN}));
  }

  void pointerMotionOnTheMouseNodeIsSwallowed() {
    Rig r;
    QVERIFY(r.ready());
    r.mouse.event(EV_REL, REL_X, 7);
    r.mouse.event(EV_REL, REL_Y, -3);
    r.mouse.event(EV_SYN, SYN_REPORT, 0);
    QTest::qWait(80);
    QVERIFY(r.overlay.all.empty());
    QVERIFY(r.keyPipe.pressedKeys().isEmpty());
  }

  void aVisibleEffectIsRecenteredOnSlideChange() {
    Rig r;
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00d8" "000000000000"));
    r.fake->notify(hex("110107000000000000000000"));  // frozen and visible
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));
    r.keyboard.key(KEY_RIGHT, 1);
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), cmd(T::Recenter));
  }

  void theConnectionPulseIsLongAndHappensOnlyOnce() {  // policy: short pulses, one longer one for "connected"
    Rig r;
    QVERIFY(r.ready());
    const QByteArray pulse = hidpp::vibrate(0x09, 3, 0x80);
    QTRY_COMPARE(r.fake->requests.count(pulse), 1);
    r.fake->notify(hex("10014100" "000000"));  // the remote wakes up: diversions are applied again
    QTest::qWait(150);
    QVERIFY(r.ready());
    QCOMPARE(r.fake->requests.count(pulse), 1);  // no second buzz
  }

  void holdingNextStartsThePresentation() {   // the measured configuration: hold Next = start presentation
    Rig r;
    QVERIFY(r.ready());
    QSignalSpy started(r.remote, &Remote::presentationStarted);
    r.fake->notify(hex("11010700" "00da" "000000000000"));
    r.fake->notify(hex("110107000000000000000000"));
    QList<int> keys;
    QTRY_VERIFY((keys += r.keyPipe.pressedKeys(), !keys.isEmpty()));
    QCOMPARE(keys, (QList<int>{KEY_F5}));
    QCOMPARE(started.count(), 1);
    QVERIFY(r.overlay.all.empty());   // not the action button: no effect
  }

  void holdingBackBlanksTheScreen() {
    Rig r;
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00dc" "000000000000"));
    r.fake->notify(hex("110107000000000000000000"));
    QList<int> keys;
    QTRY_VERIFY((keys += r.keyPipe.pressedKeys(), !keys.isEmpty()));
    QCOMPARE(keys, (QList<int>{KEY_B}));
  }

  void movingTheRemoteWhileHoldingNextChangesTheVolume() {
    Config c;
    c.holdNext = HoldAction::Volume;
    Rig r(c);
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00da" "000000000000"));
    for (int i = 0; i < 10; ++i) r.fake->notify(hex("11010710" "0000" "fff1" "00000000000000000000"));   // ten reports of -15
    QList<int> keys;
    QTRY_VERIFY((keys += r.keyPipe.pressedKeys(), !keys.isEmpty()));
    QCOMPARE(keys, (QList<int>{KEY_VOLUMEUP}));
  }

  void movingTheRemoteWhileHoldingBackScrollsThroughTheVirtualPointer() {
    Config c;
    c.holdBack = HoldAction::Scroll;
    Rig r(c);
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00dc" "000000000000"));
    for (int i = 0; i < 6; ++i) r.fake->notify(hex("11010710" "0000" "0014" "00000000000000000000"));   // six reports of +20: 120 counts
    QList<int> notches;
    QTRY_VERIFY((notches += r.pointerPipe.wheel(), notches.size() >= 2));
    QCOMPARE(notches, (QList<int>{-1, -1}));    // one notch per 60 counts; turned down: scrolls down
  }

  void aLowBatteryAtConnectionVibratesFourShortPulsesOnce() {
    Rig r({}, [](FakeRemote* f) { f->batteryPercent = 15; });
    QVERIFY(r.ready());
    const QByteArray shortPulse = hidpp::vibrate(0x09, 1, 0x80);
    QTRY_COMPARE(r.fake->requests.count(shortPulse), Haptics::kBatteryLowPulses);
    r.fake->notify(hex("11010600" "0e4b00" "0000000000000000000000"));   // 14 %: still low, no second warning
    QTest::qWait(100);
    QCOMPARE(r.fake->requests.count(shortPulse), Haptics::kBatteryLowPulses);
    r.fake->notify(hex("11010600" "644b01" "0000000000000000000000"));   // charged again ...
    r.fake->notify(hex("11010600" "0f4b00" "0000000000000000000000"));   // ... and low once more: warns again
    QTRY_COMPARE(r.fake->requests.count(shortPulse), 2 * Haptics::kBatteryLowPulses);
  }

  void noBatteryWarningWhenItIsSwitchedOff() {
    Config c;
    c.batteryWarning = false;
    Rig r(c, [](FakeRemote* f) { f->batteryPercent = 5; });
    QVERIFY(r.ready());
    QTest::qWait(150);
    QCOMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 1, 0x80)), 0);
  }

  void vibrationAtZeroPercentIsSilentIncludingTheConnectionPulse() {
    Config c;
    c.vibrationPercent = 0;
    Rig r(c);
    QVERIFY(r.ready());
    QTest::qWait(100);
    for (const QByteArray& m : std::as_const(r.fake->requests)) QVERIFY2(!(static_cast<uint8_t>(m[2]) == 0x09 && static_cast<uint8_t>(m[3]) == 0x1d), "a vibrate request was sent");
  }

  void theVibrationStrengthFollowsTheSettingAtOnce() {
    Rig r;
    QVERIFY(r.ready());
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0x80)), 1);
    Config c;
    c.vibrationPercent = 100;
    r.remote->setConfig(c);
    r.remote->haptics()->pulses(1);
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 1, 0xff)), 1);
  }

  void cursorControlLeavesTheMovementToTheSystemAndHidesTheEffect() {
    Rig r;
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00d8" "000000000000"));
    r.fake->notify(hex("110107000000000000000000"));
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));    // a frozen effect is visible
    r.fake->requests.clear();
    Config c;
    c.cursorControl = true;
    r.remote->setConfig(c);
    QTRY_VERIFY(r.fake->requests.contains(setCidReporting(0x07, kCidHold, kDivert)));   // no raw X/Y any more
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), cmd(T::Hide));                                         // nothing stays behind
    QTRY_VERIFY(r.fake->requests.contains(setPointerSpeed(0x0a, c.pointerSpeedLevel())));
    r.fake->notify(hex("11010700" "00d8" "000000000000"));     // holding now shows no effect
    QTest::qWait(60);
    QCOMPARE(r.overlay.all.size(), size_t(2));
  }

  void changedSettingsApplyToTheEffectsImmediately() {
    Rig r;
    QVERIFY(r.ready());
    Config c;
    c.pointerSpeed = 70;       // twice the reference gain
    c.modes = {false, true, true};
    r.remote->setConfig(c);
    r.fake->notify(hex("11010700" "00d8" "000000000000"));
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));
    QCOMPARE(r.overlay.all.back(), show(Mode::Magnify));       // highlight is off: the first enabled mode
    r.fake->notify(hex("11010710" "0003" "fffb" "00000000000000000000"));
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), move(6, -10));
  }

  void slideChangesAreAnnounced() {
    Rig r;
    QVERIFY(r.ready());
    QSignalSpy slides(r.remote, &Remote::slideChanged);
    r.keyboard.key(KEY_RIGHT, 1);
    r.keyboard.key(KEY_LEFT, 1);
    QTRY_COMPARE(slides.count(), 2);
  }

  void unpluggingTheKeyboardNodeEmitsGone() {
    Rig r;
    QVERIFY(r.ready());
    QSignalSpy gone(r.remote, &Remote::gone);
    ::close(r.keyboard.theirs);
    r.keyboard.theirs = -1;
    QTRY_COMPARE(gone.count(), 1);
  }

  void deletingTheRemoteReleasesTheDiversions() {
    Rig r;
    QVERIFY(r.ready());
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0x80)), 1);  // the connection pulse has arrived
    r.fake->requests.clear();
    delete r.remote;
    r.remote = nullptr;
    QTRY_VERIFY(r.fake->requests.size() >= 2);
    QCOMPARE(r.fake->requests.at(0).left(7), hex("1101073d00d822"));
    QCOMPARE(r.fake->requests.at(1).left(7), hex("1101073d00df22"));
    QCOMPARE(r.fake->requests.at(2).left(7), hex("1101073d00da22"));
    QCOMPARE(r.fake->requests.at(3).left(7), hex("1101073d00dc22"));
  }
};

QTEST_MAIN(RemoteTest)
#include "test_remote.moc"
