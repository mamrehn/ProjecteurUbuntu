// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <cstdint>

#include "hidpplink.h"

namespace projecteur {

/// The Spotlight behind a HID++ link: finds the features, diverts the action button, turns the notifications
/// into signals. A sleeping remote ignores requests, so the handshake simply asks again until it answers.
class SpotlightDevice : public QObject {
  Q_OBJECT
 public:
  struct Config {
    /// Also divert the raw X/Y movement of the hold. The system pointer then stays still while the effect moves
    /// (Windows "cursor control off"). When false the movement arrives as ordinary pointer motion.
    bool rawMovement = true;
    int retryMs = 1000;           ///< pause before asking a silent remote again
    int requestTimeoutMs = 1000;  ///< how long to wait for an answer before treating the remote as asleep
    uint8_t deviceIndex = 0x01;   ///< HID++ device index: 1 behind the USB receiver, 0xff when connected directly (Bluetooth)
    bool longMessagesOnly = false;  ///< Bluetooth: the hidraw node accepts only 20 byte reports
  };

  SpotlightDevice(HidppLink* link, Config config, QObject* parent = nullptr);

  void start();     ///< (re)run the handshake
  void shutdown();  ///< undivert the controls (best effort, nothing waits for the answer)
  /// Vibrate. length 0 is not felt on the original Spotlight; use 1 for a short pulse.
  void vibrate(uint8_t length, uint8_t intensity);
  bool isReady() const { return ready_; }
  bool canVibrate() const { return presenterIndex_ != 0; }

 signals:
  void readyChanged(bool ready);
  void holdDown();
  void holdUp();
  void rawMove(int dx, int dy);
  void doubleClick();
  void problem(const QString& message);

 private:
  void lookupReprogramControls();
  void lookupPresenterControl();
  void divert(uint16_t cid, uint8_t flags, std::function<void()> next);
  void retryLater();
  void setReady(bool ready);
  void onNotification(const QByteArray& message);

  HidppLink* link_;
  Config config_;
  uint8_t reprogIndex_ = 0;
  uint8_t presenterIndex_ = 0;
  bool ready_ = false;
  bool starting_ = false;
  QSet<uint16_t> held_;
  QTimer retryTimer_;
};

}  // namespace projecteur
