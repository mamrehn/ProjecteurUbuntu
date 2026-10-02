// SPDX-License-Identifier: MIT
#include "service.h"

#include <QJsonDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcService, "projecteur.service")

namespace projecteur {

Service::Service(PresentationTimer::Clock clock, QObject* parent) : QObject(parent), timer_(std::move(clock)) {
  timer_.configure(config_.timerMinutes, config_.timerAlerts);
  settingsGrace_.setSingleShot(true);
  settingsGrace_.setInterval(kSettingsGraceMs);
  connect(&settingsGrace_, &QTimer::timeout, this, [this] {
    qCInfo(lcService) << "no settings from the GNOME Shell extension yet: the remote uses the defaults";
    markSettingsKnown();
  });
  connect(&timer_, &PresentationTimer::remainingChanged, this, &Service::emitStatus);
  connect(&timer_, &PresentationTimer::stateChanged, this, &Service::emitStatus);
  connect(&timer_, &PresentationTimer::alert, this, [this](int minutes) {
    qCInfo(lcService) << "timer:" << minutes << "minutes remaining";
    if (config_.timerNotification && remote_) remote_->haptics()->pulses(Haptics::kTimerAlertPulses);
  });
  connect(&timer_, &PresentationTimer::minuteElapsed, this, [this](int minutes) {
    if (config_.timerMinutePulses && remote_) remote_->haptics()->minute(minutes);
  });
  connect(&timer_, &PresentationTimer::finished, this, [this] {
    qCInfo(lcService) << "timer: time is up";
    if (config_.timerNotification && remote_) remote_->haptics()->pulses(Haptics::kTimerEndPulses);
  });
}

void Service::setRemote(Remote* remote) {
  if (remote_ == remote) return;
  remote_ = remote;
  if (remote_) {
    remote_->setConfig(config_);
    connect(remote_, &Remote::statusChanged, this, &Service::emitStatus);
    connect(remote_, &Remote::slideChanged, this, &Service::onSlideChange);
    connect(remote_, &Remote::presentationStarted, this, &Service::onSlideChange);
    if (settingsKnown_) remote_->settingsKnown();
    else if (!settingsGrace_.isActive()) settingsGrace_.start();
  }
  emitStatus();
}

void Service::markSettingsKnown() {
  settingsGrace_.stop();
  settingsKnown_ = true;
  if (remote_) remote_->settingsKnown();
}

void Service::onSlideChange() {
  // the first slide change (or "Start presentation") starts the timer, unless it is started by hand
  if (config_.timerEnabled && config_.timerAutoStart && timer_.state() == PresentationTimer::State::Idle) timerStart();
}

QString Service::applyConfigJson(const QString& json) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    return QStringLiteral("not a JSON object: %1").arg(parseError.errorString());
  QStringList problems;
  setConfig(Config::fromJson(doc.object(), config_, &problems));
  markSettingsKnown();
  for (const QString& p : std::as_const(problems)) qCWarning(lcService).noquote() << "ignored setting:" << p;
  return problems.join(QLatin1Char('\n'));
}

void Service::setConfig(const Config& config) {
  if (config == config_) return;
  config_ = config;
  timer_.configure(config_.timerMinutes, config_.timerAlerts);
  if (remote_) remote_->setConfig(config_);
  emitStatus();
}

QString Service::configJson() const { return QString::fromUtf8(QJsonDocument(config_.toJson()).toJson(QJsonDocument::Compact)); }

QString Service::statusJson() const {
  QJsonObject o;
  o[QStringLiteral("version")] = QStringLiteral(PROJECTEURD_VERSION);
  const bool connected = remote_ && remote_->device()->isReady();
  o[QStringLiteral("connected")] = connected;
  o[QStringLiteral("connection")] = !remote_ ? QString() : remote_->isBluetooth() ? QStringLiteral("bluetooth") : QStringLiteral("usb");
  if (remote_) {
    const SpotlightDevice* d = remote_->device();
    if (d->hasBattery() && d->battery().percent >= 0) {
      QJsonObject b;
      b[QStringLiteral("percent")] = d->battery().percent;
      b[QStringLiteral("state")] = hidpp::batteryStateName(d->battery().state);
      b[QStringLiteral("charging")] = hidpp::isCharging(d->battery().state);
      o[QStringLiteral("battery")] = b;
    }
    o[QStringLiteral("firmware")] = d->info().firmware;
    o[QStringLiteral("bootloader")] = d->info().bootloader;
  }
  QJsonObject t;
  t[QStringLiteral("enabled")] = config_.timerEnabled;
  t[QStringLiteral("state")] = PresentationTimer::stateName(timer_.state());
  t[QStringLiteral("remaining")] = timer_.remainingSeconds();
  t[QStringLiteral("total")] = timer_.totalSeconds();
  o[QStringLiteral("timer")] = t;
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void Service::emitStatus() {
  const QString now = statusJson();
  if (now == lastStatus_) return;
  lastStatus_ = now;
  emit statusChanged(now);
}

void Service::timerStart() {
  timer_.start();
  qCInfo(lcService) << "timer started:" << config_.timerMinutes << "minutes";
}

void Service::timerTogglePause() {
  if (timer_.state() == PresentationTimer::State::Running) timer_.pause();
  else if (timer_.state() == PresentationTimer::State::Paused) timer_.resume();
}

void Service::timerReset() { timer_.reset(); }

void Service::vibrate(int pulses) {
  if (remote_) remote_->haptics()->pulses(qBound(1, pulses, 6));
}

void Service::vibrateMinute(int minute) {
  if (remote_) remote_->haptics()->minute(qBound(1, minute, 600));
}

}  // namespace projecteur
