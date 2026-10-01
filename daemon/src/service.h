// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include "config.h"
#include "presentationtimer.h"
#include "remote.h"

namespace projecteur {

/// The daemon's brain above one remote: holds the settings pushed by the extension, applies them to the connected
/// remote, runs the presentation timer and turns its alerts into vibration, and describes the whole state as JSON
/// for the D-Bus interface (dbusservice.h).
class Service : public QObject {
  Q_OBJECT
 public:
  explicit Service(PresentationTimer::Clock clock = {}, QObject* parent = nullptr);

  /// The remote that appeared (nullptr: it is gone). The current settings are applied to it at once.
  void setRemote(Remote* remote);
  Remote* remote() const { return remote_; }

  const Config& config() const { return config_; }
  /// Apply settings given as a JSON object. Returns the problems found, one per line; empty means all fine.
  QString applyConfigJson(const QString& json);
  void setConfig(const Config& config);
  QString configJson() const;
  QString statusJson() const;

  PresentationTimer* timer() { return &timer_; }
  void timerStart();
  void timerTogglePause();   ///< pause a running timer, resume a paused one
  void timerReset();
  void vibrate(int pulses);  ///< for the "test vibration" button in the settings

 signals:
  void statusChanged(const QString& statusJson);

 private:
  void onSlideChange();
  void emitStatus();

  Config config_;
  PresentationTimer timer_;
  QPointer<Remote> remote_;
  QString lastStatus_;
};

}  // namespace projecteur
