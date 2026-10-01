// SPDX-License-Identifier: MIT
#include "keysink.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

namespace projecteur {

namespace {
void writeEvent(int fd, int type, int code, int value) {
  input_event ev{};
  ev.type = static_cast<__u16>(type);
  ev.code = static_cast<__u16>(code);
  ev.value = value;
  const ssize_t n = ::write(fd, &ev, sizeof ev);
  (void)n;  // a failed key event cannot be handled in any useful way; the next tap simply tries again
}
}  // namespace

std::unique_ptr<UinputKeyboard> UinputKeyboard::create(QString* error) {
  const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    if (error)
      *error = QStringLiteral("cannot open /dev/uinput: %1 (is the udev rule installed and are you on the local session?)")
                   .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return nullptr;
  }
  bool ok = ::ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
  for (int key = KEY_ESC; ok && key <= KEY_MICMUTE; ++key) ok = ::ioctl(fd, UI_SET_KEYBIT, key) == 0;
  uinput_setup setup{};
  setup.id.bustype = BUS_VIRTUAL;
  setup.id.vendor = 0x0001;
  setup.id.product = 0x0001;
  std::strncpy(setup.name, "Projecteur Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);
  ok = ok && ::ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ::ioctl(fd, UI_DEV_CREATE) == 0;
  if (!ok) {
    if (error) *error = QStringLiteral("cannot create the virtual keyboard: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
    ::close(fd);
    return nullptr;
  }
  return std::unique_ptr<UinputKeyboard>(new UinputKeyboard(fd));
}

UinputKeyboard::~UinputKeyboard() {
  ::ioctl(fd_, UI_DEV_DESTROY);
  ::close(fd_);
}

void UinputKeyboard::emitEvent(int type, int code, int value) { writeEvent(fd_, type, code, value); }

void UinputKeyboard::tap(int keyCode) {
  emitEvent(EV_KEY, keyCode, 1);
  emitEvent(EV_SYN, SYN_REPORT, 0);
  emitEvent(EV_KEY, keyCode, 0);
  emitEvent(EV_SYN, SYN_REPORT, 0);
}

void FdKeySink::tap(int keyCode) {
  writeEvent(fd_, EV_KEY, keyCode, 1);
  writeEvent(fd_, EV_SYN, SYN_REPORT, 0);
  writeEvent(fd_, EV_KEY, keyCode, 0);
  writeEvent(fd_, EV_SYN, SYN_REPORT, 0);
}

}  // namespace projecteur
