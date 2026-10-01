// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>

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
  void handshakeFindsTheFeaturesAndDivertsTheActionButton() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    QVERIFY(r.device->canVibrate());
    QVERIFY(r.remote->requests.contains(getFeatureIndex(Feature::ReprogramControlsV4)));
    QVERIFY(r.remote->requests.contains(getFeatureIndex(Feature::PresenterControl)));
    QVERIFY(r.remote->requests.contains(divertTo(kCidHold, 0x33)));        // hold with raw X/Y: pointer stays still
    QVERIFY(r.remote->requests.contains(divertTo(kCidDoubleClick, 0x03)));
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
    QCOMPARE(r.remote->requests.size(), 3 + 4);  // 3 unanswered, then 2 feature lookups + 2 diversions
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

  void shutdownReleasesTheDiversions() {
    Rig r;
    r.device->start();
    QVERIFY(r.becomesReady());
    r.remote->requests.clear();
    r.device->shutdown();
    QTRY_COMPARE(r.remote->requests.size(), 2);
    QCOMPARE(r.remote->requests.at(0), divertTo(kCidHold, 0x22));
    QCOMPARE(r.remote->requests.at(1), divertTo(kCidDoubleClick, 0x22));
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
};

QTEST_MAIN(SpotlightDeviceTest)
#include "test_spotlightdevice.moc"
