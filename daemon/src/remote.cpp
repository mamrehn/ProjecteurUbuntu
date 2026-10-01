// SPDX-License-Identifier: MIT
#include "remote.h"

#include <QLoggingCategory>
#include <linux/input.h>

#include "evdevdevice.h"
#include "hidpplink.h"
#include "keysink.h"

Q_LOGGING_CATEGORY(lcRemote, "projecteur.remote")
// Every raw movement report (about 100 a second). Off even with --verbose; for calibration runs set
// QT_LOGGING_RULES=projecteur.raw.debug=true
Q_LOGGING_CATEGORY(lcRaw, "projecteur.raw", QtInfoMsg)

namespace projecteur {

Remote::Remote(Fds fds, KeySink* keys, PointerSink* pointer, OverlaySink* overlay, Options options, QObject* parent)
    : QObject(parent), config_(options.config), state_(options.config.effectSettings()), options_(options), keys_(keys), overlay_(overlay) {
  options_.device.rawMovement = !config_.cursorControl;
  link_ = new HidppLink(fds.hidraw, this);
  device_ = new SpotlightDevice(link_, options_.device, this);
  keyboard_ = new EvdevDevice(fds.keyboard, this);
  mouse_ = new EvdevDevice(fds.mouse, this);
  actions_ = new HoldActions(keys, pointer, this);
  haptics_ = new Haptics([this](uint8_t length, uint8_t intensity, Haptics::Done done) { device_->vibrate(length, intensity, std::move(done)); },
                         options_.pulseGapMs, options_.patternGapMs, this);
  actions_->setConfig(config_);
  haptics_->setIntensity(config_.vibrationIntensity());

  if (options_.grab) {
    // The Next / Back keys are re-sent through the virtual keyboard, a short press of the action button is a
    // plain mouse click that must not reach the application under the pointer.
    if (!keyboard_->grab(true)) qCWarning(lcRemote) << "cannot grab the remote's keyboard node; Next/Back reach the desktop directly";
    if (!mouse_->grab(!config_.cursorControl)) qCWarning(lcRemote) << "cannot grab the remote's mouse node; a short click would click in the application";
  }

  connect(device_, &SpotlightDevice::holdDown, this, [this] {
    moveCount_ = 0; sumDx_ = sumDy_ = 0;
    qCDebug(lcRemote) << "action button held";
    run(state_.holdDown());
  });
  connect(device_, &SpotlightDevice::holdUp, this, [this] {
    qCDebug(lcRemote) << "action button released after" << moveCount_ << "movement reports, sum dx =" << sumDx_ << "dy =" << sumDy_;
    run(state_.holdUp());
  });
  connect(device_, &SpotlightDevice::rawMove, this, [this](int dx, int dy) {
    ++moveCount_; sumDx_ += dx; sumDy_ += dy;
    qCDebug(lcRaw) << "raw" << dx << dy;
    run(state_.rawMove(dx, dy));
  });
  connect(device_, &SpotlightDevice::doubleClick, this, [this] {
    qCDebug(lcRemote) << "double click";
    run(state_.doubleClick());
  });
  connect(device_, &SpotlightDevice::sideHoldDown, this, [this](Side s) { qCDebug(lcRemote) << (s == Side::Next ? "Next" : "Back") << "held"; actions_->down(s); });
  connect(device_, &SpotlightDevice::sideHoldUp, this, [this](Side s) { qCDebug(lcRemote) << (s == Side::Next ? "Next" : "Back") << "released"; actions_->up(s); });
  connect(device_, &SpotlightDevice::sideMove, this, [this](Side s, int dx, int dy) { qCDebug(lcRaw) << "side raw" << dx << dy; actions_->move(s, dx, dy); });
  connect(actions_, &HoldActions::presentationStarted, this, &Remote::presentationStarted);
  connect(device_, &SpotlightDevice::problem, this, [](const QString& m) { qCWarning(lcRemote).noquote() << m; });
  connect(device_, &SpotlightDevice::batteryChanged, this, &Remote::onBattery);
  connect(device_, &SpotlightDevice::infoChanged, this, &Remote::statusChanged);
  connect(device_, &SpotlightDevice::readyChanged, this, [this](bool ready) {
    qCInfo(lcRemote) << (ready ? "remote ready" : "remote not ready");
    if (ready) {
      applyDeviceConfig();
      if (!announcedConnection_) {  // once per connection, not after every wake-up
        announcedConnection_ = true;
        haptics_->connected();
      }
    }
    emit statusChanged();
  });

  connect(keyboard_, &EvdevDevice::key, this, &Remote::onKeyboardKey);
  connect(mouse_, &EvdevDevice::key, this, &Remote::onMouseKey);
  // movement on the mouse node is swallowed while it is grabbed: with the raw X/Y diverted the pointer must stay put

  const auto vanished = [this] {
    if (goneEmitted_) return;
    goneEmitted_ = true;
    emit gone();
  };
  connect(link_, &HidppLink::closed, this, vanished);
  connect(keyboard_, &EvdevDevice::closed, this, vanished);
  connect(mouse_, &EvdevDevice::closed, this, vanished);

  device_->start();
}

Remote::~Remote() {
  if (device_) device_->shutdown();  // give the buttons back to the system
}

void Remote::setConfig(const Config& config) {
  const bool cursorChanged = config.cursorControl != config_.cursorControl;
  config_ = config;
  Commands hide;
  if (cursorChanged && state_.visible()) hide = state_.shortClick();   // an effect must not stay behind
  state_.setSettings(config_.effectSettings());
  actions_->setConfig(config_);
  haptics_->setIntensity(config_.vibrationIntensity());
  if (cursorChanged && options_.grab) mouse_->grab(!config_.cursorControl);
  applyDeviceConfig();
  run(hide);
}

void Remote::applyDeviceConfig() {
  device_->setRawMovement(!config_.cursorControl);
  if (config_.cursorControl) device_->setPointerSpeed(config_.pointerSpeedLevel());
}

void Remote::onBattery(int percent, hidpp::BatteryState state) {
  qCInfo(lcRemote).noquote() << QStringLiteral("battery %1 % (%2)").arg(percent).arg(hidpp::batteryStateName(state));
  const bool low = percent <= kBatteryLowPercent && !hidpp::isCharging(state);
  if (low && !warnedLowBattery_ && config_.batteryWarning) {
    warnedLowBattery_ = true;
    haptics_->pulses(Haptics::kBatteryLowPulses);
  } else if (!low && (percent > kBatteryLowPercent + 5 || hidpp::isCharging(state))) {
    warnedLowBattery_ = false;   // recovered: warn again the next time
  }
  emit statusChanged();
}

void Remote::run(const Commands& commands) {
  Commands forOverlay;
  for (const Command& c : commands) {
    if (c.type == Command::Type::ForwardKey) {
      if (keys_) keys_->tap(c.key == RemoteKey::Next ? KEY_RIGHT : KEY_LEFT);
      emit slideChanged();
    } else {
      forOverlay.push_back(c);
    }
  }
  if (!forOverlay.empty() && overlay_) overlay_->apply(forOverlay);
}

void Remote::onKeyboardKey(int code, int value) {
  qCDebug(lcRemote) << "keyboard node: key" << code << (value == 1 ? "down" : value == 0 ? "up" : "repeat");
  if (value != 1) return;  // act on presses; the remote's keys are tapped
  if (code == KEY_RIGHT) run(state_.key(RemoteKey::Next));
  else if (code == KEY_LEFT) run(state_.key(RemoteKey::Back));
  else if (keys_) keys_->tap(code);  // anything else is passed on unchanged
}

void Remote::onMouseKey(int code, int value) {
  qCDebug(lcRemote) << "mouse node: button" << code << (value == 1 ? "down" : "up");
  if (code == BTN_LEFT && value == 1) run(state_.shortClick());
}

}  // namespace projecteur
