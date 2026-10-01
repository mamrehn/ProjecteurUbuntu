// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

#include "config.h"
#include "keysink.h"
#include "spotlightdevice.h"

namespace projecteur {

/// What holding Next or Back does. One-shot actions (start the presentation, blank the screen, a shortcut) happen
/// when the hold is recognised; fast forward / rewind repeat while it lasts; volume and scrolling follow the
/// remote's up and down movement while it is held.
class HoldActions : public QObject {
  Q_OBJECT
 public:
  /// Counts of the remote per step (49.5 counts per degree: see doc/ubuntu/INPUT-MODEL.md).
  static constexpr int kCountsPerVolumeStep = 150;  ///< about 3 degrees
  static constexpr int kCountsPerScrollNotch = 60;  ///< about 1.2 degrees
  static constexpr int kRepeatMs = 250;             ///< fast forward / rewind: four slides a second

  HoldActions(KeySink* keys, PointerSink* pointer, QObject* parent = nullptr);

  void setConfig(const Config& config);
  void down(Side side);
  void move(Side side, int dx, int dy);
  void up(Side side);
  void setRepeatMs(int ms) { repeat_.setInterval(ms); }

 signals:
  void presentationStarted();

 private:
  HoldAction actionFor(Side side) const;
  const QList<int>& shortcutFor(Side side) const;
  void stepRepeat();

  KeySink* keys_;
  PointerSink* pointer_;
  Config config_;
  bool held_ = false;
  Side side_ = Side::Next;
  int accumulated_ = 0;
  QTimer repeat_;
};

}  // namespace projecteur
