// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <deque>
#include <functional>

namespace projecteur {

/// Vibration patterns of the remote. Alerts are made of short pulses and differ by their number; the one longer pulse
/// (`kConnectedPulse`) says "connected", and the minute code (`minute()`) uses a medium pulse for "five".
///
/// A pulse is only counted as played when the remote has acknowledged it (the device layer retries a sleeping remote),
/// and the pause before the next pulse starts then, so a delayed first pulse does not squeeze the pattern together.
class Haptics : public QObject {
  Q_OBJECT
 public:
  static constexpr uint8_t kShortPulse = 1;      ///< `length` of a short pulse
  static constexpr uint8_t kLongPulse = 2;       ///< "five" in the minute code
  static constexpr uint8_t kConnectedPulse = 3;  ///< the one longer pulse: "connection established"
  static constexpr int kTimerAlertPulses = 2;
  static constexpr int kTimerEndPulses = 3;
  static constexpr int kBatteryLowPulses = 4;

  using Done = std::function<void()>;
  /// Vibrate once and call `done` when that is over (acknowledged or given up). Calling it more than once is harmless.
  using Output = std::function<void(uint8_t length, uint8_t intensity, Done done)>;
  Haptics(Output output, int gapMs = 450, int patternGapMs = 1200, QObject* parent = nullptr);

  void setIntensity(uint8_t intensity) { intensity_ = intensity; }  ///< 0 = silent
  void pulses(int count);   ///< `count` short pulses; queued behind a pattern that is still playing
  void connected();         ///< the one longer pulse
  /// The minute code for the `elapsedMinutes`-th minute of a talk: counts 1 to 10 and starts again, like a tally.
  /// 1 to 4 short pulses; 5 one long pulse; 6 to 9 a long pulse and 1 to 4 short ones; 10 two long pulses; 11 as 1.
  void minute(int elapsedMinutes);
  static QList<uint8_t> minutePattern(int elapsedMinutes);   ///< the `length` of each pulse

 private:
  struct Pulse {
    uint8_t length;
    bool lastOfPattern;
  };
  void enqueue(const QList<uint8_t>& lengths);
  void next();
  void finished(uint8_t length, bool lastOfPattern);

  Output output_;
  int gapMs_, patternGapMs_;
  uint8_t intensity_ = 0x80;
  std::deque<Pulse> queue_;
  bool playing_ = false;
  QTimer timer_;
};

}  // namespace projecteur
