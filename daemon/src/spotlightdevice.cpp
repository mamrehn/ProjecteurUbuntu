// SPDX-License-Identifier: MIT
#include "spotlightdevice.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDevice, "projecteur.device")

namespace projecteur {

using namespace hidpp;

namespace {
bool isErrorAnswer(const QByteArray& a) { return a.size() > 2 && static_cast<uint8_t>(a[2]) == 0xff; }
uint8_t indexIn(const QByteArray& a) { return (isErrorAnswer(a) || a.size() < 5) ? 0 : static_cast<uint8_t>(a[4]); }
}  // namespace

SpotlightDevice::SpotlightDevice(HidppLink* link, Config config, QObject* parent)
    : QObject(parent), link_(link), config_(config) {
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, &SpotlightDevice::start);
  batteryTimer_.setInterval(config_.batteryPollMs);
  connect(&batteryTimer_, &QTimer::timeout, this, &SpotlightDevice::refreshBattery);
  connect(link_, &HidppLink::notification, this, &SpotlightDevice::onNotification);
  connect(link_, &HidppLink::closed, this, [this] {
    retryTimer_.stop();
    batteryTimer_.stop();
    starting_ = false;
    setReady(false);
  });
}

void SpotlightDevice::setReady(bool ready) {
  if (ready_ == ready) return;
  ready_ = ready;
  emit readyChanged(ready_);
}

void SpotlightDevice::retryLater() {
  starting_ = false;
  setReady(false);
  retryTimer_.start(config_.retryMs);
}

void SpotlightDevice::ask(const QByteArray& request, std::function<void(const QByteArray&)> onAnswer) {
  link_->request(request, [this, onAnswer = std::move(onAnswer)](const QByteArray& a) {
    if (a.isEmpty()) return retryLater();  // asleep: it wakes on a button press or after another request
    onAnswer(a);
  }, config_.requestTimeoutMs);
}

SpotlightDevice::Step SpotlightDevice::lookupStep(Feature feature, uint8_t* slot) {
  return [this, feature, slot](Next next) {
    ask(getFeatureIndex(feature, config_.deviceIndex, config_.longMessagesOnly), [slot, next](const QByteArray& a) {
      *slot = indexIn(a);  // 0: the model does not have the feature
      next();
    });
  };
}

SpotlightDevice::Step SpotlightDevice::divertStep(uint16_t cid, std::function<uint8_t()> flags) {
  return [this, cid, flags = std::move(flags)](Next next) {
    ask(setCidReporting(reprogIndex_, cid, flags(), config_.deviceIndex), [this, cid, next](const QByteArray& a) {
      if (isErrorAnswer(a))
        emit problem(QStringLiteral("The remote did not accept the diversion of control %1.").arg(cid, 4, 16, QLatin1Char('0')));
      next();
    });
  };
}

