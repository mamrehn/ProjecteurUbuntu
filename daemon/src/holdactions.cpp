// SPDX-License-Identifier: MIT
#include "holdactions.h"

#include <QLoggingCategory>
#include <linux/input.h>

Q_LOGGING_CATEGORY(lcActions, "projecteur.actions")

namespace projecteur {

HoldActions::HoldActions(KeySink* keys, PointerSink* pointer, QObject* parent) : QObject(parent), keys_(keys), pointer_(pointer) {
  repeat_.setInterval(kRepeatMs);
  connect(&repeat_, &QTimer::timeout, this, &HoldActions::stepRepeat);
}

void HoldActions::setConfig(const Config& config) { config_ = config; }

HoldAction HoldActions::actionFor(Side side) const { return side == Side::Next ? config_.holdNext : config_.holdBack; }
const QList<int>& HoldActions::shortcutFor(Side side) const { return side == Side::Next ? config_.shortcutNext : config_.shortcutBack; }

void HoldActions::down(Side side) {
  if (held_) return;  // one hold at a time
  held_ = true;
  side_ = side;
  accumulated_ = 0;
  const HoldAction action = actionFor(side);
  qCDebug(lcActions) << (side == Side::Next ? "Next" : "Back") << "held:" << holdActionName(action);
  if (!keys_) return;
  switch (action) {
    case HoldAction::None: break;
    case HoldAction::StartPresentation:
      keys_->tap(KEY_F5);
      emit presentationStarted();
      break;
    case HoldAction::BlankScreen: keys_->tap(KEY_B); break;
    case HoldAction::FastForward:
    case HoldAction::FastRewind:
      stepRepeat();
      repeat_.start();
      break;
    case HoldAction::Volume:
    case HoldAction::Scroll: break;  // follow the movement
    case HoldAction::Shortcut:
      if (!shortcutFor(side).isEmpty()) keys_->chord(shortcutFor(side));
      break;
  }
}

void HoldActions::stepRepeat() {
  if (!keys_ || !held_) return;
  keys_->tap(actionFor(side_) == HoldAction::FastRewind ? KEY_LEFT : KEY_RIGHT);
}

void HoldActions::move(Side side, int /*dx*/, int dy) {
  if (!held_ || side != side_) return;
  const HoldAction action = actionFor(side);
  if (action != HoldAction::Volume && action != HoldAction::Scroll) return;
  // turning the remote up gives negative dy (measured); volume up and scrolling up are both "up"
  accumulated_ += -dy;
  const int unit = action == HoldAction::Volume ? kCountsPerVolumeStep : kCountsPerScrollNotch;
  const int steps = accumulated_ / unit;  // truncates toward zero
  if (steps == 0) return;
  accumulated_ -= steps * unit;
  if (action == HoldAction::Volume) {
    if (!keys_) return;
    for (int i = 0; i < qAbs(steps); ++i) keys_->tap(steps > 0 ? KEY_VOLUMEUP : KEY_VOLUMEDOWN);
  } else if (pointer_) {
    pointer_->scroll(steps);
  }
}

void HoldActions::up(Side side) {
  if (!held_ || side != side_) return;
  held_ = false;
  repeat_.stop();
  accumulated_ = 0;
}

}  // namespace projecteur
