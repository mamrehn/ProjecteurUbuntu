// SPDX-License-Identifier: MIT
#include "haptics.h"

#include <memory>

namespace projecteur {

Haptics::Haptics(Output output, int gapMs, int patternGapMs, QObject* parent)
    : QObject(parent), output_(std::move(output)), gapMs_(gapMs), patternGapMs_(patternGapMs) {
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] {
    playing_ = false;
    next();
  });
}

QList<uint8_t> Haptics::minutePattern(int elapsedMinutes) {
  if (elapsedMinutes < 1) return {};
  const int n = (elapsedMinutes - 1) % 10 + 1;   // 1 .. 10, then 11 is 1 again
  QList<uint8_t> out;
  for (int i = 0; i < n / 5; ++i) out.append(kLongPulse);
  for (int i = 0; i < n % 5; ++i) out.append(kShortPulse);
  return out;
}

void Haptics::enqueue(const QList<uint8_t>& lengths) {
  if (intensity_ == 0 || lengths.isEmpty()) return;
  for (int i = 0; i < lengths.size(); ++i) queue_.push_back({lengths[i], i == lengths.size() - 1});
  next();
}

void Haptics::pulses(int count) {
  enqueue(QList<uint8_t>(qMax(count, 0), kShortPulse));
}

void Haptics::connected() { enqueue({kConnectedPulse}); }

void Haptics::minute(int elapsedMinutes) { enqueue(minutePattern(elapsedMinutes)); }

void Haptics::next() {
  if (playing_ || queue_.empty()) return;
  playing_ = true;
  const Pulse p = queue_.front();
  queue_.pop_front();
  auto called = std::make_shared<bool>(false);
  const Done done = [this, called, p] {
    if (*called) return;
    *called = true;
    finished(p.length, p.lastOfPattern);
  };
  if (output_) output_(p.length, intensity_, done);
  else done();
}

void Haptics::finished(uint8_t length, bool lastOfPattern) {
  // a longer pulse needs more time before the next one; a finished pattern needs a clear break before the next pattern
  int pause = gapMs_ * (length > kShortPulse ? 2 : 1);
  if (lastOfPattern) pause = patternGapMs_;
  timer_.start(pause);   // also when nothing follows yet: a pattern queued a moment later must not run into this one
}

}  // namespace projecteur
