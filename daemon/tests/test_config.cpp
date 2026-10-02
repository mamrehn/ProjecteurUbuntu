// SPDX-License-Identifier: MIT
#include <QJsonArray>
#include <QRandomGenerator>
#include <QtTest>

#include "config.h"

using namespace projecteur;

class ConfigTest : public QObject {
  Q_OBJECT

 private slots:
  void defaultsAreTheReferenceSettingsOfTheWindowsApp() {  // doc/ubuntu/FEATURE-PARITY.md, section 1
    const Config c;
    QCOMPARE(c.pointerSpeed, 35);
    QVERIFY(c.modes[0] && c.modes[1] && c.modes[2]);
    QVERIFY(c.freeze);
    QVERIFY(c.recenter);
    QVERIFY(!c.cursorControl);
    QCOMPARE(c.holdNext, HoldAction::StartPresentation);
    QCOMPARE(c.holdBack, HoldAction::BlankScreen);
    QCOMPARE(c.vibrationPercent, 50);
    QVERIFY(c.batteryWarning);
    QVERIFY(c.timerNotification);
    QVERIFY(!c.timerEnabled);
    QVERIFY(!c.timerMinutePulses);   // the minute code is an addition of this fork: off unless asked for
  }

  void theJsonRoundTrips() {
    Config c;
    c.modes = {true, false, true};
    c.freeze = false;
    c.pointerSpeed = 70;
    c.holdNext = HoldAction::Shortcut;
    c.shortcutNext = {29, 42, 25};
    c.holdBack = HoldAction::Volume;
    c.vibrationPercent = 80;
    c.timerEnabled = true;
    c.timerMinutes = 45;
    c.timerAlerts = {10, 5, 1};
    c.timerMinutePulses = true;
    QStringList problems;
    QCOMPARE(Config::fromJson(c.toJson(), Config{}, &problems), c);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
  }

  void keysArePresentedWithTheNamesOfTheExtensionsSettings() {
    const QJsonObject o = Config{}.toJson();
    for (const char* k : {"highlight-enabled", "magnify-enabled", "laser-enabled", "freeze-effects", "recenter-effects", "cursor-control",
                          "pointer-speed", "hold-next-action", "hold-back-action", "hold-next-shortcut", "hold-back-shortcut",
                          "vibration-intensity", "battery-warning", "timer-notification", "timer-enabled", "timer-minutes",
                          "timer-auto-start", "timer-alerts", "timer-minute-pulses"})
      QVERIFY2(o.contains(QLatin1String(k)), k);
    QCOMPARE(o["hold-next-action"].toString(), QStringLiteral("start-presentation"));
  }

  void onlyThePresentKeysChangeTheBase() {
    Config base;
    base.pointerSpeed = 60;
    const Config c = Config::fromJson(QJsonObject{{"freeze-effects", false}}, base);
    QVERIFY(!c.freeze);
    QCOMPARE(c.pointerSpeed, 60);
  }

  void invalidValuesAreReportedAndIgnored() {
    QStringList problems;
    const Config c = Config::fromJson(QJsonObject{{"pointer-speed", 150},
                                                  {"hold-next-action", "launch-missiles"},
                                                  {"timer-minutes", 0},
                                                  {"timer-alerts", QJsonArray{1, 2, 3, 4}},
                                                  {"freeze-effects", "yes"},
                                                  {"vibration-intensity", 12.5}},
                                      Config{}, &problems);
    QCOMPARE(problems.size(), 6);
    QCOMPARE(c, Config{});   // nothing changed
  }

  void alertSlotsMayBeSwitchedOffWithZero() {
    QStringList problems;
    const Config c = Config::fromJson(QJsonObject{{"timer-alerts", QJsonArray{0, 5, 0}}}, Config{}, &problems);
    QVERIFY(problems.isEmpty());
    QCOMPARE(c.timerAlerts, (QList<int>{0, 5, 0}));
    QVERIFY(!Config::fromJson(QJsonObject{{"timer-alerts", QJsonArray{-1}}}, Config{}, &problems).timerAlerts.contains(-1));
    QVERIFY(!problems.isEmpty());
  }

  void unknownKeysAreIgnoredSilently() {   // the extension sends its appearance settings along
    QStringList problems;
    const Config c = Config::fromJson(QJsonObject{{"highlight-size", 43}, {"laser-color", "#ff0000"}, {"cursor-control", true}}, Config{}, &problems);
    QVERIFY(problems.isEmpty());
    QVERIFY(c.cursorControl);
  }

