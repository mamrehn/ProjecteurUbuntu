// SPDX-License-Identifier: MIT
#include "remote.h"

#include <QLoggingCategory>
#include <linux/input.h>

#include "evdevdevice.h"
#include "hidpplink.h"
#include "keysink.h"

Q_LOGGING_CATEGORY(lcRemote, "projecteur.remote")

namespace projecteur {

Remote::Remote(Fds fds, KeySink* keys, OverlaySink* overlay, Options options, QObject* parent)
    : QObject(parent), state_(options.effect), options_(options), keys_(keys), overlay_(overlay) {
  link_ = new HidppLink(fds.hidraw, this);
  device_ = new SpotlightDevice(link_, options_.device, this);
  keyboard_ = new EvdevDevice(fds.keyboard, this);
  mouse_ = new EvdevDevice(fds.mouse, this);

  if (options_.grab) {
    // The Next / Back keys are re-sent through the virtual keyboard, a short press of the action button is a
    // plain mouse click that must not reach the application under the pointer.
    if (!keyboard_->grab(true)) qCWarning(lcRemote) << "cannot grab the remote's keyboard node; Next/Back reach the desktop directly";
    if (!mouse_->grab(true)) qCWarning(lcRemote) << "cannot grab the remote's mouse node; a short click would click in the application";
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
    run(state_.rawMove(dx, dy));
  });
  connect(device_, &SpotlightDevice::doubleClick, this, [this] {
    qCDebug(lcRemote) << "double click";
    run(state_.doubleClick());
  });
  connect(device_, &SpotlightDevice::problem, this, [](const QString& m) { qCWarning(lcRemote).noquote() << m; });
  connect(device_, &SpotlightDevice::readyChanged, this, [this](bool ready) {
    qCInfo(lcRemote) << (ready ? "remote ready" : "remote not ready");
    if (ready && !announcedConnection_) {  // once per connection, not after every wake-up
      announcedConnection_ = true;
      device_->vibrate(options_.connectedPulseLength, options_.pulseIntensity);
    }
  });

  connect(keyboard_, &EvdevDevice::key, this, &Remote::onKeyboardKey);
  connect(mouse_, &EvdevDevice::key, this, &Remote::onMouseKey);
  // movement on the mouse node is swallowed: with the raw X/Y diverted the pointer must stay where it is

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

void Remote::run(const Commands& commands) {
  Commands forOverlay;
  for (const Command& c : commands) {
    if (c.type == Command::Type::ForwardKey) {
      if (keys_) keys_->tap(c.key == RemoteKey::Next ? KEY_RIGHT : KEY_LEFT);
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