void SpotlightDevice::start() {
  if (starting_ || !link_->isOpen()) return;
  starting_ = true;
  held_.clear();

  auto steps = std::make_shared<std::vector<Step>>();
  steps->push_back(lookupStep(Feature::ReprogramControlsV4, &reprogIndex_));
  steps->push_back([this](Next next) {
    if (reprogIndex_ != 0) return next();
    starting_ = false;
    emit problem(QStringLiteral("The remote has no ReprogramControlsV4 feature; button events are unavailable."));
  });
  steps->push_back(lookupStep(Feature::PresenterControl, &presenterIndex_));  // 0: this model cannot vibrate this way
  steps->push_back(lookupStep(Feature::BatteryStatus, &batteryIndex_));
  steps->push_back(lookupStep(Feature::FirmwareVersion, &firmwareIndex_));
  steps->push_back(lookupStep(Feature::PointerSpeed, &pointerSpeedIndex_));
  steps->push_back(divertStep(kCidHold, [this] { return config_.rawMovement ? kDivertWithRawXY : kDivert; }));
  steps->push_back(divertStep(kCidDoubleClick, [] { return kDivert; }));
  // Next / Back held: with the raw X/Y, because moving the remote while holding them drives volume and scrolling
  steps->push_back(divertStep(kCidNextHold, [] { return kDivertWithRawXY; }));
  steps->push_back(divertStep(kCidBackHold, [] { return kDivertWithRawXY; }));
  steps->push_back([this](Next next) {
    if (batteryIndex_ == 0) return next();
    ask(getBatteryStatus(batteryIndex_, config_.deviceIndex, config_.longMessagesOnly), [this, next](const QByteArray& a) {
      if (!isErrorAnswer(a))
        if (const auto s = parseBatteryStatus(a)) updateBattery(*s);
      next();
    });
  });
  steps->push_back([this](Next next) {
    if (firmwareIndex_ == 0) return next();
    ask(getFirmwareEntityCount(firmwareIndex_, config_.deviceIndex, config_.longMessagesOnly), [this, next](const QByteArray& a) {
      info_ = {};
      readFirmwareEntity(0, isErrorAnswer(a) ? 0 : qMin(parseFirmwareEntityCount(a), 4), next);
    });
  });
  run(steps, 0);
}

void SpotlightDevice::run(Steps steps, size_t i) {
  if (i >= steps->size()) {
    starting_ = false;
    batteryTimer_.start();
    setReady(true);
    return;
  }
  (*steps)[i]([this, steps, i] { run(steps, i + 1); });
}

void SpotlightDevice::readFirmwareEntity(int index, int count, Next next) {
  if (index >= count) {
    emit infoChanged();
    return next();
  }
  ask(getFirmwareInfo(firmwareIndex_, static_cast<uint8_t>(index), config_.deviceIndex, config_.longMessagesOnly),
      [this, index, count, next](const QByteArray& a) {
        if (!isErrorAnswer(a))
          if (const auto e = parseFirmwareInfo(a)) {
            if (e->type == 0) info_.firmware = e->version;
            if (e->type == 1) info_.bootloader = e->version;
          }
        readFirmwareEntity(index + 1, count, next);
      });
}

void SpotlightDevice::shutdown() {
  retryTimer_.stop();
  batteryTimer_.stop();
  if (reprogIndex_ == 0 || !link_->isOpen()) return;
  for (const uint16_t cid : {kCidHold, kCidDoubleClick, kCidNextHold, kCidBackHold})
    link_->send(setCidReporting(reprogIndex_, cid, kUndivert, config_.deviceIndex));
  if (originalPointerSpeed_ >= 0 && pointerSpeedIndex_ != 0)
    link_->send(hidpp::setPointerSpeed(pointerSpeedIndex_, static_cast<uint8_t>(originalPointerSpeed_), config_.deviceIndex));
  originalPointerSpeed_ = -1;
  setReady(false);
}

void SpotlightDevice::vibrate(uint8_t length, uint8_t intensity, std::function<void()> done) {
  if (presenterIndex_ == 0 || !link_->isOpen()) {
    if (done) done();
    return;
  }
  sendVibrate(hidpp::vibrate(presenterIndex_, length, intensity, config_.deviceIndex), qMax(config_.vibrateTries, 1), std::move(done));
}

void SpotlightDevice::sendVibrate(const QByteArray& message, int triesLeft, std::function<void()> done) {
  link_->request(message, [this, message, triesLeft, done = std::move(done)](const QByteArray& answer) mutable {
    if (answer.isEmpty() && triesLeft > 1 && link_->isOpen()) {
      qCDebug(lcDevice) << "no answer to a vibration request, sending it again";
      sendVibrate(message, triesLeft - 1, std::move(done));
      return;
    }
    if (done) done();
  }, config_.vibrateTimeoutMs);
}

void SpotlightDevice::setRawMovement(bool raw) {
  if (config_.rawMovement == raw) return;
  config_.rawMovement = raw;
  if (!ready_ || reprogIndex_ == 0) return;
  link_->request(setCidReporting(reprogIndex_, kCidHold, raw ? kDivertWithRawXY : kDivert, config_.deviceIndex), nullptr,
                 config_.requestTimeoutMs);
}

