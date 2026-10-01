// SPDX-License-Identifier: MIT
#include <QtTest>

#include "hidpp.h"

using namespace projecteur::hidpp;

namespace {
QByteArray hex(const char* s) { return QByteArray::fromHex(s); }
constexpr uint8_t kReprog = 0x07;  // feature index of ReprogramControlsV4 on the measured unit
}  // namespace

class HidppTest : public QObject {
  Q_OBJECT

 private slots:
  // ---- requests: byte for byte what the working tools sent --------------------------------------
  void featureLookupIsAShortMessage() {
    QCOMPARE(getFeatureIndex(Feature::ReprogramControlsV4), hex("1001000d1b0400"));
    QCOMPARE(getFeatureIndex(Feature::PresenterControl), hex("1001000d1a0000"));
  }

  void bluetoothNeedsTheLongFormOfTheFeatureLookup() {
    // the Bluetooth hidraw node declares only report 0x11, and the device is addressed with index 0xff
    const QByteArray m = getFeatureIndex(Feature::ReprogramControlsV4, kDirectDeviceIndex, true);
    QCOMPARE(m.size(), 20);
    QCOMPARE(m.left(7), hex("11ff000d1b0400"));
    QVERIFY(m.mid(7).count('\0') == 13);
  }

  void divertingTheHoldWithRawXYMatchesUpstreamsDocumentedCommand() {
    // {0x11, 0x01, 0x07, 0x3d, 0x00, 0xd8, 0x33, 0 ...} (20 bytes), see doc/LogitechSpotlightHID++.md
    const QByteArray m = setCidReporting(kReprog, kCidHold, kDivertWithRawXY);
    QCOMPARE(m.size(), 20);
    QCOMPARE(m.left(7), hex("1101073d00d833"));
    QVERIFY(m.mid(7).count('\0') == 13);
  }

  void divertingTheDoubleClickAndUndiverting() {
    QCOMPARE(setCidReporting(kReprog, kCidDoubleClick, kDivert).left(7), hex("1101073d00df03"));
    QCOMPARE(setCidReporting(kReprog, kCidHold, kUndivert).left(7), hex("1101073d00d822"));
  }

  void vibrateIsALongMessageWithFunction1() {
    // length 1, intensity 0x80 on the measured unit (PresenterControl at feature index 9)
    const QByteArray m = vibrate(0x09, 1, 0x80);
    QCOMPARE(m.size(), 20);
    QCOMPARE(m.left(7), hex("1101091d01e880"));
  }

  // ---- notifications: lines copied from the recordings of 2026-10-01 --------------------------------
  void holdPressAndRelease() {
    auto down = decode(hex("11010700" "00d8" "000000000000"), kReprog);
    QVERIFY(down && std::holds_alternative<ButtonsChanged>(*down));
    QCOMPARE(std::get<ButtonsChanged>(*down).pressed, (QList<uint16_t>{kCidHold}));

    auto up = decode(hex("110107000000000000000000"), kReprog);
    QVERIFY(up && std::holds_alternative<ButtonsChanged>(*up));
    QVERIFY(std::get<ButtonsChanged>(*up).pressed.isEmpty());
  }

  void doubleClick() {
    auto e = decode(hex("11010700" "00df" "000000000000"), kReprog);
    QVERIFY(e && std::holds_alternative<ButtonsChanged>(*e));
    QCOMPARE(std::get<ButtonsChanged>(*e).pressed, (QList<uint16_t>{kCidDoubleClick}));
  }

  void rawMovementIsTwoSignedBigEndianCounts() {
    auto e = decode(hex("11010710" "0003" "fffb" "00000000000000000000"), kReprog);
    QVERIFY(e && std::holds_alternative<RawMove>(*e));
    QCOMPARE(std::get<RawMove>(*e).dx, 3);
    QCOMPARE(std::get<RawMove>(*e).dy, -5);

    auto big = decode(hex("11010710" "0100" "fe00" "00000000000000000000"), kReprog);
    QCOMPARE(std::get<RawMove>(*big).dx, 256);
    QCOMPARE(std::get<RawMove>(*big).dy, -512);
  }

  void answersToOurRequestsAreNotEvents() {
    // software id 0xd in byte 3: the answer to a feature lookup, not a notification
    QVERIFY(!decode(hex("1001000d070004"), kReprog));
    QVERIFY(!decode(hex("1101073d00d83300000000000000"), kReprog));
  }

  void eventsAreIgnoredUntilTheFeatureIndexIsKnown() {
    QVERIFY(!decode(hex("11010700" "00d8" "000000000000"), 0));
  }

  void errorsAreReported() {
    auto e = decode(hex("1001ff091d0500"), kReprog);
    QVERIFY(e && std::holds_alternative<ErrorReply>(*e));
    QCOMPARE(std::get<ErrorReply>(*e).featureIndex, 0x09);
    QCOMPARE(std::get<ErrorReply>(*e).code, 0x05);
  }

  void garbageIsRejected() {
    QVERIFY(!decode(QByteArray(), kReprog));
    QVERIFY(!decode(hex("0201"), kReprog));
    QVERIFY(!decode(hex("20010700000000"), kReprog));  // DJ report, not HID++
  }

