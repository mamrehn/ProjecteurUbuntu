// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>
#include <csignal>

#include "hidpp.h"
#include "fakeremote.h"
#include "hidpplink.h"
#include "spotlightdevice.h"

using namespace projecteur;
using namespace projecteur::hidpp;

namespace {

QByteArray hex(const char* s) { return QByteArray::fromHex(s); }

struct Rig {
  explicit Rig(SpotlightDevice::Config cfg = {}) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0) qFatal("socketpair failed");
    link = new HidppLink(fds[0]);
    remote = new FakeRemote(fds[1]);
    cfg.retryMs = 20;
    cfg.requestTimeoutMs = 60;
    device = new SpotlightDevice(link, cfg);
  }
  ~Rig() { delete device; delete link; delete remote; }
  bool becomesReady(int ms = 3000) { return QTest::qWaitFor([this] { return device->isReady(); }, ms); }
  HidppLink* link;
  FakeRemote* remote;
  SpotlightDevice* device;
};

QByteArray divertTo(uint16_t cid, uint8_t flags) { return setCidReporting(0x07, cid, flags); }

}  // namespace

class SpotlightDeviceTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { std::signal(SIGPIPE, SIG_IGN); }   // writes to a closed fake remote fail with EPIPE, as in the daemon

  void handshakeFindsTheFeaturesAndDivertsTheActionButton() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(r.device->canVibrate());
    QVERIFY(r.remote->requests.contains(getFeatureIndex(Feature::ReprogramControlsV4)));
    QVERIFY(r.remote->requests.contains(getFeatureIndex(Feature::PresenterControl)));
    QVERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x33)));        // hold with raw X/Y: pointer stays still
    QVERIFY(r.remote->requests.contains(divertTo(kCidDoubleClick, 0x03)));
    QVERIFY(r.remote->requests.contains(divertTo(kCidNextHold, 0x33)));    // Next / Back held: with raw X/Y for volume, scrolling
    QVERIFY(r.remote->requests.contains(divertTo(kCidBackHold, 0x33)));
  }

  void batteryAndFirmwareAreReadDuringTheHandshake() {
    Rig r;
    r.remote->batteryPercent = 63;
    r.remote->batteryState = 0;
    QSignalSpy battery(r.device, &SpotlightDevice::batteryChanged), info(r.device, &SpotlightDevice::infoChanged);
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(r.device->hasBattery());
    QCOMPARE(r.device->battery().percent, 63);
    QCOMPARE(r.device->battery().state, BatteryState::Discharging);
    QCOMPARE(battery.count(), 1);
    QCOMPARE(r.device->info().firmware, QStringLiteral("1.1.32"));  // what the Windows app shows for the measured unit
    QCOMPARE(r.device->info().bootloader, QStringLiteral("26.1.15"));
    QCOMPARE(info.count(), 1);
  }

  void aModelWithoutBatteryAndFirmwareFeaturesIsStillReady() {
    Rig r;
    r.remote->features.remove(0x1000);
    r.remote->features.remove(0x0003);
    r.remote->features.remove(0x2205);
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(!r.device->hasBattery());
    QCOMPARE(r.device->battery().percent, -1);
    QVERIFY(r.device->info().firmware.isEmpty());
    r.device->setPointerSpeed(0x18);  // no such feature: nothing is sent
    r.remote->requests.clear();
    QTest::qWait(50);
    QVERIFY(r.remote->requests.isEmpty());
  }

  void batteryLevelStepsAnnouncedByTheRemoteAreReported() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy battery(r.device, &SpotlightDevice::batteryChanged);
    r.remote->notify(hex("11010600" "4b4b00" "0000000000000000000000"));   // 75 %
    QTRY_COMPARE(battery.count(), 1);
    QCOMPARE(battery.at(0).at(0).toInt(), 75);
    r.remote->notify(hex("11010600" "4b4b00" "0000000000000000000000"));   // the same again: no new signal
    r.remote->notify(hex("11010600" "644b01" "0000000000000000000000"));   // plugged in
    QTRY_COMPARE(battery.count(), 2);
    QCOMPARE(battery.at(1).at(1).value<BatteryState>(), BatteryState::Charging);
  }

  void theBatteryIsPolledAsASafetyNet() {
    SpotlightDevice::Config cfg;
    cfg.batteryPollMs = 40;
    Rig r(cfg);
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy battery(r.device, &SpotlightDevice::batteryChanged);
    r.remote->batteryPercent = 9;
    QTRY_COMPARE(battery.count(), 1);
    QCOMPARE(battery.at(0).at(0).toInt(), 9);
  }

  void nextAndBackHoldsAreSeparateFromTheActionButton() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy sideDown(r.device, &SpotlightDevice::sideHoldDown), sideUp(r.device, &SpotlightDevice::sideHoldUp),
        sideMove(r.device, &SpotlightDevice::sideMove), actionMove(r.device, &SpotlightDevice::rawMove),
        actionDown(r.device, &SpotlightDevice::holdDown);
    r.remote->notify(hex("11010700" "00da" "000000000000"));                 // Next held
    r.remote->notify(hex("11010710" "0000" "fff6" "00000000000000000000"));   // moved up by 10
    r.remote->notify(hex("110107000000000000000000"));                      // released
    r.remote->notify(hex("11010700" "00dc" "000000000000"));                 // Back held
    r.remote->notify(hex("11010710" "0000" "0007" "00000000000000000000"));
    r.remote->notify(hex("110107000000000000000000"));
    QTRY_COMPARE(sideUp.count(), 2);
    QCOMPARE(sideDown.count(), 2);
    QCOMPARE(sideDown.at(0).at(0).value<Side>(), Side::Next);
    QCOMPARE(sideDown.at(1).at(0).value<Side>(), Side::Back);
    QCOMPARE(sideMove.count(), 2);
    QCOMPARE(sideMove.at(0).at(0).value<Side>(), Side::Next);
    QCOMPARE(sideMove.at(0).at(2).toInt(), -10);
    QCOMPARE(sideMove.at(1).at(0).value<Side>(), Side::Back);
    QCOMPARE(sideMove.at(1).at(2).toInt(), 7);
    QCOMPARE(actionMove.count(), 0);   // not the action button
    QCOMPARE(actionDown.count(), 0);
  }

  void movementWithNothingHeldIsDropped() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy any(r.device, &SpotlightDevice::rawMove), side(r.device, &SpotlightDevice::sideMove);
    r.remote->notify(hex("11010710" "0003" "fffb" "00000000000000000000"));
    QTest::qWait(60);
    QCOMPARE(any.count(), 0);
    QCOMPARE(side.count(), 0);
  }

  void switchingToCursorControlDivertsTheHoldWithoutRawMovement() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->setRawMovement(false);
    QTRY_VERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x03)));
    r.remote->requests.clear();
    r.device->setRawMovement(true);
    QTRY_VERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x33)));
  }

  void thePointerSpeedIsSetAndThePreviousLevelIsRestoredOnShutdown() {
    Rig r;
    r.remote->pointerSpeed = 0x12;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->setPointerSpeed(0x18);
    QTRY_VERIFY(r.remote->requests.contains(setPointerSpeed(0x0a, 0x18)));
    QTRY_VERIFY(r.remote->requests.contains(getPointerSpeed(0x0a)));   // read the original first
    r.device->setPointerSpeed(0x30);   // out of range: clamped
    QTRY_VERIFY(r.remote->requests.contains(setPointerSpeed(0x0a, 0x19)));
    r.remote->requests.clear();
    r.device->shutdown();
    QTRY_VERIFY(r.remote->requests.contains(setPointerSpeed(0x0a, 0x12)));
  }

  void pointerSpeedIsLeftAloneWhenItWasNeverChanged() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->shutdown();
    QTRY_VERIFY(r.remote->requests.size() >= 4);
    QTest::qWait(30);
    for (const QByteArray& m : std::as_const(r.remote->requests)) QVERIFY(static_cast<uint8_t>(m[2]) != 0x0a);
  }

  void overBluetoothEverythingIsALongMessageToDeviceIndexFF() {
    SpotlightDevice::Config cfg;
    cfg.deviceIndex = kDirectDeviceIndex;
    cfg.longMessagesOnly = true;
    Rig r(cfg);
    r.remote->longOnly = true;  // like the kernel: short reports are rejected
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(!r.remote->requests.isEmpty());
    for (const QByteArray& m : std::as_const(r.remote->requests)) {
      QCOMPARE(m.size(), 20);
      QCOMPARE(static_cast<uint8_t>(m[0]), uint8_t(0x11));
      QCOMPARE(static_cast<uint8_t>(m[1]), uint8_t(0xff));
    }
    r.remote->requests.clear();
    r.device->vibrate(1, 0x80);
    QTRY_COMPARE(r.remote->requests.size(), 1);
    QCOMPARE(static_cast<uint8_t>(r.remote->requests.first()[1]), uint8_t(0xff));
  }

  void withoutRawMovementTheHoldIsDivertedWithoutTheRawBit() {
    SpotlightDevice::Config cfg;
    cfg.rawMovement = false;
    Rig r(cfg);
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x03)));
  }

  void aSleepingRemoteIsAskedAgainUntilItAnswers() {
    Rig r;
    r.remote->ignoreFirst = 3;  // the first three requests get no answer
    r.device->start();
    QVERIFY(r.becomesReady());
    QCOMPARE(r.remote->requests.size(), 3 + 14);  // 3 unanswered, then 5 lookups, 4 diversions, battery, 4 firmware reads
  }

  void buttonAndMovementNotificationsBecomeSignals() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy down(r.device, &SpotlightDevice::holdDown), up(r.device, &SpotlightDevice::holdUp),
        dbl(r.device, &SpotlightDevice::doubleClick), move(r.device, &SpotlightDevice::rawMove);

    r.remote->notify(hex("11010700" "00d8" "000000000000"));                // hold pressed
    r.remote->notify(hex("11010710" "0003" "fffb" "00000000000000000000"));  // dx=3 dy=-5
    r.remote->notify(hex("11010710" "fff6" "0002" "00000000000000000000"));  // dx=-10 dy=2
    r.remote->notify(hex("110107000000000000000000"));                      // released
    r.remote->notify(hex("11010700" "00df" "000000000000"));                // double click
    r.remote->notify(hex("110107000000000000000000"));
    QTRY_COMPARE(up.count(), 1);

    QCOMPARE(down.count(), 1);
    QCOMPARE(dbl.count(), 1);
    QCOMPARE(move.count(), 2);
    QCOMPARE(move.at(0).at(0).toInt(), 3);
    QCOMPARE(move.at(0).at(1).toInt(), -5);
    QCOMPARE(move.at(1).at(0).toInt(), -10);
    QCOMPARE(move.at(1).at(1).toInt(), 2);
  }

  void ordinaryInputReportsOnTheBluetoothNodeAreIgnored() {
    // recorded over Bluetooth: the hidraw node also delivers the keyboard (report 1) and mouse (report 2) input
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy seen(r.link, &HidppLink::notification);
    r.remote->notify(hex("01004f0000000000"));  // Right arrow down
    r.remote->notify(hex("0100000000000000"));  // ... and up
    r.remote->notify(hex("0201000000000000"));  // left button down
    r.remote->notify(hex("0200001400fd0000"));  // movement
    r.remote->notify(hex("11010700" "00d8" "000000000000"));
    QTRY_COMPARE(seen.count(), 1);
    QTest::qWait(50);
    QCOMPARE(seen.count(), 1);
  }

  void aRepeatedHoldReportIsNotAFreshPress() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy down(r.device, &SpotlightDevice::holdDown);
    r.remote->notify(hex("11010700" "00d8" "000000000000"));
    r.remote->notify(hex("11010700" "00d8" "000000000000"));
    QTest::qWait(100);
    QCOMPARE(down.count(), 1);
  }

  void vibrateSendsTheDocumentedLongMessage() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->vibrate(1, 0x80);
    QTRY_COMPARE(r.remote->requests.size(), 1);
    QCOMPARE(r.remote->requests.first(), hidpp::vibrate(0x09, 1, 0x80));
  }

  void vibrateTellsWhenThePulseWasAcknowledged() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    bool done = false;
    r.device->vibrate(1, 0x80, [&] { done = true; });
    QTRY_VERIFY(done);
  }

  void aPulseToASleepingRemoteIsSentAgainUntilItAnswers() {
    SpotlightDevice::Config cfg;
    cfg.vibrateTimeoutMs = 40;
    Rig r(cfg);
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.remote->ignoreFirst = 2;       // it falls asleep: the next two requests get no answer
    int done = 0;
    r.device->vibrate(1, 0x80, [&] { ++done; });
    QTRY_COMPARE(done, 1);
    QCOMPARE(r.remote->requests.count(hidpp::vibrate(0x09, 1, 0x80)), 3);
  }

  void aPulseIsGivenUpAfterTheConfiguredNumberOfTries() {
    SpotlightDevice::Config cfg;
    cfg.vibrateTimeoutMs = 30;
    cfg.vibrateTries = 3;
    Rig r(cfg);
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.remote->ignoreFirst = 100;     // never answers
    int done = 0;
    r.device->vibrate(1, 0x80, [&] { ++done; });
    QTRY_COMPARE(done, 1);
    QCOMPARE(r.remote->requests.count(hidpp::vibrate(0x09, 1, 0x80)), 3);
    QTest::qWait(100);
    QCOMPARE(done, 1);               // called once, not again
  }

  void withoutARemoteTheCallbackStillComes() {
    Rig r;                           // never started: no feature index, cannot vibrate
    bool done = false;
    r.device->vibrate(1, 0x80, [&] { done = true; });
    QVERIFY(done);
  }

  void shutdownReleasesTheDiversions() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->shutdown();
    QTRY_COMPARE(r.remote->requests.size(), 4);
    QCOMPARE(r.remote->requests.at(0), divertTo(kCidHold, 0x22));
    QCOMPARE(r.remote->requests.at(1), divertTo(kCidDoubleClick, 0x22));
    QCOMPARE(r.remote->requests.at(2), divertTo(kCidNextHold, 0x22));
    QCOMPARE(r.remote->requests.at(3), divertTo(kCidBackHold, 0x22));
    QVERIFY(!r.device->isReady());
  }

  void aVanishingDeviceMakesItNotReady() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy ready(r.device, &SpotlightDevice::readyChanged);
    r.remote->closeNow();  // unplugged
    QTRY_VERIFY(!ready.isEmpty());
    QVERIFY(!r.device->isReady());
  }

  void aWakeUpNotificationAppliesTheDiversionsAgain() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.remote->notify(hex("10014100" "000000"));  // short WirelessDeviceStatus, bit clear = awake
    QTRY_VERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x33)));
    QVERIFY(r.becomesReady());
  }

  // ---- regressions --------------------------------------------------------------------------------
  void aReconnectAsTheReceiverReportsItAppliesTheDiversionsAgain() {
    // the real notification carries the protocol type in byte 3 (0x04); it used to be ignored
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.remote->notify(hex("10014104" "041ab0"));
    QTRY_VERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x33)));
    QVERIFY(r.remote->requests.contains(divertTo(kCidNextHold, 0x33)));
    QVERIFY(r.becomesReady());
  }

  void aReconnectOfAnotherDeviceOnTheReceiverChangesNothing() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy ready(r.device, &SpotlightDevice::readyChanged);
    r.remote->requests.clear();
    r.remote->notify(hex("10024104" "041ab0"));   // device index 2
    QTest::qWait(100);
    QVERIFY(r.remote->requests.isEmpty());
    QCOMPARE(ready.count(), 0);
  }

  void controlsHeldWhenTheLinkDropsAreReleased() {
    // the release report of a held control never comes once the remote is out of range or its battery dies;
    // a held "fast forward" would otherwise step through the slides for good
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy up(r.device, &SpotlightDevice::holdUp), sideUp(r.device, &SpotlightDevice::sideHoldUp),
        sideDown(r.device, &SpotlightDevice::sideHoldDown);
    r.remote->notify(hex("11010700" "00d8" "00da" "00000000"));   // action button and Next held together
    QTRY_COMPARE(sideDown.count(), 1);
    r.remote->notify(hex("10014104" "441ab0"));                   // link lost
    QTRY_COMPARE(up.count(), 1);
    QCOMPARE(sideUp.count(), 1);
    QCOMPARE(sideUp.at(0).at(0).value<Side>(), Side::Next);
    r.remote->notify(hex("110107000000000000000000"));            // a late release report changes nothing
    QTest::qWait(50);
    QCOMPARE(up.count(), 1);
    QCOMPARE(sideUp.count(), 1);
  }

  void startingTheHandshakeAgainReleasesHeldControls() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy sideUp(r.device, &SpotlightDevice::sideHoldUp), sideDown(r.device, &SpotlightDevice::sideHoldDown);
    r.remote->notify(hex("11010700" "00dc" "000000000000"));      // Back held
    QTRY_COMPARE(sideDown.count(), 1);
    r.remote->notify(hex("10014104" "041ab0"));                   // reconnect: the handshake starts over
    QTRY_COMPARE(sideUp.count(), 1);
    QCOMPARE(sideUp.at(0).at(0).value<Side>(), Side::Back);
  }

  void aClosedLinkReleasesHeldControls() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy up(r.device, &SpotlightDevice::holdUp), down(r.device, &SpotlightDevice::holdDown);
    r.remote->notify(hex("11010700" "00d8" "000000000000"));
    QTRY_COMPARE(down.count(), 1);
    r.remote->closeNow();
    QTRY_COMPARE(up.count(), 1);
  }

  void aLateAcknowledgementDoesNotPlayThePulseTwice() {
    // measured: an idle remote plays the pulse but answers up to 0.85 s late. Sending it again before that made
    // the remote buzz twice, which changes the meaning of a pattern (alerts differ by their number of pulses).
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.remote->answerDelayMs = 850;
    int done = 0;
    r.device->vibrate(1, 0x80, [&] { ++done; });
    QTRY_COMPARE_WITH_TIMEOUT(done, 1, 3000);
    QCOMPARE(r.remote->requests.count(hidpp::vibrate(0x09, 1, 0x80)), 1);
  }

  void theOriginalPointerSpeedIsReadBeforeItIsChangedEvenWhileARequestIsInFlight() {
    Rig r;
    r.remote->pointerSpeed = 0x12;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->answerDelayMs = 30;
    r.device->vibrate(1, 0x80);        // in flight while the speed is changed
    r.device->setPointerSpeed(0x18);
    QTRY_COMPARE(r.remote->pointerSpeed, 0x18);
    QTest::qWait(150);                 // every answer is in
    r.remote->requests.clear();
    r.device->shutdown();
    QTRY_VERIFY(r.remote->requests.contains(setPointerSpeed(0x0a, 0x12)));   // the level before, not our own
  }

  void aTransientWriteErrorKeepsTheLinkOpen() {
    // only "the device is gone" closes the link; other write errors (a USB transfer error) make a request go
    // unanswered, and its timeout and retry handle that
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QSignalSpy closed(r.link, &HidppLink::closed), down(r.device, &SpotlightDevice::holdDown);
    QCOMPARE(::shutdown(r.remote->fd(), SHUT_RD), 0);   // our writes now fail with EPIPE, reads still work
    r.device->vibrate(1, 0x80);
    QTest::qWait(50);
    QVERIFY(r.link->isOpen());
    QCOMPARE(closed.count(), 0);
    r.remote->notify(hex("11010700" "00d8" "000000000000"));
    QTRY_COMPARE(down.count(), 1);
  }

  void closedIsReportedOnceWhenTheDeviceVanishesWhileAnAnswerIsHandled() {
    int fds[2];
    QCOMPARE(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds), 0);
    HidppLink link(fds[0]);
    QSignalSpy closed(&link, &HidppLink::closed);
    link.request(getFeatureIndex(Feature::Root), [&](const QByteArray&) {
      // like a handshake step: the answer triggers the next request, whose write finds the device gone
      link.request(getFeatureIndex(Feature::PresenterControl), nullptr);
    });
    char buf[64];
    QCOMPARE(::recv(fds[1], buf, sizeof buf, MSG_DONTWAIT), ssize_t(7));   // the first request went out at once
    const QByteArray answer = hex("1001000d000000");
    QCOMPARE(::write(fds[1], answer.constData(), static_cast<size_t>(answer.size())), ssize_t(answer.size()));
    ::close(fds[1]);   // gone right after answering
    QTRY_COMPARE(closed.count(), 1);
    QTest::qWait(50);
    QCOMPARE(closed.count(), 1);
    QVERIFY(!link.isOpen());
  }
};

QTEST_MAIN(SpotlightDeviceTest)
#include "test_spotlightdevice.moc"
