// SPDX-License-Identifier: MIT
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QProcess>
#include <QtTest>

#include "overlayclient.h"

using namespace projecteur;
using T = Command::Type;

namespace {

/// Plays the GNOME Shell extension: records every call of org.projecteur.Overlay1 and whether it wanted a reply.
class FakeOverlay : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.projecteur.Overlay1")
 public:
  struct Call {
    QString method;
    QVariantList arguments;
    bool replyWanted;
  };
  QList<Call> calls;

 public slots:
  void ShowAtPointer(const QString&) { record(); }
  void MoveBy(double, double) { record(); }
  void Hide() { record(); }
  void SetMode(const QString&) { record(); }
  void Recenter() { record(); }

 private:
  void record() { calls.append({message().member(), message().arguments(), message().isReplyRequired()}); }
};

/// A private dbus-daemon for one test, so nothing ever reaches the real session bus.
struct PrivateBus {
  PrivateBus() {
    process.start("dbus-daemon", {"--session", "--print-address=1", "--nofork"});
    if (process.waitForStarted(5000) && process.waitForReadyRead(5000)) address = QString::fromUtf8(process.readLine()).trimmed();
  }
  ~PrivateBus() {
    for (const QString& name : std::as_const(connections)) QDBusConnection::disconnectFromBus(name);
    process.kill();
    process.waitForFinished(5000);
  }
  QDBusConnection connect(const QString& name) {
    connections.append(name);
    return QDBusConnection::connectToBus(address, name);
  }
  QProcess process;
  QString address;
  QStringList connections;
};

Command show(Mode m) { Command c{T::ShowAtPointer}; c.mode = m; return c; }
Command move(double dx, double dy) { Command c{T::MoveBy}; c.dx = dx; c.dy = dy; return c; }
Command setMode(Mode m) { Command c{T::SetMode}; c.mode = m; return c; }

}  // namespace

class OverlayClientTest : public QObject {
  Q_OBJECT

 private slots:
  void everyCommandArrivesInOrderAndOnlyTheMovementGoesWithoutAReply() {
    PrivateBus bus;
    QVERIFY2(!bus.address.isEmpty(), "dbus-daemon is needed for this test");
    QDBusConnection shell = bus.connect("shell"), daemon = bus.connect("daemon");
    FakeOverlay overlay;
    QVERIFY(shell.registerObject("/org/projecteur/Overlay", &overlay, QDBusConnection::ExportAllSlots));
    QVERIFY(shell.registerService("org.projecteur.Overlay"));

    OverlayClient client(daemon);
    client.apply({show(Mode::Magnify), move(3, -5), move(-1.5, 2), cmd(T::Recenter), cmd(T::Hide), setMode(Mode::Laser)});
    QTRY_COMPARE(overlay.calls.size(), 6);

    const QStringList methods = [&] { QStringList m; for (const auto& c : overlay.calls) m << c.method; return m; }();
    QCOMPARE(methods, (QStringList{"ShowAtPointer", "MoveBy", "MoveBy", "Recenter", "Hide", "SetMode"}));
    QCOMPARE(overlay.calls[0].arguments, (QVariantList{QStringLiteral("magnify")}));
    QCOMPARE(overlay.calls[1].arguments, (QVariantList{3.0, -5.0}));
    QCOMPARE(overlay.calls[2].arguments, (QVariantList{-1.5, 2.0}));
    QCOMPARE(overlay.calls[5].arguments, (QVariantList{QStringLiteral("laser")}));
    for (const auto& c : std::as_const(overlay.calls))
      QCOMPARE(c.replyWanted, c.method != QLatin1String("MoveBy"));   // ~100 a second: no reply, no round trip
  }

  void aLongHoldLosesNoMovement() {
    PrivateBus bus;
    QVERIFY2(!bus.address.isEmpty(), "dbus-daemon is needed for this test");
    QDBusConnection shell = bus.connect("shell"), daemon = bus.connect("daemon");
    FakeOverlay overlay;
    QVERIFY(shell.registerObject("/org/projecteur/Overlay", &overlay, QDBusConnection::ExportAllSlots));
    QVERIFY(shell.registerService("org.projecteur.Overlay"));

    OverlayClient client(daemon);
    client.apply({show(Mode::Highlight)});
    constexpr int kReports = 3000;   // half a minute of movement at the measured rate
    for (int i = 0; i < kReports; ++i) client.apply({move(i % 7 - 3, 1)});
    QTRY_COMPARE_WITH_TIMEOUT(overlay.calls.size(), kReports + 1, 10000);
    double dx = 0, dy = 0;
    for (int i = 1; i <= kReports; ++i) {
      dx += overlay.calls[i].arguments[0].toDouble();
      dy += overlay.calls[i].arguments[1].toDouble();
    }
    double expectedDx = 0;
    for (int i = 0; i < kReports; ++i) expectedDx += i % 7 - 3;
    QCOMPARE(dx, expectedDx);
    QCOMPARE(dy, double(kReports));
  }

  void withoutTheExtensionNothingBlocksOrCrashes() {
    PrivateBus bus;
    QVERIFY2(!bus.address.isEmpty(), "dbus-daemon is needed for this test");
    QDBusConnection daemon = bus.connect("daemon");
    OverlayClient client(daemon);
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < 100; ++i) client.apply({show(Mode::Laser), move(1, 1), cmd(T::Hide)});
    QVERIFY(t.elapsed() < 1000);   // asynchronous: never waits for the missing shell
    QTest::qWait(100);             // the error replies come back and are handled
  }

 private:
  static Command cmd(T t) { return Command{t}; }
};

QTEST_MAIN(OverlayClientTest)
#include "test_overlayclient.moc"
