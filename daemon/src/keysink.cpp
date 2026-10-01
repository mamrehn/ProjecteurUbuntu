// SPDX-License-Identifier: MIT
#include "keysink.h"

#include <cerrno>
#include <cstdlib>
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
  (void)n;  // a failed event cannot be handled in any useful way; the next one simply tries again
}

void writeKey(int fd, int keyCode, bool down) {
  writeEvent(fd, EV_KEY, keyCode, down ? 1 : 0);
  writeEvent(fd, EV_SYN, SYN_REPORT, 0);
}

void writeScroll(int fd, int notches) {
  if (notches == 0) return;
  // the kernel convention: the legacy notch axis and the hi-res axis (120 units per notch) together
  writeEvent(fd, EV_REL, REL_WHEEL, notches);
  writeEvent(fd, EV_REL, REL_WHEEL_HI_RES, notches * 120);
  writeEvent(fd, EV_SYN, SYN_REPORT, 0);
}

int openUinput(QString* error) {
  const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0 && error)
    *error = QStringLiteral("cannot open /dev/uinput: %1 (is the udev rule installed and are you on the local session?)")
                 .arg(QString::fromLocal8Bit(std::strerror(errno)));
  return fd;
}

bool createDevice(int fd, const char* name, QString* error) {
  uinput_setup setup{};
  setup.id.bustype = BUS_VIRTUAL;
  setup.id.vendor = 0x0001;
  setup.id.product = 0x0001;
  std::strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);
  if (::ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ::ioctl(fd, UI_DEV_CREATE) == 0) return true;
  if (error) *error = QStringLiteral("cannot create the virtual device: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
  return false;
}
}  // namespace

std::unique_ptr<UinputKeyboard> UinputKeyboard::create(QString* error) {
  const int fd = openUinput(error);
  if (fd < 0) return nullptr;
  bool ok = ::ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
  for (int key = KEY_ESC; ok && key <= KEY_MICMUTE; ++key) ok = ::ioctl(fd, UI_SET_KEYBIT, key) == 0;
  if (!ok && error) *error = QStringLiteral("cannot set up the virtual keyboard: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
  if (!ok || !createDevice(fd, "Projecteur Virtual Keyboard", ok ? error : nullptr)) {
    ::close(fd);
    return nullptr;
  }
  return std::unique_ptr<UinputKeyboard>(new UinputKeyboard(fd));
}

UinputKeyboard::~UinputKeyboard() {
  ::ioctl(fd_, UI_DEV_DESTROY);
  ::close(fd_);
}

void UinputKeyboard::key(int keyCode, bool down) { writeKey(fd_, keyCode, down); }

std::unique_ptr<UinputPointer> UinputPointer::create(QString* error) {
  const int fd = openUinput(error);
  if (fd < 0) return nullptr;
  bool ok = ::ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(fd, UI_SET_EVBIT, EV_REL) == 0 &&
            ::ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
  for (const int btn : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE}) ok = ok && ::ioctl(fd, UI_SET_KEYBIT, btn) == 0;
  for (const int rel : {REL_X, REL_Y, REL_WHEEL, REL_WHEEL_HI_RES}) ok = ok && ::ioctl(fd, UI_SET_RELBIT, rel) == 0;
  if (!ok || !createDevice(fd, "Projecteur Virtual Pointer", error)) {
    ::close(fd);
    return nullptr;
  }
  return std::unique_ptr<UinputPointer>(new UinputPointer(fd));
}

UinputPointer::~UinputPointer() {
  ::ioctl(fd_, UI_DEV_DESTROY);
  ::close(fd_);
}

void UinputPointer::scroll(int notches) { writeScroll(fd_, notches); }

void FdKeySink::key(int keyCode, bool down) { writeKey(fd_, keyCode, down); }

void FdPointerSink::scroll(int notches) { writeScroll(fd_, notches); }

}  // namespace projecteur