void SpotlightDevice::setPointerSpeed(uint8_t level) {
  if (!ready_ || pointerSpeedIndex_ == 0) return;
  level = qBound(kPointerSpeedMin, level, kPointerSpeedMax);
  if (originalPointerSpeed_ < 0) {
    // remember the level the remote had, so shutdown() can put it back
    originalPointerSpeed_ = kPointerSpeedDefault;
    link_->request(hidpp::getPointerSpeed(pointerSpeedIndex_, config_.deviceIndex), [this](const QByteArray& a) {
      if (!a.isEmpty() && !isErrorAnswer(a) && a.size() > 4) originalPointerSpeed_ = static_cast<uint8_t>(a[4]);
    }, config_.requestTimeoutMs);
  }
  link_->send(hidpp::setPointerSpeed(pointerSpeedIndex_, level, config_.deviceIndex));
}

void SpotlightDevice::refreshBattery() {
  if (batteryIndex_ == 0 || !link_->isOpen()) return;
  link_->request(getBatteryStatus(batteryIndex_, config_.deviceIndex, config_.longMessagesOnly), [this](const QByteArray& a) {
    if (a.isEmpty() || isErrorAnswer(a)) return;  // asleep: the remote announces the next step itself
    if (const auto s = parseBatteryStatus(a)) updateBattery(*s);
  }, config_.requestTimeoutMs);
}

void SpotlightDevice::updateBattery(const BatteryStatus& status) {
  if (status == battery_) return;
  battery_ = status;
  emit batteryChanged(status.percent, status.state);
}

void SpotlightDevice::onNotification(const QByteArray& message) {
  const auto event = decode(message, reprogIndex_, 0, batteryIndex_);
  if (!event) {
    qCDebug(lcDevice).noquote() << "unhandled HID++ message:" << toHex(message);
    return;
  }

  if (const auto* buttons = std::get_if<ButtonsChanged>(&*event)) {
    const QSet<uint16_t> now(buttons->pressed.begin(), buttons->pressed.end());
    const auto edge = [&](uint16_t cid, auto&& down, auto&& up) {
      if (!held_.contains(cid) && now.contains(cid)) down();
      if (held_.contains(cid) && !now.contains(cid)) up();
    };
    edge(kCidHold, [this] { emit holdDown(); }, [this] { emit holdUp(); });
    edge(kCidNextHold, [this] { emit sideHoldDown(Side::Next); }, [this] { emit sideHoldUp(Side::Next); });
    edge(kCidBackHold, [this] { emit sideHoldDown(Side::Back); }, [this] { emit sideHoldUp(Side::Back); });
    if (now.contains(kCidDoubleClick) && !held_.contains(kCidDoubleClick)) emit doubleClick();
    held_ = now;
  } else if (const auto* move = std::get_if<RawMove>(&*event)) {
    // the raw movement belongs to whichever control is held
    if (held_.contains(kCidHold)) emit rawMove(move->dx, move->dy);
    else if (held_.contains(kCidNextHold)) emit sideMove(Side::Next, move->dx, move->dy);
    else if (held_.contains(kCidBackHold)) emit sideMove(Side::Back, move->dx, move->dy);
  } else if (const auto* status = std::get_if<DeviceStatus>(&*event)) {
    if (status->awake && ready_) {  // be safe: apply the diversions again after a wake-up
      setReady(false);
      start();
    }
  } else if (const auto* battery = std::get_if<BatteryChanged>(&*event)) {
    updateBattery(battery->status);
  } else if (const auto* err = std::get_if<ErrorReply>(&*event)) {
    emit problem(QStringLiteral("The remote rejected a request (feature index %1, error %2).")
                     .arg(err->featureIndex).arg(err->code));
  }
}

}  // namespace projecteur
