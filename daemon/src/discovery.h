// SPDX-License-Identifier: MIT
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

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
/// The USB receiver if present, else a Bluetooth Spotlight. Only looks at sysfs when the text names a candidate.
SpotlightNodes findSpotlight(const QString& procInputDevices, const QString& sysHidraw = QStringLiteral("/sys/class/hidraw"),
                             const QString& devDir = QStringLiteral("/dev"));
SpotlightNodes findSpotlight();  ///< on the running system

/// Looks for the remote whenever a device node appears and keeps one Remote alive while it is there.
class RemoteWatcher : public QObject {
  Q_OBJECT
 public:
  using Finder = std::function<SpotlightNodes()>;
  static constexpr int kSettleMs = 300;   ///< after the last change in /dev, before looking
  static constexpr int kRetryMs = 500;    ///< found, but the nodes cannot be opened (yet)
  static constexpr int kOpenRetries = 10;

  RemoteWatcher(KeySink* keys, PointerSink* pointer, OverlaySink* overlay, Remote::Options options, QObject* parent = nullptr);
  /// Tests: where to look and which directories to watch instead of the running system's (call before start()).
  void setFinder(Finder find, const QStringList& watchedDirs);
  /// Look now, again whenever a node changes in the watched directories, and every `fallbackPollMs` in any case.
  void start(int fallbackPollMs = 30000);
  bool connected() const { return remote_ != nullptr; }
  Remote* remote() const { return remote_; }
  int polls() const { return polls_; }   ///< how often it looked (tests)

 signals:
  void remoteChanged(Remote* remote);  ///< a remote appeared (right after it was created), or nullptr: it is gone

 private:
  void poll();

  KeySink* keys_;
  PointerSink* pointer_;
  OverlaySink* overlay_;
  Remote::Options options_;
  Remote* remote_ = nullptr;
  Finder find_;
  QStringList watchedDirs_{QStringLiteral("/dev/input"), QStringLiteral("/dev")};
  QFileSystemWatcher nodes_;
  QTimer settle_, fallback_;
  int openRetries_ = 0;
  bool warnedOpen_ = false;
  int polls_ = 0;
};

}  // namespace projecteur
