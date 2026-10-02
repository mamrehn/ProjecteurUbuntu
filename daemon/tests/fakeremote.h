// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
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
  int fd() const { return fd_; }
  void notify(const QByteArray& m) {
    const ssize_t n = ::write(fd_, m.constData(), static_cast<size_t>(m.size()));
    QVERIFY(n == m.size());
  }

  /// Feature indices of the measured unit (Bluetooth, 2026-10-01). Remove an entry to play a model without it.
  static constexpr uint8_t kReprogIndex = 0x07, kPresenterIndex = 0x09, kBatteryIndex = 0x06, kFirmwareIndex = 0x02,
                           kPointerSpeedIndex = 0x0a;
  QMap<int, int> features{{0x1b04, kReprogIndex}, {0x1a00, kPresenterIndex}, {0x1000, kBatteryIndex},
                          {0x0003, kFirmwareIndex}, {0x2205, kPointerSpeedIndex}};
  int batteryPercent = 100, batteryNext = 75, batteryState = 0;  ///< as measured: "64 4b 00"
  int pointerSpeed = 0x14;

  int ignoreFirst = 0;         ///< a sleeping remote does not answer the first requests
  int answerDelayMs = 0;       ///< an idle remote acts on a request at once but answers late (0.4 to 0.85 s measured)
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
      const uint8_t index = static_cast<uint8_t>(m[2]);
      const uint8_t function = static_cast<uint8_t>(m[3]) >> 4;
      const auto longAnswer = [&](std::initializer_list<uint8_t> data) {  // data from byte 4 on, in a long report
        QByteArray r(20, '\0');
        r[0] = 0x11;
        r[1] = m[1];  // same device index as asked (0xff over Bluetooth)
        r[2] = m[2];
        r[3] = m[3];
        int i = 4;
        for (uint8_t d : data) r[i++] = static_cast<char>(d);
        return r;
      };
      if (index == 0x00) {  // IRoot.GetFeature(feature id) -> index
        const int id = (static_cast<uint8_t>(m[4]) << 8) | static_cast<uint8_t>(m[5]);
        reply = QByteArray::fromHex("1001000d000002");
        reply[4] = static_cast<char>(features.value(id, 0));
        reply[1] = m[1];
        if (m.size() == 20) { reply[0] = 0x11; reply.resize(20); }  // a long request gets a long answer
      } else if (index == kBatteryIndex && function == 0 && features.contains(0x1000)) {
        reply = longAnswer({uint8_t(batteryPercent), uint8_t(batteryNext), uint8_t(batteryState)});
      } else if (index == kPointerSpeedIndex && function == 0 && features.contains(0x2205)) {
        reply = longAnswer({uint8_t(pointerSpeed), 0x00});
      } else if (index == kPointerSpeedIndex && function == 1 && features.contains(0x2205)) {
        pointerSpeed = static_cast<uint8_t>(m[4]);   // setSpeed: remembered, and echoed below
      } else if (index == kFirmwareIndex && features.contains(0x0003)) {
        if (function == 0) {
          reply = longAnswer({3});
        } else {  // the three entities of the measured unit: bootloader, main firmware, hardware
          const uint8_t entity = static_cast<uint8_t>(m[4]);
          if (entity == 0) reply = longAnswer({0x01, 'B', 'O', 'T', 0x26, 0x01, 0x00, 0x15});
          else if (entity == 1) reply = longAnswer({0x00, 'M', 'P', 'O', 0x01, 0x01, 0x00, 0x32});
          else reply = longAnswer({0x05});
        }
      }
      if (answerDelayMs > 0)
        QTimer::singleShot(answerDelayMs, this, [this, reply] { answer(reply); });
      else
        answer(reply);
    }
  }
  void answer(const QByteArray& reply) {
    if (fd_ < 0) return;
    // the daemon may already have closed its end (shutdown): answering then fails, which is fine
    const ssize_t w = ::write(fd_, reply.constData(), static_cast<size_t>(reply.size()));
    (void)w;
  }
  int fd_;
  QSocketNotifier* notifier_ = nullptr;
};
