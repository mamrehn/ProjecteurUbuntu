// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "remote.h"

namespace projecteur {

/// The device nodes of one Spotlight receiver.
struct SpotlightNodes {
  QString hidraw;    ///< /dev/hidrawN: the vendor HID++ interface (interface 2)
  QString keyboard;  ///< /dev/input/eventN: Next / Back
  QString mouse;     ///< /dev/input/eventN: the action button's clicks
  bool bluetooth = false;
  bool complete() const { return !hidraw.isEmpty() && !keyboard.isEmpty() && !mouse.isEmpty(); }
};

/// Find the USB receiver (046d:c53e). `procInputDevices` is the text of /proc/bus/input/devices; `sysHidraw` the
/// directory /sys/class/hidraw; `devDir` where the nodes live. All injectable for tests.
SpotlightNodes findUsbReceiver(const QString& procInputDevices, const QString& sysHidraw = QStringLiteral("/sys/class/hidraw"),
                               const QString& devDir = QStringLiteral("/dev"));
SpotlightNodes findUsbReceiver();  ///< on the running system

/// Find a Spotlight connected over Bluetooth (046d:b503). Its HID++ interface is the only hidraw node of the device,
/// the input nodes are called "SPOTLIGHT Keyboard" and "SPOTLIGHT Mouse".
SpotlightNodes findBluetoothSpotlight(const QString& procInputDevices, const QString& sysHidraw = QStringLiteral("/sys/class/hidraw"),
                                      const QString& devDir = QStringLiteral("/dev"));
/// The USB receiver if present, else a Bluetooth Spotlight, on the running system.
SpotlightNodes findSpotlight();

/// Polls for the receiver and keeps one Remote alive while it is plugged in.
class RemoteWatcher : public QObject {
  Q_OBJECT
 public:
  RemoteWatcher(KeySink* keys, PointerSink* pointer, OverlaySink* overlay, Remote::Options options, QObject* parent = nullptr);
  void start(int intervalMs = 2000);
  bool connected() const { return remote_ != nullptr; }
  Remote* remote() const { return remote_; }

 signals:
  void remoteChanged(Remote* remote);  ///< a remote appeared (right after it was created), or nullptr: it is gone

 private:
  void poll();

  KeySink* keys_;
  PointerSink* pointer_;
  OverlaySink* overlay_;
  Remote::Options options_;
  Remote* remote_ = nullptr;
  QTimer timer_;
};

}  // namespace projecteur
