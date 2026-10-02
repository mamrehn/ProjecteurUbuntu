// SPDX-License-Identifier: MIT
#include <QElapsedTimer>
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

  void switchedOffAlertSlotsAreIgnored() {   // the settings window keeps three positional slots, 0 = off
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {0, 5, 0});
    QList<int> fired;
    connect(&t, &PresentationTimer::alert, this, [&](int m) { fired.append(m); });
    t.start();
    for (qint64 minute = 1; minute <= 30; ++minute) { c.ms = minute * kMinute; t.tick(); }
    QCOMPARE(fired, (QList<int>{5}));
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

  void anAlertAddedDuringTheTalkForAMomentAlreadyPastIsNotAnnounced() {
    // regression: switching to a profile with a 10 minute alert when only 4 minutes are left buzzed "10 minutes left"
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {5});
    QList<int> fired;
    connect(&t, &PresentationTimer::alert, this, [&](int m) { fired.append(m); });
    t.start();
    c.ms = 26 * kMinute; t.tick();           // 4 minutes left: the 5 minute alert has fired
    QCOMPARE(fired, (QList<int>{5}));
    t.configure(30, {10, 5, 2});             // another profile
    c.ms += 1000; t.tick();
    QCOMPARE(fired, (QList<int>{5}));        // not 10, it is long past
    c.ms = 28 * kMinute; t.tick();
    QCOMPARE(fired, (QList<int>{5, 2}));     // a future one still comes
    t.pause();
    t.configure(30, {3});                    // while paused (2 minutes left): also in the past
    t.resume();
    c.ms += 1000; t.tick();
    QCOMPARE(fired, (QList<int>{5, 2}));
  }

  void theShownSecondChangesOnTimeWithTheRealClock() {
    // the timer wakes up when the shown second changes, not on a fixed poll; check that it really does
    PresentationTimer t;
    t.configure(1, {});
    QList<int> seconds;
    QElapsedTimer elapsed;
    qint64 firstChangeMs = -1;
    connect(&t, &PresentationTimer::remainingChanged, this, [&](int s) {
      seconds.append(s);
      if (s == 59 && firstChangeMs < 0) firstChangeMs = elapsed.elapsed();
    });
    elapsed.start();
    t.start();
    QTRY_VERIFY_WITH_TIMEOUT(seconds.contains(58), 3000);
    QVERIFY2(firstChangeMs >= 990 && firstChangeMs < 1500, qPrintable(QString::number(firstChangeMs)));   // slack for a busy CI
    QCOMPARE(seconds.mid(0, 3), (QList<int>{60, 59, 58}));   // each second once, none skipped
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

  // ---- minute ticks of the timer --------------------------------------------------------------------
  void everyFullMinuteIsAnnouncedOnceButNotTheEnd() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(5, {});
    QList<int> minutes;
    connect(&t, &PresentationTimer::minuteElapsed, this, [&](int m) { minutes.append(m); });
    t.start();
    c.ms = 59 * 1000; t.tick();
    QCOMPARE(minutes.size(), 0);
    for (int m = 1; m <= 5; ++m) { c.ms = m * kMinute; t.tick(); t.tick(); }   // ticking twice announces nothing twice
    QCOMPARE(minutes, (QList<int>{1, 2, 3, 4}));   // minute 5 is the end: `finished`, not a tick
  }

  void aLongGapAnnouncesOnlyTheCurrentMinute() {   // the computer slept
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(30, {});
    QList<int> minutes;
    connect(&t, &PresentationTimer::minuteElapsed, this, [&](int m) { minutes.append(m); });
    t.start();
    c.ms = 7 * kMinute + 20 * 1000; t.tick();
    QCOMPARE(minutes, (QList<int>{7}));
  }

  void minuteTicksSurvivePauseAndRestartFromOne() {
    FakeClock c;
    PresentationTimer t(c.clock());
    t.configure(10, {});
    QList<int> minutes;
    connect(&t, &PresentationTimer::minuteElapsed, this, [&](int m) { minutes.append(m); });
    t.start();
    c.ms = 90 * 1000; t.tick();               // 1:30 in: minute 1 announced
    t.pause();
    c.ms = 50 * kMinute;
    t.resume();
    c.ms += 30 * 1000; t.tick();              // 2:00 in
    QCOMPARE(minutes, (QList<int>{1, 2}));
    t.reset();
    t.start();
    c.ms += kMinute; t.tick();
    QCOMPARE(minutes, (QList<int>{1, 2, 1}));  // a new talk counts from 1 again
  }

  // ---- the vibration patterns --------------------------------------------------------------------
  void aPatternIsNShortPulsesInARow() {
    QList<QPair<int, int>> out;   // length, intensity
    Haptics h([&](uint8_t len, uint8_t inten, Haptics::Done done) { out.append({len, inten}); done(); }, 15, 30);
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
    Haptics h([&](uint8_t len, uint8_t, Haptics::Done done) { lengths.append(len); done(); }, 10, 20);
    h.pulses(2);
    h.connected();
    h.pulses(1);
    QTRY_COMPARE(lengths.size(), 4);
    QCOMPARE(lengths, (QList<int>{1, 1, 3, 1}));
  }

  void theGapBetweenPulsesIsKeptAndPatternsAreSeparatedMore() {
    QList<qint64> times;
    QElapsedTimer clock;
    clock.start();
    Haptics h([&](uint8_t, uint8_t, Haptics::Done done) { times.append(clock.elapsed()); done(); }, 60, 200);
    h.pulses(2);
    h.pulses(1);
    QTRY_COMPARE(times.size(), 3);
    QVERIFY(times[1] - times[0] >= 55);      // within a pattern
    QVERIFY(times[2] - times[1] >= 190);     // between two patterns
  }

  void theNextPulseWaitsUntilThePreviousOneIsDone() {   // a sleeping remote answers late: the pattern must not be squeezed
    QList<qint64> times;
    QList<Haptics::Done> pending;
    QElapsedTimer clock;
    clock.start();
    Haptics h([&](uint8_t, uint8_t, Haptics::Done done) { times.append(clock.elapsed()); pending.append(done); }, 30, 60);
    h.pulses(2);
    QTRY_COMPARE(times.size(), 1);
    QTest::qWait(150);
    QCOMPARE(times.size(), 1);               // still waiting for the first pulse to be acknowledged
    pending[0]();
    pending[0]();                            // calling it twice changes nothing
    QTRY_COMPARE(times.size(), 2);
    QVERIFY(times[1] >= 150 + 25);           // the gap starts when the first one is over
  }

  void zeroIntensityIsSilent() {
    int calls = 0;
    Haptics h([&](uint8_t, uint8_t, Haptics::Done done) { ++calls; done(); }, 5, 10);
    h.setIntensity(0);
    h.pulses(3);
    h.connected();
    h.minute(7);
    QTest::qWait(60);
    QCOMPARE(calls, 0);
  }

  void theAlertsAreToldApartByTheirPulseCount() {
    QVERIFY(Haptics::kTimerAlertPulses != Haptics::kTimerEndPulses);
    QVERIFY(Haptics::kTimerEndPulses != Haptics::kBatteryLowPulses);
    QVERIFY(Haptics::kTimerAlertPulses != Haptics::kBatteryLowPulses);
  }

  // ---- the minute code -----------------------------------------------------------------------------
  void theMinuteCodeCountsLikeATally() {
    const auto S = Haptics::kShortPulse, L = Haptics::kLongPulse;
    using P = QList<uint8_t>;
    QCOMPARE(Haptics::minutePattern(1), (P{S}));
    QCOMPARE(Haptics::minutePattern(2), (P{S, S}));
    QCOMPARE(Haptics::minutePattern(4), (P{S, S, S, S}));
    QCOMPARE(Haptics::minutePattern(5), (P{L}));
    QCOMPARE(Haptics::minutePattern(6), (P{L, S}));
    QCOMPARE(Haptics::minutePattern(9), (P{L, S, S, S, S}));
    QCOMPARE(Haptics::minutePattern(10), (P{L, L}));
    QCOMPARE(Haptics::minutePattern(11), Haptics::minutePattern(1));   // starts again
    QCOMPARE(Haptics::minutePattern(15), Haptics::minutePattern(5));
    QCOMPARE(Haptics::minutePattern(20), Haptics::minutePattern(10));
    QCOMPARE(Haptics::minutePattern(21), Haptics::minutePattern(1));
    QVERIFY(Haptics::minutePattern(0).isEmpty());
    QVERIFY(Haptics::minutePattern(-3).isEmpty());
  }

  void everyMinutePatternIsDistinctWithinTenMinutes() {
    QSet<QList<uint8_t>> seen;
    for (int m = 1; m <= 10; ++m) seen.insert(Haptics::minutePattern(m));
    QCOMPARE(seen.size(), 10);
  }

  void theMinuteCodeIsPlayedAsLongAndShortPulses() {
    QList<int> lengths;
    Haptics h([&](uint8_t len, uint8_t, Haptics::Done done) { lengths.append(len); done(); }, 5, 10);
    h.minute(8);
    QTRY_COMPARE(lengths.size(), 4);
    QCOMPARE(lengths, (QList<int>{2, 1, 1, 1}));
  }
};

QTEST_MAIN(TimerTest)
#include "test_timer.moc"
