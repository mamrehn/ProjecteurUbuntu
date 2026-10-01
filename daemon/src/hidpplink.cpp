// SPDX-License-Identifier: MIT
#include "hidpplink.h"

#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include "hidpp.h"

namespace projecteur {

HidppLink::HidppLink(int fd, QObject* parent) : QObject(parent), fd_(fd) {
  const int flags = ::fcntl(fd_, F_GETFL, 0);
  ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this, &HidppLink::onReadable);
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] { finishFront(QByteArray()); });
}

HidppLink::~HidppLink() {
  if (fd_ >= 0) ::close(fd_);
}

void HidppLink::send(const QByteArray& message) {
  if (fd_ < 0) return;
  const ssize_t n = ::write(fd_, message.constData(), static_cast<size_t>(message.size()));
  if (n < 0 && errno != EAGAIN) {  // the node vanished (unplugged)
    notifier_->setEnabled(false);
    ::close(fd_);
    fd_ = -1;
    emit closed();
  }
}

void HidppLink::request(const QByteArray& message, Reply callback, int timeoutMs) {
  queue_.push_back({message, std::move(callback), timeoutMs});
  startNext();
}

void HidppLink::startNext() {
  if (inFlight_ || queue_.empty()) return;
  inFlight_ = true;
  send(queue_.front().message);
  timeout_.start(queue_.front().timeoutMs);
}

void HidppLink::finishFront(const QByteArray& answer) {
  if (!inFlight_ || queue_.empty()) return;
  timeout_.stop();
  Reply cb = std::move(queue_.front().callback);
  queue_.pop_front();
  inFlight_ = false;
  if (cb) cb(answer);
  startNext();  // no-op if the callback already started the next request
}

void HidppLink::onReadable() {
  char buf[64];
  for (;;) {
    const ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n > 0) {
      const QByteArray msg(buf, static_cast<qsizetype>(n));
      if (!hidpp::isHidpp(msg)) continue;
      if (inFlight_ && !queue_.empty() && hidpp::answers(queue_.front().message, msg))
        finishFront(msg);
      else
        emit notification(msg);
      continue;
    }
    if (n < 0 && errno == EAGAIN) return;
    // EOF or a real error: the device is gone
    notifier_->setEnabled(false);
    ::close(fd_);
    fd_ = -1;
    emit closed();
    return;
  }
}

}  // namespace projecteur
