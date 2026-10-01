// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "hidpp.h"
#include "hidpplink.h"

namespace projecteur {

/// The Next and Back keys of the remote, when they are held.
enum class Side { Next, Back };

/// The Spotlight behind a HID++ link: finds the features, diverts the action button and the held Next / Back keys,
/// reads battery and firmware, turns the notifications into signals. A sleeping remote ignores requests, so the
/// handshake simply asks again until it answers.
class SpotlightDevice : public QObject {
  Q_OBJECT
 public:
  struct Config {
    /// Also divert the raw X/Y movement of the action button. The system pointer then stays still while the effect
    /// moves (Windows "cursor control off"). When false the movement arrives as ordinary pointer motion.
    bool rawMovement = true;
    int retryMs = 1000;           ///< pause before asking a silent remote again
    int requestTimeoutMs = 1000;  ///< how long to wait for an answer before treating the remote as asleep
    int batteryPollMs = 10 * 60 * 1000;  ///< the remote also announces level steps itself; this is the safety net
    uint8_t deviceIndex = 0x01;   ///< HID++ device index: 1 behind the USB receiver, 0xff when connected directly (Bluetooth)
    bool longMessagesOnly = false;  ///< Bluetooth: the hidraw node accepts only 20 byte reports
    int vibrateTimeoutMs = 600;     ///< how long to wait for the remote to acknowledge a pulse (it may be asleep)
    int vibrateTries = 4;           ///< how often a pulse is sent before it is given up
  };

  /// What the remote tells about itself (shown in the settings).
  struct Info {
    QString firmware;    ///< main firmware, for example "1.1.32"
    QString bootloader;  ///< for example "26.1.15"
  };

  SpotlightDevice(HidppLink* link, Config config, QObject* parent = nullptr);

  void start();     ///< (re)run the handshake
  void shutdown();  ///< undivert the controls and restore the pointer speed (best effort, nothing waits for the answers)
  /// Vibrate. length 0 is not felt on the original Spotlight; use 1 for a short pulse. The pulse is sent again when the
  /// remote does not answer (a sleeping remote ignores the first request). `done` is called once it was acknowledged or
  /// given up, or at once when there is no remote to vibrate.
  void vibrate(uint8_t length, uint8_t intensity, std::function<void()> done = {});
  /// Divert the action button's movement as raw X/Y (effects) or leave it to the system pointer (cursor control).
  void setRawMovement(bool raw);
  /// Pointer speed of the movement the remote sends when it controls the cursor. The original level is restored by
  /// shutdown().
  void setPointerSpeed(uint8_t level);
  void refreshBattery();

  bool isReady() const { return ready_; }
  bool canVibrate() const { return presenterIndex_ != 0; }
  bool hasBattery() const { return batteryIndex_ != 0; }
  hidpp::BatteryStatus battery() const { return battery_; }
  const Info& info() const { return info_; }

 signals:
  void readyChanged(bool ready);
  void holdDown();   ///< the action button is held
  void holdUp();
  void rawMove(int dx, int dy);  ///< movement while the action button is held
  void doubleClick();
  void sideHoldDown(Side side);  ///< Next or Back is held (not tapped)
  void sideHoldUp(Side side);
  void sideMove(Side side, int dx, int dy);  ///< movement while Next or Back is held
  void batteryChanged(int percent, hidpp::BatteryState state);
  void infoChanged();
  void problem(const QString& message);

 private:
  using Next = std::function<void()>;
  using Step = std::function<void(Next)>;
  using Steps = std::shared_ptr<std::vector<Step>>;

  void run(Steps steps, size_t i);
  void ask(const QByteArray& request, std::function<void(const QByteArray&)> onAnswer);
  Step lookupStep(hidpp::Feature feature, uint8_t* slot);
  Step divertStep(uint16_t cid, std::function<uint8_t()> flags);
  void readFirmwareEntity(int index, int count, Next next);
  void sendVibrate(const QByteArray& message, int triesLeft, std::function<void()> done);
  void retryLater();
  void setReady(bool ready);
  void updateBattery(const hidpp::BatteryStatus& status);
  void onNotification(const QByteArray& message);

  HidppLink* link_;
  Config config_;
  uint8_t reprogIndex_ = 0;
  uint8_t presenterIndex_ = 0;
  uint8_t batteryIndex_ = 0;
  uint8_t firmwareIndex_ = 0;
  uint8_t pointerSpeedIndex_ = 0;
  bool ready_ = false;
  bool starting_ = false;
  QSet<uint16_t> held_;
  QTimer retryTimer_;
  QTimer batteryTimer_;
  hidpp::BatteryStatus battery_;
  Info info_;
  int originalPointerSpeed_ = -1;  ///< the level before we changed it, -1 while untouched
};

}  // namespace projecteur
