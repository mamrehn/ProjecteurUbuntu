// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;

namespace projecteur {

/// Reads one /dev/input/eventN node (in tests: any file descriptor carrying `struct input_event` records).
/// Grabbing it exclusively keeps the remote's events away from the desktop; the daemon forwards what it wants.
class EvdevDevice : public QObject {
  Q_OBJECT
 public:
  /// Takes ownership of `fd`.
  explicit EvdevDevice(int fd, QObject* parent = nullptr);
  ~EvdevDevice() override;

  /// Open a node read-only. Returns nullptr and fills `error` on failure.
  static EvdevDevice* open(const QString& path, QString* error, QObject* parent = nullptr);

  /// EVIOCGRAB. Only real input nodes support it; returns false (and does nothing) for other descriptors.
  bool grab(bool on);

 signals:
  void key(int code, int value);  ///< EV_KEY: KEY_* and BTN_*; value 1 press, 0 release, 2 repeat
  void motion(int dx, int dy);    ///< EV_REL, summed up to the next SYN_REPORT
  void closed();                  ///< unplugged

 private:
  void onReadable();

  int fd_;
  QSocketNotifier* notifier_ = nullptr;
  int pendingDx_ = 0, pendingDy_ = 0;
};

}  // namespace projecteur
