// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <optional>

#include "effectstate.h"

namespace projecteur {

/// What holding Next or Back does (the choices of the Windows app that make sense on Linux).
enum class HoldAction {
  None,
  StartPresentation,  ///< F5
  BlankScreen,        ///< B (black screen, toggles)
  FastForward,        ///< Next, repeated while held
  FastRewind,         ///< Back, repeated while held
  Volume,             ///< turning the remote up / down changes the volume
  Scroll,             ///< turning the remote up / down scrolls
  Shortcut,           ///< a key combination of the user's choice
};
QString holdActionName(HoldAction action);  ///< "none", "start-presentation", "blank-screen", ...
std::optional<HoldAction> holdActionFromName(const QString& name);

/// The settings the daemon acts on. The JSON keys are the names of the extension's GSettings keys, so that the
/// extension can hand its settings over unchanged (doc/ubuntu/PLAN.md, "Where settings live"). Appearance
/// (sizes, contrast, colours) is not here: only the overlay needs it.
struct Config {
  std::array<bool, kModeCount> modes{true, true, true};  ///< highlight, magnify, laser enabled
  bool freeze = true;
  bool recenter = true;
  bool cursorControl = false;
  int pointerSpeed = 35;  ///< percent; 35 % is the reference setting of the Windows app
  HoldAction holdNext = HoldAction::StartPresentation;
  HoldAction holdBack = HoldAction::BlankScreen;
  QList<int> shortcutNext;  ///< evdev key codes, modifiers first; used when the action is Shortcut
  QList<int> shortcutBack;
  int vibrationPercent = 50;  ///< 0 turns every vibration off
  bool batteryWarning = true;
  bool timerNotification = true;
  bool timerEnabled = false;
  int timerMinutes = 30;
  bool timerAutoStart = true;     ///< start with the first slide change (or "Start presentation") instead of by hand
  QList<int> timerAlerts{5};      ///< minutes remaining at which the remote vibrates: up to three slots, 0 = off

  /// 35 % pointer speed is 1.0 screen pixel per count of the remote (doc/ubuntu/INPUT-MODEL.md); linear from there.
  double pixelsPerCount() const;
  EffectSettings effectSettings() const;  ///< no effects while the remote controls the cursor itself
  uint8_t vibrationIntensity() const;     ///< percent x 2.55
  uint8_t pointerSpeedLevel() const;      ///< the remote's own scale, 0x10 .. 0x19, for cursor control

  QJsonObject toJson() const;
  /// Apply the keys present in `json` on top of `base`. Invalid values are reported in `problems` (when given) and
  /// ignored. Keys the daemon does not know are ignored silently: the extension sends all of its settings, and a
  /// newer extension must not break an older daemon.
  static Config fromJson(const QJsonObject& json, const Config& base, QStringList* problems = nullptr);

  bool operator==(const Config&) const = default;
};

}  // namespace projecteur
