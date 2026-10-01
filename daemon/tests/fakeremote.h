// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSocketNotifier>
#include <QtTest>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

/// Plays the remote on the other end of a SOCK_SEQPACKET socket (which keeps message boundaries like hidraw does).
class FakeRemote : public QObject {
  Q_OBJECT
 public:
  explicit FakeRemote(int fd) : fd_(fd) {
    ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);  // the read loop must not block
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &FakeRemote::onReadable);
  }
  ~FakeRemote() override { closeNow(); }
  void closeNow() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }
  void notify(const QByteArray& m) {
    const ssize_t n = ::write(fd_, m.constData(), static_cast<size_t>(m.size()));
    QVERIFY(n == m.size());
  }

  int ignoreFirst = 0;         ///< a sleeping remote does not answer the first requests
  bool longOnly = false;       ///< Bluetooth: short (7 byte) reports are not accepted
  QList<QByteArray> requests;  ///< everything received, answered or not

 private:
  void onReadable() {
    for (;;) {
      char buf[64];
      const ssize_t n = ::read(fd_, buf, sizeof buf);
      if (n <= 0) return;  // nothing more to read (or the other side closed)
      const QByteArray m(buf, static_cast<qsizetype>(n));
      requests.append(m);
      if (longOnly && m.size() != 20) continue;  // the kernel rejects short reports on the Bluetooth node: no answer
      if (ignoreFirst > 0) {
        --ignoreFirst;
        continue;
      }
      QByteArray reply = m;  // by default acknowledge by echoing, as the real remote does for set requests
      if (static_cast<uint8_t>(m[2]) == 0x00) {  // IRoot.GetFeature(feature id) -> index
        const int id = (static_cast<uint8_t>(m[4]) << 8) | static_cast<uint8_t>(m[5]);
        reply = QByteArray::fromHex("1001000d000002");
        reply[4] = static_cast<char>(id == 0x1b04 ? 0x07 : id == 0x1a00 ? 0x09 : 0x00);
        reply[1] = m[1];                                  // same device index as asked (0xff over Bluetooth)
        if (m.size() == 20) { reply[0] = 0x11; reply.resize(20); }  // a long request gets a long answer
      }
      // the daemon may already have closed its end (shutdown): answering then fails, which is fine
      const ssize_t w = ::write(fd_, reply.constData(), static_cast<size_t>(reply.size()));
      (void)w;
    }
  }
  int fd_;
  QSocketNotifier* notifier_ = nullptr;
};
