// SPDX-License-Identifier: MIT
#include "haptics.h"

namespace projecteur {

Haptics::Haptics(Output output, int gapMs, QObject* parent) : QObject(parent), output_(std::move(output)), gapMs_(gapMs) {
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, &Haptics::next);
}

void Haptics::pulses(int count) {
  if (intensity_ == 0 || count <= 0) return;
  for (int i = 0; i < count; ++i) queue_.push_back(kShortPulse);
  if (!timer_.isActive()) next();
}

void Haptics::connected() {
  if (intensity_ == 0) return;
  queue_.push_back(kConnectedPulse);
  if (!timer_.isActive()) next();
}

void Haptics::next() {
  if (queue_.empty()) return;
  const uint8_t length = queue_.front();
  queue_.pop_front();
  if (output_) output_(length, intensity_);
  // a longer pulse needs more time before the next one starts
  if (!queue_.empty()) timer_.start(gapMs_ * (length > kShortPulse ? 2 : 1));
}

}  // namespace projecteur
