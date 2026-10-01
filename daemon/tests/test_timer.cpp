// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>

#include "haptics.h"
#include "presentationtimer.h"

using namespace projecteur;
using State = PresentationTimer::State;

namespace {
constexpr qint64 kMinute = 60 * 1000;

struct FakeClock {
  qint64 ms = 0;
  PresentationTimer::Clock clock() { return [this] { return ms; }; }
};
}  // namespace

class TimerTest : public QObject {
  Q_OBJECT

 private slots:
  // ---- the timer ---------------------------------------------------------------------------------
  void countsDownAndAnnouncesTheAlertAndTheEnd() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {5});
    QSignalSpy alert(&t, &PresentationTimer::alert), finished(&t, &PresentationTimer::finished);
    QCOMPARE(t.state(), State::Idle);
    QCOMPARE(t.remainingSeconds(), 30 * 60);
    t.start();
    QCOMPARE(t.state(), State::Running);
    c.ms = 24 * kMinute + 59 * 1000; t.tick();
    QCOMPARE(alert.count(), 0);
    c.ms = 25 * kMinute; t.tick();
    QCOMPARE(alert.count(), 1);
    QCOMPARE(alert.at(0).at(0).toInt(), 5);
    QCOMPARE(t.remainingSeconds(), 5 * 60);
    c.ms = 26 * kMinute; t.tick();
    QCOMPARE(alert.count(), 1);   // once only
    c.ms = 30 * kMinute; t.tick();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(t.state(), State::Finished);
    QCOMPARE(t.remainingSeconds(), 0);
  }

  void severalAlertsFireInOrderOfTheClock() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {1, 10, 5});   // given in any order
    QList<int> fired;
    connect(&t, &PresentationTimer::alert, this, [&](int m) { fired.append(m); });
    t.start();
    for (qint64 minute = 1; minute <= 30; ++minute) { c.ms = minute * kMinute; t.tick(); }
    QCOMPARE(fired, (QList<int>{10, 5, 1}));
  }

  void anAlertBeyondTheTotalTimeIsNeverAnnounced() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(3, {5, 1});   // "5 minutes remaining" makes no sense for a 3 minute timer
    QList<int> fired;
    connect(&t, &PresentationTimer::alert, this, [&](int m) { fired.append(m); });
    t.start();
    c.ms = 2 * kMinute; t.tick();
    QCOMPARE(fired, (QList<int>{1}));
  }

  void pauseKeepsTheRemainingTime() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(10, {});
    t.start();
    c.ms = 4 * kMinute; t.tick();
    t.pause();
    QCOMPARE(t.state(), State::Paused);
    c.ms = 100 * kMinute;            // a long break: nothing runs
    t.tick();
    QCOMPARE(t.remainingSeconds(), 6 * 60);
    t.resume();
    c.ms += 6 * kMinute; t.tick();
    QCOMPARE(t.state(), State::Finished);
  }

  void changingTheSettingsMidTalkDoesNotRestartTheClock() {   // per-app profiles switch with the focused window
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {5});
    t.start();
    c.ms = 10 * kMinute;
    t.configure(60, {10, 2});    // another profile
    QCOMPARE(t.remainingSeconds(), 20 * 60);
    QCOMPARE(t.totalSeconds(), 30 * 60);
    c.ms = 20 * kMinute; t.tick();   // the new alert at 10 minutes remaining
    QSignalSpy alert(&t, &PresentationTimer::alert);
    c.ms = 28 * kMinute; t.tick();
    QCOMPARE(alert.count(), 1);
    QCOMPARE(alert.at(0).at(0).toInt(), 2);
  }

  void resetReturnsToIdleWithTheFullTime() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(15, {});
    t.start();
    c.ms = 5 * kMinute; t.tick();
    t.reset();
    QCOMPARE(t.state(), State::Idle);
    QCOMPARE(t.remainingSeconds(), 15 * 60);
    t.configure(20, {});             // idle: the new duration applies at once
    QCOMPARE(t.remainingSeconds(), 20 * 60);
  }

  void aFinishedTimerCanBeStartedAgain() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(1, {});
    t.start();
    c.ms = kMinute; t.tick();
    QCOMPARE(t.state(), State::Finished);
    t.start();
    QCOMPARE(t.state(), State::Running);
    QCOMPARE(t.remainingSeconds(), 60);
  }

  void theRemainingSecondsAreAnnouncedOncePerSecond() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(1, {});
    t.start();
    QSignalSpy remaining(&t, &PresentationTimer::remainingChanged);
    for (int i = 0; i < 8; ++i) { c.ms += 250; t.tick(); }   // two seconds in four steps each
    QCOMPARE(remaining.count(), 2);
  }

  // ---- the vibration patterns --------------------------------------------------------------------
  void aPatternIsNShortPulsesInARow() {
    QList<QPair<int, int>> out;   // length, intensity
    Haptics h([&](uint8_t len, uint8_t inten) { out.append({len, inten}); }, 15);
    h.setIntensity(0x80);
    h.pulses(3);
    QTRY_COMPARE(out.size(), 3);
    for (const auto& p : std::as_const(out)) {
      QCOMPARE(p.first, 1);       // short pulses only
      QCOMPARE(p.second, 0x80);
    }
  }

  void patternsQueueInsteadOfOverlapping() {
    QList<int> lengths;
    Haptics h([&](uint8_t len, uint8_t) { lengths.append(len); }, 10);
    h.pulses(2);
    h.connected();
    h.pulses(1);
    QTRY_COMPARE(lengths.size(), 4);
    QCOMPARE(lengths, (QList<int>{1, 1, 3, 1}));
  }

  void theGapBetweenPulsesIsKept() {
    QList<qint64> times;
    QElapsedTimer clock;
    clock.start();
    Haptics h([&](uint8_t, uint8_t) { times.append(clock.elapsed()); }, 60);
    h.pulses(3);
    QTRY_COMPARE(times.size(), 3);
    QVERIFY(times[1] - times[0] >= 55);
    QVERIFY(times[2] - times[1] >= 55);
  }

  void zeroIntensityIsSilent() {
    int calls = 0;
    Haptics h([&](uint8_t, uint8_t) { ++calls; }, 5);
    h.setIntensity(0);
    h.pulses(3);
    h.connected();
    QTest::qWait(60);
    QCOMPARE(calls, 0);
  }

  void theAlertsAreToldApartByTheirPulseCount() {
    QVERIFY(Haptics::kTimerAlertPulses != Haptics::kTimerEndPulses);
    QVERIFY(Haptics::kTimerEndPulses != Haptics::kBatteryLowPulses);
    QVERIFY(Haptics::kTimerAlertPulses != Haptics::kBatteryLowPulses);
  }
};

QTEST_MAIN(TimerTest)
#include "test_timer.moc"
