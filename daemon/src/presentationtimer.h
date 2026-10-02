// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QObject>
#include <QTimer>
#include <functional>

namespace projecteur {

/// The presentation timer: counts down from the configured time and announces the alerts ("5 minutes remaining")
/// and the end. Time is read through an injectable clock, so tests do not wait.
class PresentationTimer : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Running, Paused, Finished };
  using Clock = std::function<qint64()>;  ///< milliseconds, monotonic

  explicit PresentationTimer(Clock clock = {}, QObject* parent = nullptr);

  /// Duration in minutes and the alert thresholds (minutes remaining). While the timer runs or is paused only the
  /// alerts change: switching the profile in the middle of a talk must not restart the clock.
  void configure(int minutes, QList<int> alertMinutes);
  void start();   ///< from the full time; also restarts a finished timer
  void pause();
  void resume();
  void reset();   ///< back to idle with the full time
  void tick();    ///< check the clock; called by the internal timer whenever the shown second changes

  State state() const { return state_; }
  int remainingSeconds() const;
  int totalSeconds() const { return minutes_ * 60; }
  static QString stateName(State s);

 signals:
  void stateChanged(State state);
  void remainingChanged(int seconds);   ///< once per second while running
  void alert(int minutesRemaining);
  void minuteElapsed(int minutes);      ///< another full minute of the talk has passed (not at the end: that is `finished`)
  void finished();

 private:
  void setState(State s);
  qint64 now() const;
  qint64 leftMs() const;   ///< while running
  void scheduleTick();

  Clock clock_;
  State state_ = State::Idle;
  int minutes_ = 30;
  QList<int> alerts_;       ///< minutes remaining, descending
  QList<int> alertsFired_;
  qint64 deadline_ = 0;     ///< while running
  qint64 remainingMs_ = 0;  ///< while idle / paused / finished
  int lastSecond_ = -1;
  int lastMinute_ = 0;      ///< the last elapsed minute that was announced
  QTimer ticker_;
};

}  // namespace projecteur
