// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <memory>

namespace projecteur {

/// Where forwarded keys go: the daemon grabs the remote's keyboard node, so Next / Back have to be sent again.
class KeySink {
 public:
  virtual ~KeySink() = default;
  virtual void tap(int keyCode) = 0;  ///< press and release one key (evdev KEY_* code)
};

/// A virtual keyboard created through /dev/uinput (needs the uaccess rule that the udev package ships).
class UinputKeyboard : public KeySink {
 public:
  static std::unique_ptr<UinputKeyboard> create(QString* error);
  ~UinputKeyboard() override;
  void tap(int keyCode) override;

 private:
  explicit UinputKeyboard(int fd) : fd_(fd) {}
  void emitEvent(int type, int code, int value);
  int fd_;
};

/// Test double: writes `struct input_event` records for each tap into a descriptor.
class FdKeySink : public KeySink {
 public:
  explicit FdKeySink(int fd) : fd_(fd) {}
  void tap(int keyCode) override;

 private:
  int fd_;
};

}  // namespace projecteur
