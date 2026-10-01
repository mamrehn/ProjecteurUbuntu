// SPDX-License-Identifier: MIT
#include "dbusservice.h"

#include <QDBusError>

namespace projecteur {

DaemonAdaptor::DaemonAdaptor(Service* service) : QDBusAbstractAdaptor(service), service_(service) {
  setAutoRelaySignals(false);
  connect(service_, &Service::statusChanged, this, &DaemonAdaptor::StatusChanged);
}

QString DaemonAdaptor::SetConfig(const QString& json) { return service_->applyConfigJson(json); }
QString DaemonAdaptor::GetConfig() { return service_->configJson(); }
QString DaemonAdaptor::GetStatus() { return service_->statusJson(); }
void DaemonAdaptor::TimerStart() { service_->timerStart(); }
void DaemonAdaptor::TimerPause() { service_->timerTogglePause(); }
void DaemonAdaptor::TimerReset() { service_->timerReset(); }
void DaemonAdaptor::Vibrate(uint pulses) { service_->vibrate(static_cast<int>(qMin(pulses, 100u))); }
void DaemonAdaptor::VibrateMinute(uint minute) { service_->vibrateMinute(static_cast<int>(qMin(minute, 600u))); }

bool exportService(QDBusConnection bus, Service* service, QString* error) {
  new DaemonAdaptor(service);   // child of the service: exported with it
  if (!bus.registerObject(QStringLiteral("/org/projecteur/Daemon"), service, QDBusConnection::ExportAdaptors)) {
    if (error) *error = QStringLiteral("cannot export /org/projecteur/Daemon: %1").arg(bus.lastError().message());
    return false;
  }
  if (!bus.registerService(QStringLiteral("org.projecteur.Daemon"))) {
    if (error) *error = QStringLiteral("cannot own the bus name org.projecteur.Daemon (is another projecteurd running?): %1").arg(bus.lastError().message());
    return false;
  }
  return true;
}

}  // namespace projecteur
