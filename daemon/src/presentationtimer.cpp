// SPDX-License-Identifier: MIT
#include "presentationtimer.h"

#include <QElapsedTimer>
#include <algorithm>
#include <memory>

namespace projecteur {

PresentationTimer::PresentationTimer(Clock clock, QObject* parent) : QObject(parent), clock_(std::move(clock)) {
  if (!clock_) {
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    clock_ = [elapsed] { return elapsed->elapsed(); };
  }
  remainingMs_ = qint64(minutes_) * 60 * 1000;
  ticker_.setInterval(250);
  connect(&ticker_, &QTimer::timeout, this, &PresentationTimer::tick);
}

qint64 PresentationTimer::now() const { return clock_(); }

QString PresentationTimer::stateName(State s) {
  switch (s) {
    case State::Idle: return QStringLiteral("idle");
    case State::Running: return QStringLiteral("running");
    case State::Paused: return QStringLiteral("paused");
    case State::Finished: return QStringLiteral("finished");
  }
  return QStringLiteral("idle");
}

void PresentationTimer::setState(State s) {
  if (state_ == s) return;
  state_ = s;
  emit stateChanged(s);
}

void PresentationTimer::configure(int minutes, QList<int> alertMinutes) {
  alertMinutes.removeIf([](int m) { return m <= 0; });   // a slot that is off
  std::sort(alertMinutes.begin(), alertMinutes.end(), std::greater<int>());
  alerts_ = alertMinutes;
  if (state_ == State::Idle || state_ == State::Finished) {
    minutes_ = qMax(minutes, 1);
    if (state_ == State::Idle) {
      remainingMs_ = qint64(minutes_) * 60 * 1000;
      emit remainingChanged(remainingSeconds());
    }
  }
}

int PresentationTimer::remainingSeconds() const {
  const qint64 ms = state_ == State::Running ? qMax<qint64>(deadline_ - now(), 0) : remainingMs_;
  return static_cast<int>((ms + 999) / 1000);  // round up: "0:01" until it is really over
}

void PresentationTimer::start() {
  remainingMs_ = qint64(minutes_) * 60 * 1000;
  deadline_ = now() + remainingMs_;
  alertsFired_.clear();
  lastMinute_ = 0;
  for (int a : std::as_const(alerts_))   // an alert that is already past when the timer starts is not announced
    if (qint64(a) * 60 * 1000 >= remainingMs_) alertsFired_.append(a);
  lastSecond_ = -1;
  setState(State::Running);
  ticker_.start();
  tick();
}

void PresentationTimer::pause() {
  if (state_ != State::Running) return;
  remainingMs_ = qMax<qint64>(deadline_ - now(), 0);
  ticker_.stop();
  setState(State::Paused);
  emit remainingChanged(remainingSeconds());
}

void PresentationTimer::resume() {
  if (state_ != State::Paused) return;
  deadline_ = now() + remainingMs_;
  setState(State::Running);
  ticker_.start();
}

void PresentationTimer::reset() {
  ticker_.stop();
  remainingMs_ = qint64(minutes_) * 60 * 1000;
  alertsFired_.clear();
  lastMinute_ = 0;
  lastSecond_ = -1;
  setState(State::Idle);
  emit remainingChanged(remainingSeconds());
}

void PresentationTimer::tick() {
  if (state_ != State::Running) return;
  const qint64 left = qMax<qint64>(deadline_ - now(), 0);
  const int seconds = remainingSeconds();
  if (seconds != lastSecond_) {
    lastSecond_ = seconds;
    emit remainingChanged(seconds);
  }
  // another full minute has passed; after a long gap (the computer slept) only the current minute is announced
  const int elapsedMinutes = static_cast<int>((qint64(minutes_) * 60 * 1000 - left) / (60 * 1000));
  if (elapsedMinutes > lastMinute_) {
    lastMinute_ = elapsedMinutes;
    if (left > 0) emit minuteElapsed(elapsedMinutes);
  }
  for (int a : std::as_const(alerts_)) {
    if (!alertsFired_.contains(a) && left <= qint64(a) * 60 * 1000) {
      alertsFired_.append(a);
      emit alert(a);
    }
  }
  if (left == 0) {
    ticker_.stop();
    remainingMs_ = 0;
    setState(State::Finished);
    emit finished();
  }
}

}  // namespace projecteur