  // ---- next / back hold, battery, firmware, pointer speed ------------------------------------------
  void divertingNextAndBackHoldMatchesUpstreamsDocumentedCommands() {
    // {0x11, 0x01, 0x07, 0x3d, 0x00, 0xda, 0x33, ...} and ... 0xdc ..., doc/LogitechSpotlightHID++.md
    QCOMPARE(setCidReporting(kReprog, kCidNextHold, kDivertWithRawXY).left(7), hex("1101073d00da33"));
    QCOMPARE(setCidReporting(kReprog, kCidBackHold, kDivertWithRawXY).left(7), hex("1101073d00dc33"));
  }

  void nextAndBackHoldAreReportedLikeTheActionButton() {
    auto next = decode(hex("11010700" "00da" "000000000000"), kReprog);
    QVERIFY(next && std::holds_alternative<ButtonsChanged>(*next));
    QCOMPARE(std::get<ButtonsChanged>(*next).pressed, (QList<uint16_t>{kCidNextHold}));
    auto back = decode(hex("11010700" "00dc" "000000000000"), kReprog);
    QCOMPARE(std::get<ButtonsChanged>(*back).pressed, (QList<uint16_t>{kCidBackHold}));
  }

  void batteryRequestsMatchUpstreamsDocumentedCommand() {
    QCOMPARE(getBatteryStatus(0x06), hex("1001060d000000"));  // {0x10, 0x01, 0x06, 0x0d, 0x00, 0x00, 0x00}
    QCOMPARE(getBatteryStatus(0x06, kDirectDeviceIndex, true).left(7), hex("11ff060d000000"));
  }

  void batteryAnswerFromTheMeasuredBluetoothUnit() {
    // 2026-10-01: "11 ff 06 0d 64 4b 00 ..." = 100 %, next report at 75 %, discharging
    const auto s = parseBatteryStatus(hex("11ff060d644b00000000000000000000000000"));
    QVERIFY(s);
    QCOMPARE(s->percent, 100);
    QCOMPARE(s->nextPercent, 75);
    QCOMPARE(s->state, BatteryState::Discharging);
    QVERIFY(!isCharging(s->state));
    QVERIFY(!parseBatteryStatus(hex("11ff060d64")));
  }

  void batteryNotificationsAreEventsOfTheBatteryFeature() {
    auto e = decode(hex("11ff0600" "504b00" "0000000000000000000000"), kReprog, 0, 0x06);
    QVERIFY(e && std::holds_alternative<BatteryChanged>(*e));
    QCOMPARE(std::get<BatteryChanged>(*e).status.percent, 80);
    auto charging = decode(hex("11ff0600" "636401" "0000000000000000000000"), kReprog, 0, 0x06);
    QVERIFY(isCharging(std::get<BatteryChanged>(*charging).status.state));
    QVERIFY(!decode(hex("11ff0600" "504b00" "0000000000000000000000"), kReprog, 0, 0));  // battery index unknown
  }

  void firmwareInfoFromTheMeasuredBluetoothUnit() {
    // the Windows app showed 1.1.32 for the main firmware of this unit
    QCOMPARE(parseFirmwareEntityCount(hex("11ff020d036d87f30a0006b503405c000000000000")), 3);
    const auto main = parseFirmwareInfo(hex("11ff021d00" "4d504f" "0101" "0032" "01b5037a266fd500"));
    QVERIFY(main);
    QCOMPARE(main->type, 0);
    QCOMPARE(main->name, QStringLiteral("MPO"));
    QCOMPARE(main->version, QStringLiteral("1.1.32"));
    const auto boot = parseFirmwareInfo(hex("11ff021d01" "424f54" "2601" "0015" "00000000007a266fd500"));
    QCOMPARE(boot->type, 1);
    QCOMPARE(boot->name, QStringLiteral("BOT"));
    QCOMPARE(boot->version, QStringLiteral("26.1.15"));
    QVERIFY(!parseFirmwareInfo(hex("11ff021d00")));
  }

  void firmwareRequests() {
    QCOMPARE(getFirmwareEntityCount(0x02), hex("1001020d000000"));
    QCOMPARE(getFirmwareInfo(0x02, 1), hex("1001021d010000"));
  }

  void pointerSpeedIsALongMessageWithTheLevelInTheHighByte() {
    // upstream resets the speed with {0x10, 0x01, 0x0a, 0x1d, 0x14, 0x00, 0x00}; ours is the long form
    const QByteArray m = setPointerSpeed(0x0a, kPointerSpeedDefault);
    QCOMPARE(m.size(), 20);
    QCOMPARE(m.left(7), hex("11010a1d140000"));
  }

  // ---- matching answers to requests ----------------------------------------------------------------
  void answerMatchesSameDeviceFeatureAndFunction() {
    const QByteArray req = getFeatureIndex(Feature::ReprogramControlsV4);
    QVERIFY(answers(req, hex("1001000d070004")));
    QVERIFY(!answers(req, hex("1001010d070004")));  // other feature index
    QVERIFY(!answers(req, hex("1002000d070004")));  // other device
  }

  void errorAnswersCountAsAnswers() {
    const QByteArray req = vibrate(0x09, 1, 0x80);
    QVERIFY(answers(req, hex("1001ff091d0500")));
    QVERIFY(!answers(req, hex("1001ff071d0500")));    // error for another feature
  }
};

QTEST_APPLESS_MAIN(HidppTest)
#include "test_hidpp.moc"
