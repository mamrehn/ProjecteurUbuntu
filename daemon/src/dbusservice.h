// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QString>

#include "service.h"

namespace projecteur {

/// org.projecteur.Daemon1 at /org/projecteur/Daemon: what the GNOME Shell extension and its settings window use to
/// talk to the daemon. All data travels as JSON strings (the names are those of the extension's GSettings keys).
class DaemonAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.projecteur.Daemon1")
 public:
  explicit DaemonAdaptor(Service* service);

 public slots:
  /// Apply settings (a JSON object; any subset of the keys). Returns the problems found, empty if there are none.
  QString SetConfig(const QString& json);
  QString GetConfig();
  /// {connected, connection, battery{percent,state,charging}, firmware, bootloader, timer{enabled,state,remaining,total}}
  QString GetStatus();
  void TimerStart();
  void TimerPause();  ///< pauses a running timer, resumes a paused one
  void TimerReset();
  void Vibrate(uint pulses);

 signals:
  void StatusChanged(const QString& json);

 private:
  Service* service_;
};

/// Export the service on `bus` as org.projecteur.Daemon. Fails (with a message) if another daemon owns the name.
bool exportService(QDBusConnection bus, Service* service, QString* error);

}  // namespace projecteur