  void pointerSpeedIsLinearAroundTheReferenceSetting() {
    Config c;
    QCOMPARE(c.pixelsPerCount(), 1.0);   // 35 % = 1 pixel per count
    c.pointerSpeed = 70;
    QCOMPARE(c.pixelsPerCount(), 2.0);
    c.pointerSpeed = 0;
    QVERIFY(c.pixelsPerCount() > 0);     // never stuck
    QCOMPARE(c.effectSettings().pixelsPerCount, c.pixelsPerCount());
  }

  void cursorControlSwitchesTheEffectsOff() {
    Config c;
    c.cursorControl = true;
    const EffectSettings s = c.effectSettings();
    QVERIFY(!s.modeEnabled[0] && !s.modeEnabled[1] && !s.modeEnabled[2]);
  }

  void vibrationPercentMapsToTheIntensityByte() {
    Config c;
    QCOMPARE(c.vibrationIntensity(), uint8_t(0x80));   // Windows' 50 %
    c.vibrationPercent = 100;
    QCOMPARE(c.vibrationIntensity(), uint8_t(0xff));
    c.vibrationPercent = 0;
    QCOMPARE(c.vibrationIntensity(), uint8_t(0));
  }

  void pointerSpeedLevelStaysInTheRemotesRange() {
    Config c;
    c.pointerSpeed = 0;
    QCOMPARE(c.pointerSpeedLevel(), uint8_t(0x10));
    c.pointerSpeed = 100;
    QCOMPARE(c.pointerSpeedLevel(), uint8_t(0x19));
    c.pointerSpeed = 35;
    QVERIFY(c.pointerSpeedLevel() >= 0x10 && c.pointerSpeedLevel() <= 0x19);
  }

  void whateverArrivesOverDBusTheSettingsStayInTheirRanges() {
    // SetConfig takes JSON from anything on the session bus: random keys, types and values, many times over, must
    // never crash and never leave a value outside its range (property test with a fixed seed)
    const Config base;
    const QStringList keys = base.toJson().keys();
    QRandomGenerator rng(0xc0f1);
    const auto randomValue = [&](int depth) -> QJsonValue {
      switch (rng.bounded(depth > 0 ? 7 : 6)) {
        case 0: return QJsonValue(rng.bounded(2) == 1);
        case 1: return QJsonValue(rng.bounded(-1000, 1000));
        case 2: return QJsonValue(rng.generateDouble() * 2e9 - 1e9);
        case 3: return QJsonValue(QStringLiteral("fast-forward"));
        case 4: return QJsonValue(QString::number(rng.bounded(100)));
        case 5: return QJsonValue(QJsonValue::Null);
        default: {
          QJsonArray a;
          for (int i = rng.bounded(9); i > 0; --i) a.append(rng.bounded(-5, 0x320));
          return a;
        }
      }
    };
    Config c = base;
    for (int round = 0; round < 20000; ++round) {
      QJsonObject o;
      for (int i = rng.bounded(1, 6); i > 0; --i) o[keys[rng.bounded(keys.size())]] = randomValue(1);
      if (rng.bounded(10) == 0) o[QStringLiteral("unknown-key")] = randomValue(1);
      c = Config::fromJson(o, c);
      QVERIFY(c.pointerSpeed >= 0 && c.pointerSpeed <= 100);
      QVERIFY(c.vibrationPercent >= 0 && c.vibrationPercent <= 100);
      QVERIFY(c.timerMinutes >= 1 && c.timerMinutes <= 600);
      QVERIFY(c.timerAlerts.size() <= 3);
      for (int a : std::as_const(c.timerAlerts)) QVERIFY(a >= 0 && a <= 600);
      for (const QList<int>* keysOf : {&c.shortcutNext, &c.shortcutBack}) {
        QVERIFY(keysOf->size() <= 6);
        for (int k : *keysOf) QVERIFY(k >= 1 && k <= 0x2ff);
      }
      QVERIFY(c.pointerSpeedLevel() >= 0x10 && c.pointerSpeedLevel() <= 0x19);
      QVERIFY(c.pixelsPerCount() > 0);
      QCOMPARE(Config::fromJson(c.toJson(), Config{}), c);   // and what the daemon reports, it would accept again
    }
  }

  void actionNamesRoundTrip() {
    for (const HoldAction a : {HoldAction::None, HoldAction::StartPresentation, HoldAction::BlankScreen, HoldAction::FastForward,
                               HoldAction::FastRewind, HoldAction::Volume, HoldAction::Scroll, HoldAction::Shortcut})
      QCOMPARE(*holdActionFromName(holdActionName(a)), a);
    QVERIFY(!holdActionFromName("nonsense"));
  }
};

QTEST_APPLESS_MAIN(ConfigTest)
#include "test_config.moc"
