// SPDX-License-Identifier: MIT
#include <QJsonArray>
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
    QStringList problems;
    QCOMPARE(Config::fromJson(c.toJson(), Config{}, &problems), c);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
  }

  void keysArePresentedWithTheNamesOfTheExtensionsSettings() {
    const QJsonObject o = Config{}.toJson();
    for (const char* k : {"highlight-enabled", "magnify-enabled", "laser-enabled", "freeze-effects", "recenter-effects", "cursor-control",
                          "pointer-speed", "hold-next-action", "hold-back-action", "hold-next-shortcut", "hold-back-shortcut",
                          "vibration-intensity", "battery-warning", "timer-notification", "timer-enabled", "timer-minutes",
                          "timer-auto-start", "timer-alerts"})
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

  void actionNamesRoundTrip() {
    for (const HoldAction a : {HoldAction::None, HoldAction::StartPresentation, HoldAction::BlankScreen, HoldAction::FastForward,
                               HoldAction::FastRewind, HoldAction::Volume, HoldAction::Scroll, HoldAction::Shortcut})
      QCOMPARE(*holdActionFromName(holdActionName(a)), a);
    QVERIFY(!holdActionFromName("nonsense"));
  }
};

QTEST_APPLESS_MAIN(ConfigTest)
#include "test_config.moc"
