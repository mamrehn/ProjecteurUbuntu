// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QTimer>
#include <cstdint>
#include <deque>
#include <functional>

namespace projecteur {

/// Vibration patterns of the remote. Policy (doc/ubuntu/INPUT-MODEL.md): a long vibration distracts a speaker, so
/// every alert is made of short pulses and alerts differ by their number; the one longer pulse is for "connected".
class Haptics : public QObject {
  Q_OBJECT
 public:
  static constexpr uint8_t kShortPulse = 1;      ///< `length` of a short pulse
  static constexpr uint8_t kConnectedPulse = 3;  ///< the one longer pulse
  /// The number of short pulses of each alert.
  static constexpr int kTimerAlertPulses = 2;
  static constexpr int kTimerEndPulses = 3;
  static constexpr int kBatteryLowPulses = 4;

  using Output = std::function<void(uint8_t length, uint8_t intensity)>;
  explicit Haptics(Output output, int gapMs = 450, QObject* parent = nullptr);

  void setIntensity(uint8_t intensity) { intensity_ = intensity; }  ///< 0 = silent
  void pulses(int count);   ///< `count` short pulses, `gapMs` apart; queued behind a pattern that is still playing
  void connected();         ///< the one longer pulse

 private:
  void next();

  Output output_;
  int gapMs_;
  uint8_t intensity_ = 0x80;
  std::deque<uint8_t> queue_;  ///< `length` of each pulse still to play
  QTimer timer_;
};

}  // namespace projecteur
