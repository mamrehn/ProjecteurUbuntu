// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <deque>
#include <functional>

class QSocketNotifier;

namespace projecteur {

/// HID++ messages over a file descriptor (a /dev/hidrawN node; in tests a SOCK_SEQPACKET socket, which keeps the
/// message boundaries like hidraw does). One request at a time is in flight; its answer is matched by device,
/// feature index and function/software id. Everything else that arrives is a notification.
class HidppLink : public QObject {
  Q_OBJECT
 public:
  /// Called with the answer, or with an empty array when the request timed out (a sleeping remote does that).
  using Reply = std::function<void(const QByteArray& answer)>;

  /// Takes ownership of `fd`.
  explicit HidppLink(int fd, QObject* parent = nullptr);
  ~HidppLink() override;

  void send(const QByteArray& message);
  void request(const QByteArray& message, Reply callback, int timeoutMs = 1000);
  bool isOpen() const { return fd_ >= 0; }

 signals:
  void notification(const QByteArray& message);
  void closed();  ///< the device went away

 private:
  struct Pending {
    QByteArray message;
    Reply callback;
    int timeoutMs;
  };
  void onReadable();
  void closeLink();  ///< the device is gone: close the descriptor and say so, once
  void startNext();
  void finishFront(const QByteArray& answer);

  int fd_;
  QSocketNotifier* notifier_ = nullptr;
  std::deque<Pending> queue_;
  bool inFlight_ = false;
  bool writeFailing_ = false;  ///< a failed write was reported; not again until one succeeds
  QTimer timeout_;
};

}  // namespace projecteur
