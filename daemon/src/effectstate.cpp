// SPDX-License-Identifier: MIT
#include "effectstate.h"

namespace projecteur {

namespace {
Command simple(Command::Type t) { return Command{t}; }
}  // namespace

bool EffectState::anyModeEnabled() const {
  for (bool e : settings_.modeEnabled)
    if (e) return true;
  return false;
}

Mode EffectState::nextEnabledMode(Mode from) const {
  for (int step = 1; step <= kModeCount; ++step) {
    const int candidate = (static_cast<int>(from) + step) % kModeCount;
    if (settings_.modeEnabled[candidate]) return static_cast<Mode>(candidate);
  }
  return from;
}

void EffectState::setSettings(EffectSettings s) {
  settings_ = s;
  if (!anyModeEnabled()) return;
  if (!settings_.modeEnabled[static_cast<int>(mode_)]) mode_ = nextEnabledMode(mode_);
}

Commands EffectState::holdDown() {
  holding_ = true;
  if (!anyModeEnabled()) return {};
  if (visible_) return {};  // a frozen effect: movement simply continues from where it is
  if (!settings_.modeEnabled[static_cast<int>(mode_)]) mode_ = nextEnabledMode(mode_);
  visible_ = true;
  Command c = simple(Command::Type::ShowAtPointer);
  c.mode = mode_;
  return {c};
}

Commands EffectState::rawMove(int dxCounts, int dyCounts) {
  if (!holding_ || !visible_ || (dxCounts == 0 && dyCounts == 0)) return {};
  Command c = simple(Command::Type::MoveBy);
  c.dx = dxCounts * settings_.pixelsPerCount;
  c.dy = dyCounts * settings_.pixelsPerCount;
  return {c};
}

Commands EffectState::holdUp() {
  holding_ = false;
  if (settings_.freeze || !visible_) return {};
  visible_ = false;
  return {simple(Command::Type::Hide)};
}

Commands EffectState::shortClick() {
  if (!visible_) return {};
  visible_ = false;
  return {simple(Command::Type::Hide)};
}

Commands EffectState::doubleClick() {
  Commands out;
  if (visible_) {  // the Windows app shows nothing between a double click and the next hold
    visible_ = false;
    out.push_back(simple(Command::Type::Hide));
  }
  if (!anyModeEnabled()) return out;
  mode_ = nextEnabledMode(mode_);
  Command c = simple(Command::Type::SetMode);
  c.mode = mode_;
  out.push_back(c);
  return out;
}

Commands EffectState::key(RemoteKey k) {
  Command fwd = simple(Command::Type::ForwardKey);
  fwd.key = k;
  Commands out{fwd};
  if (settings_.recenter && visible_) out.push_back(simple(Command::Type::Recenter));
  return out;
}

}  // namespace projecteur
