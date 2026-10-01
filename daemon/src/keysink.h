// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace projecteur {

/// Where forwarded keys go: the daemon grabs the remote's keyboard node, so Next / Back have to be sent again, and
/// the held-key actions (start presentation, volume, shortcuts) are keys too.
class KeySink {
 public:
  virtual ~KeySink() = default;
  virtual void key(int keyCode, bool down) = 0;  ///< one key event (evdev KEY_* code), followed by a sync
  void tap(int keyCode) { key(keyCode, true); key(keyCode, false); }  ///< press and release
  /// Press the keys in order, release them in reverse: for example {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_P}.
  void chord(const QList<int>& keyCodes) {
    for (int k : keyCodes) key(k, true);
    for (auto it = keyCodes.crbegin(); it != keyCodes.crend(); ++it) key(*it, false);
  }
};

/// A virtual pointer for the scroll action: wheel events only (the remote's own pointer movement is not forwarded).
class PointerSink {
 public:
  virtual ~PointerSink() = default;
  virtual void scroll(int notches) = 0;  ///< positive scrolls up (away from the user), negative down
};

/// A virtual keyboard created through /dev/uinput (needs the uaccess rule that the udev package ships).
class UinputKeyboard : public KeySink {
 public:
  static std::unique_ptr<UinputKeyboard> create(QString* error);
  ~UinputKeyboard() override;
  void key(int keyCode, bool down) override;

 private:
  explicit UinputKeyboard(int fd) : fd_(fd) {}
  int fd_;
};

/// A virtual mouse that only ever scrolls (it declares pointer buttons and axes so that libinput treats it as a
/// pointer, but never moves or clicks).
class UinputPointer : public PointerSink {
 public:
  static std::unique_ptr<UinputPointer> create(QString* error);
  ~UinputPointer() override;
  void scroll(int notches) override;

 private:
  explicit UinputPointer(int fd) : fd_(fd) {}
  int fd_;
};

/// Test double: writes `struct input_event` records into a descriptor.
class FdKeySink : public KeySink {
 public:
  explicit FdKeySink(int fd) : fd_(fd) {}
  void key(int keyCode, bool down) override;

 private:
  int fd_;
};

/// Test double for the virtual pointer: writes the wheel events into a descriptor.
class FdPointerSink : public PointerSink {
 public:
  explicit FdPointerSink(int fd) : fd_(fd) {}
  void scroll(int notches) override;

 private:
  int fd_;
};

}  // namespace projecteur
