// SPDX-License-Identifier: MIT
#include "evdevdevice.h"

#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace projecteur {

EvdevDevice::EvdevDevice(int fd, QObject* parent) : QObject(parent), fd_(fd) {
  const int flags = ::fcntl(fd_, F_GETFL, 0);
  ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this, &EvdevDevice::onReadable);
}

EvdevDevice::~EvdevDevice() {
  if (fd_ >= 0) ::close(fd_);  // closing also releases a grab
}

EvdevDevice* EvdevDevice::open(const QString& path, QString* error, QObject* parent) {
  const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    return nullptr;
  }
  return new EvdevDevice(fd, parent);
}

bool EvdevDevice::grab(bool on) { return fd_ >= 0 && ::ioctl(fd_, EVIOCGRAB, on ? 1 : 0) == 0; }

void EvdevDevice::onReadable() {
  input_event ev[32];
  for (;;) {
    const ssize_t n = ::read(fd_, ev, sizeof ev);
    if (n > 0) {
      for (size_t i = 0; i < static_cast<size_t>(n) / sizeof(input_event); ++i) {
        const input_event& e = ev[i];
        if (e.type == EV_KEY) {
          emit key(e.code, e.value);
        } else if (e.type == EV_REL) {
          if (e.code == REL_X) pendingDx_ += e.value;
          if (e.code == REL_Y) pendingDy_ += e.value;
        } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
          if (pendingDx_ != 0 || pendingDy_ != 0) emit motion(pendingDx_, pendingDy_);
          pendingDx_ = pendingDy_ = 0;
        }
      }
      continue;
    }
    if (n < 0 && errno == EAGAIN) return;
    notifier_->setEnabled(false);  // EOF or ENODEV: the node is gone
    ::close(fd_);
    fd_ = -1;
    emit closed();
    return;
  }
}

}  // namespace projecteur
