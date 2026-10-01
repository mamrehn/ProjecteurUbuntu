// SPDX-License-Identifier: MIT
#include "spotlightdevice.h"

#include "hidpp.h"

namespace projecteur {

using namespace hidpp;

SpotlightDevice::SpotlightDevice(HidppLink* link, Config config, QObject* parent)
    : QObject(parent), link_(link), config_(config) {
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, &SpotlightDevice::start);
  connect(link_, &HidppLink::notification, this, &SpotlightDevice::onNotification);
  connect(link_, &HidppLink::closed, this, [this] {
    retryTimer_.stop();
    starting_ = false;
    setReady(false);
  });
}

void SpotlightDevice::setReady(bool ready) {
  if (ready_ == ready) return;
  ready_ = ready;
  emit readyChanged(ready_);
}

void SpotlightDevice::start() {
  if (starting_ || !link_->isOpen()) return;
  starting_ = true;
  held_.clear();
  lookupReprogramControls();
}

void SpotlightDevice::retryLater() {
  starting_ = false;
  setReady(false);
  retryTimer_.start(config_.retryMs);
}

void SpotlightDevice::lookupReprogramControls() {
  link_->request(getFeatureIndex(Feature::ReprogramControlsV4, config_.deviceIndex, config_.longMessagesOnly), [this](const QByteArray& a) {
    if (a.isEmpty()) return retryLater();  // asleep: it wakes on a button press or after another request
    reprogIndex_ = static_cast<uint8_t>(a.size() > 4 ? a[4] : 0);
    if (reprogIndex_ == 0) {
      starting_ = false;
      emit problem(QStringLiteral("The remote has no ReprogramControlsV4 feature; button events are unavailable."));
      return;
    }
    lookupPresenterControl();
  }, config_.requestTimeoutMs);
}

void SpotlightDevice::lookupPresenterControl() {
  link_->request(getFeatureIndex(Feature::PresenterControl, config_.deviceIndex, config_.longMessagesOnly), [this](const QByteArray& a) {
    if (a.isEmpty()) return retryLater();
    presenterIndex_ = static_cast<uint8_t>(a.size() > 4 ? a[4] : 0);  // 0: this model cannot vibrate this way
    const uint8_t holdFlags = config_.rawMovement ? kDivertWithRawXY : kDivert;
    divert(kCidHold, holdFlags, [this] {
      divert(kCidDoubleClick, kDivert, [this] {
        starting_ = false;
        setReady(true);
      });
    });
  }, config_.requestTimeoutMs);
}

void SpotlightDevice::divert(uint16_t cid, uint8_t flags, std::function<void()> next) {
  link_->request(setCidReporting(reprogIndex_, cid, flags, config_.deviceIndex), [this, next = std::move(next)](const QByteArray& a) {
    if (a.isEmpty()) return retryLater();
    next();
  }, config_.requestTimeoutMs);
}

void SpotlightDevice::shutdown() {
  retryTimer_.stop();
  if (reprogIndex_ == 0 || !link_->isOpen()) return;
  link_->send(setCidReporting(reprogIndex_, kCidHold, kUndivert, config_.deviceIndex));
  link_->send(setCidReporting(reprogIndex_, kCidDoubleClick, kUndivert, config_.deviceIndex));
  setReady(false);
}

void SpotlightDevice::vibrate(uint8_t length, uint8_t intensity) {
  if (presenterIndex_ == 0 || !link_->isOpen()) return;
  link_->send(hidpp::vibrate(presenterIndex_, length, intensity, config_.deviceIndex));
}

void SpotlightDevice::onNotification(const QByteArray& message) {
  const auto event = decode(message, reprogIndex_);
  if (!event) return;

  if (const auto* buttons = std::get_if<ButtonsChanged>(&*event)) {
    const QSet<uint16_t> now(buttons->pressed.begin(), buttons->pressed.end());
    const bool wasHold = held_.contains(kCidHold);
    const bool isHold = now.contains(kCidHold);
    if (!wasHold && isHold) emit holdDown();
    if (wasHold && !isHold) emit holdUp();
    if (now.contains(kCidDoubleClick) && !held_.contains(kCidDoubleClick)) emit doubleClick();
    held_ = now;
  } else if (const auto* move = std::get_if<RawMove>(&*event)) {
    emit rawMove(move->dx, move->dy);
  } else if (const auto* status = std::get_if<DeviceStatus>(&*event)) {
    if (status->awake && ready_) {  // be safe: apply the diversions again after a wake-up
      setReady(false);
      start();
    }
  } else if (const auto* err = std::get_if<ErrorReply>(&*event)) {
    emit problem(QStringLiteral("The remote rejected a request (feature index %1, error %2).")
                     .arg(err->featureIndex).arg(err->code));
  }
}

}  // namespace projecteur
