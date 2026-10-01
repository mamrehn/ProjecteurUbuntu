// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <vector>

namespace projecteur {

/// The three pointer effects of the Windows app, in the order the double click cycles through them.
enum class Mode { Highlight = 0, Magnify = 1, Laser = 2 };
constexpr int kModeCount = 3;

/// Keys on the remote that the daemon forwards (evdev codes).
enum class RemoteKey { Next, Back };

/// What the daemon must do as a result of an input. The I/O layer turns these into D-Bus calls to the
/// GNOME Shell extension (org.projecteur.Overlay1) and into key events on the virtual keyboard.
struct Command {
  enum class Type {
    ShowAtPointer,  ///< show `mode` at the (unmoved) mouse pointer
    MoveBy,         ///< move the visible effect by (dx, dy) screen pixels
    Hide,
    SetMode,        ///< select `mode` for the next time the effect is shown
    Recenter,       ///< move the effect to the centre of its monitor
    ForwardKey,     ///< send `key` on the virtual keyboard
  };
  Type type;
  Mode mode = Mode::Highlight;
  double dx = 0, dy = 0;
  RemoteKey key = RemoteKey::Next;

  bool operator==(const Command&) const = default;
};
using Commands = std::vector<Command>;

struct EffectSettings {
  std::array<bool, kModeCount> modeEnabled{true, true, true};  ///< checkboxes of the Windows settings page
  bool freeze = true;           ///< "Effekte einfrieren": the effect stays where it is when the button is released
  bool recenter = true;         ///< "Zeiger-Effekte neu zentrieren": slide change moves a visible effect to the centre
  double pixelsPerCount = 1.0;  ///< gain from the remote's raw X/Y counts to screen pixels (to be calibrated)
};

/// The behaviour measured on the Windows app (doc/ubuntu/FEATURE-PARITY.md, section 4 and 4a):
///  - holding the action button shows the effect at the mouse pointer; movement of the remote moves it,
///  - releasing leaves it where it is (freeze); holding again moves it on from there,
///  - a short click hides it, a double click selects the next mode (Highlight -> Magnify -> Laser -> ...),
///  - Next / Back are passed on; with "recenter" they also move a visible effect to the centre.
class EffectState {
 public:
  explicit EffectState(EffectSettings settings = {}) : settings_(settings) {}

  Commands holdDown();
  Commands rawMove(int dxCounts, int dyCounts);
  Commands holdUp();
  Commands shortClick();
  Commands doubleClick();
  Commands key(RemoteKey key);

  Mode mode() const { return mode_; }
  bool visible() const { return visible_; }
  bool holding() const { return holding_; }
  const EffectSettings& settings() const { return settings_; }
  void setSettings(EffectSettings s);

 private:
  bool anyModeEnabled() const;
  Mode nextEnabledMode(Mode from) const;

  EffectSettings settings_;
  Mode mode_ = Mode::Highlight;  // the first mode the Windows app offers
  bool visible_ = false;
  bool holding_ = false;
};

}  // namespace projecteur
