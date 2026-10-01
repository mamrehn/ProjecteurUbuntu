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
