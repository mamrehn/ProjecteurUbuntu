// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>

#include "config.h"
#include "effectstate.h"
#include "haptics.h"
#include "holdactions.h"
#include "overlayclient.h"
#include "spotlightdevice.h"

namespace projecteur {

class EvdevDevice;
class HidppLink;
class KeySink;

/// One connected Spotlight: its HID++ link, its two input nodes, the effect state machine and the hold actions,
/// wired to the overlay (the GNOME Shell extension), to the virtual keyboard and pointer, and to the remote's motor.
class Remote : public QObject {
  Q_OBJECT
 public:
  struct Fds {  ///< open descriptors; the Remote takes ownership
    int hidraw = -1;
    int keyboard = -1;
    int mouse = -1;
  };
  struct Options {
    Config config;
    SpotlightDevice::Config device;
    bool grab = true;         ///< capture the keyboard and mouse nodes exclusively
    /// Send the remote's keys on through the virtual keyboard although its keyboard node is not grabbed. Only for
    /// descriptors that nobody else reads (tests, --test-fds): on a real node the desktop would get every key twice.
    bool forwardUngrabbed = false;
    bool bluetooth = false;   ///< connected directly instead of through the USB receiver
    /// Hold the connection pulse and the low-battery warning back until settingsKnown(): the daemon starts with the
    /// defaults, and the user's settings (vibration strength, battery warning) arrive from the extension a moment later.
    bool waitForSettings = false;
    int pulseGapMs = 450;     ///< pause between the pulses of a vibration pattern
    int patternGapMs = 1200;  ///< pause between two patterns
  };
  /// At or below this charge the remote warns once (it announces its level in steps, so this must not be lower
  /// than the lowest step; doc/ubuntu/INPUT-MODEL.md).
  static constexpr int kBatteryLowPercent = 20;

  Remote(Fds fds, KeySink* keys, PointerSink* pointer, OverlaySink* overlay, Options options, QObject* parent = nullptr);
  ~Remote() override;

  void setConfig(const Config& config);  ///< applied at once, also while a hold is in progress
  /// The settings in force are the user's (or none are coming): announcements held back by waitForSettings happen now.
  void settingsKnown();
  const EffectState& state() const { return state_; }
  SpotlightDevice* device() { return device_; }
  Haptics* haptics() { return haptics_; }
  bool isBluetooth() const { return options_.bluetooth; }

 signals:
  void gone();                 ///< the receiver was unplugged (always delivered later, never from inside a call)
  void presentationStarted();  ///< hold Next/Back started the presentation
  void slideChanged();         ///< Next or Back was pressed
  void statusChanged();        ///< ready, battery or firmware information changed

 private:
  void run(const Commands& commands);
  void applyDeviceConfig();
  void onBattery(int percent, hidpp::BatteryState state);
  void announce();       ///< the connection pulse (once) and the battery check, as soon as both remote and settings are ready
  void checkBattery();
  void onKeyboardKey(int code, int value);
  void onMouseKey(int code, int value);

  Config config_;
  EffectState state_;
  Options options_;
  KeySink* keys_;
  OverlaySink* overlay_;
  HidppLink* link_ = nullptr;
  SpotlightDevice* device_ = nullptr;
  EvdevDevice* keyboard_ = nullptr;
  EvdevDevice* mouse_ = nullptr;
  HoldActions* actions_ = nullptr;
  Haptics* haptics_ = nullptr;
  bool forwardKeys_ = false;   ///< the keyboard node is ours alone: Next / Back must be sent on by us
  bool settingsKnown_ = true;
  bool announcedConnection_ = false;
  bool warnedLowBattery_ = false;
  int moveCount_ = 0;
  long sumDx_ = 0, sumDy_ = 0;
  bool goneEmitted_ = false;
};

}  // namespace projecteur
