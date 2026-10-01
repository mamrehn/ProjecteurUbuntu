// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>

#include "effectstate.h"
#include "overlayclient.h"
#include "spotlightdevice.h"

namespace projecteur {

class EvdevDevice;
class HidppLink;
class KeySink;

/// One connected Spotlight: its HID++ link, its two input nodes and the effect state machine, wired to the
/// overlay (the GNOME Shell extension) and to the virtual keyboard.
class Remote : public QObject {
  Q_OBJECT
 public:
  struct Fds {  ///< open descriptors; the Remote takes ownership
    int hidraw = -1;
    int keyboard = -1;
    int mouse = -1;
  };
  struct Options {
    EffectSettings effect;
    SpotlightDevice::Config device;
    bool grab = true;                  ///< capture the keyboard and mouse nodes exclusively
    uint8_t pulseIntensity = 0x80;     ///< vibration strength byte (percent x 2.55)
    uint8_t connectedPulseLength = 3;  ///< the one longer pulse: "connection established"
  };

  Remote(Fds fds, KeySink* keys, OverlaySink* overlay, Options options, QObject* parent = nullptr);
  ~Remote() override;

  const EffectState& state() const { return state_; }
  SpotlightDevice* device() { return device_; }

 signals:
  void gone();  ///< the receiver was unplugged

 private:
  void run(const Commands& commands);
  void onKeyboardKey(int code, int value);
  void onMouseKey(int code, int value);

  EffectState state_;
  Options options_;
  KeySink* keys_;
  OverlaySink* overlay_;
  HidppLink* link_ = nullptr;
  SpotlightDevice* device_ = nullptr;
  EvdevDevice* keyboard_ = nullptr;
  EvdevDevice* mouse_ = nullptr;
  bool announcedConnection_ = false;
  bool goneEmitted_ = false;
};

}  // namespace projecteur
