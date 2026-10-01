// SPDX-License-Identifier: MIT
#include <QSignalSpy>
#include <QtTest>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <unistd.h>

#include "fakeremote.h"
#include "hidpp.h"
#include "keysink.h"
#include "remote.h"

using namespace projecteur;
using namespace projecteur::hidpp;
using T = Command::Type;

namespace {

QByteArray hex(const char* s) { return QByteArray::fromHex(s); }

class RecordingOverlay : public OverlaySink {
 public:
  void apply(const Commands& c) override { all.insert(all.end(), c.begin(), c.end()); }
  Commands all;
};

/// Two connected stream sockets: `ours` is handed to the code under test, `theirs` plays the kernel.
struct Pipe {
  Pipe() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) qFatal("socketpair failed");
    ours = fds[0];
    theirs = fds[1];
    ::fcntl(theirs, F_SETFL, ::fcntl(theirs, F_GETFL) | O_NONBLOCK);
  }
  void event(int type, int code, int value) {  // write one struct input_event
    input_event ev{};
    ev.type = static_cast<__u16>(type);
    ev.code = static_cast<__u16>(code);
    ev.value = value;
    const ssize_t n = ::write(theirs, &ev, sizeof ev);
    QVERIFY(n == static_cast<ssize_t>(sizeof ev));
  }
  void key(int code, int value) { event(EV_KEY, code, value); event(EV_SYN, SYN_REPORT, 0); }
  /// Key presses written into the pipe by an FdKeySink on the other end.
  QList<int> pressedKeys() {
    QList<int> out;
    input_event ev;
    while (::read(theirs, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev))
      if (ev.type == EV_KEY && ev.value == 1) out.append(ev.code);
    return out;
  }
  int ours = -1, theirs = -1;
};

Command cmd(T t) { return Command{t}; }
Command show(Mode m) { Command c{T::ShowAtPointer}; c.mode = m; return c; }
Command move(double dx, double dy) { Command c{T::MoveBy}; c.dx = dx; c.dy = dy; return c; }
Command setMode(Mode m) { Command c{T::SetMode}; c.mode = m; return c; }

struct Rig {
  Rig() {
    int h[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, h) != 0) qFatal("socketpair failed");
    fake = new FakeRemote(h[1]);
    keyOut = new FdKeySink(keyPipe.ours);
    Remote::Options opt;
    opt.grab = false;  // sockets, not input nodes
    opt.device.retryMs = 20;
    opt.device.requestTimeoutMs = 60;
    remote = new Remote({h[0], keyboard.ours, mouse.ours}, keyOut, &overlay, opt);
  }
  ~Rig() { delete remote; delete fake; delete keyOut; }
  bool ready() { return QTest::qWaitFor([this] { return remote->device()->isReady(); }, 3000); }

  Pipe keyboard, mouse, keyPipe;
  RecordingOverlay overlay;
  FakeRemote* fake;
  FdKeySink* keyOut;
  Remote* remote;
};

}  // namespace

class RemoteTest : public QObject {
  Q_OBJECT

 private slots:
  void theMeasuredSessionEndToEnd() {
    Rig r;
    QVERIFY(r.ready());

    r.fake->notify(hex("11010700" "00d8" "000000000000"));                      // action button held
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));
    QCOMPARE(r.overlay.all.back(), show(Mode::Highlight));

    r.fake->notify(hex("11010710" "0003" "fffb" "00000000000000000000"));        // dx=3 dy=-5
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), move(3, -5));

    r.fake->notify(hex("110107000000000000000000"));                            // released: frozen, nothing happens
    QTest::qWait(60);
    QCOMPARE(r.overlay.all.size(), size_t(2));

    r.mouse.key(BTN_LEFT, 1);                                                   // short press = plain mouse click
    r.mouse.key(BTN_LEFT, 0);
    QTRY_COMPARE(r.overlay.all.size(), size_t(3));
    QCOMPARE(r.overlay.all.back(), cmd(T::Hide));

    r.fake->notify(hex("11010700" "00df" "000000000000"));                      // double click
    QTRY_COMPARE(r.overlay.all.size(), size_t(4));
    QCOMPARE(r.overlay.all.back(), setMode(Mode::Magnify));

    r.keyboard.key(KEY_RIGHT, 1);                                               // Next
    r.keyboard.key(KEY_RIGHT, 0);
    QList<int> forwarded;                                                       // (QTRY_* re-evaluates its expression: accumulate)
    QTRY_VERIFY((forwarded += r.keyPipe.pressedKeys(), !forwarded.isEmpty()));  // re-sent through the virtual keyboard
    QCOMPARE(forwarded, (QList<int>{KEY_RIGHT}));
  }

  void nextAndBackAreForwardedAsTheSameKeys() {
    Rig r;
    QVERIFY(r.ready());
    r.keyboard.key(KEY_RIGHT, 1);
    r.keyboard.key(KEY_LEFT, 1);
    QList<int> got;
    QTRY_VERIFY((got += r.keyPipe.pressedKeys(), got.size() >= 2));
    QCOMPARE(got, (QList<int>{KEY_RIGHT, KEY_LEFT}));
  }

  void otherKeysAreForwardedUnchanged() {
    Rig r;
    QVERIFY(r.ready());
    r.keyboard.key(KEY_PAGEDOWN, 1);
    QList<int> got;
    QTRY_VERIFY((got += r.keyPipe.pressedKeys(), !got.isEmpty()));
    QCOMPARE(got, (QList<int>{KEY_PAGEDOWN}));
  }

  void pointerMotionOnTheMouseNodeIsSwallowed() {
    Rig r;
    QVERIFY(r.ready());
    r.mouse.event(EV_REL, REL_X, 7);
    r.mouse.event(EV_REL, REL_Y, -3);
    r.mouse.event(EV_SYN, SYN_REPORT, 0);
    QTest::qWait(80);
    QVERIFY(r.overlay.all.empty());
    QVERIFY(r.keyPipe.pressedKeys().isEmpty());
  }

  void aVisibleEffectIsRecenteredOnSlideChange() {
    Rig r;
    QVERIFY(r.ready());
    r.fake->notify(hex("11010700" "00d8" "000000000000"));
    r.fake->notify(hex("110107000000000000000000"));  // frozen and visible
    QTRY_COMPARE(r.overlay.all.size(), size_t(1));
    r.keyboard.key(KEY_RIGHT, 1);
    QTRY_COMPARE(r.overlay.all.size(), size_t(2));
    QCOMPARE(r.overlay.all.back(), cmd(T::Recenter));
  }

  void theConnectionPulseIsLongAndHappensOnlyOnce() {  // policy: short pulses, one longer one for "connected"
    Rig r;
    QVERIFY(r.ready());
    const QByteArray pulse = hidpp::vibrate(0x09, 3, 0x80);
    QTRY_COMPARE(r.fake->requests.count(pulse), 1);
    r.fake->notify(hex("10014100" "000000"));  // the remote wakes up: diversions are applied again
    QTest::qWait(150);
    QVERIFY(r.ready());
    QCOMPARE(r.fake->requests.count(pulse), 1);  // no second buzz
  }

  void unpluggingTheKeyboardNodeEmitsGone() {
    Rig r;
    QVERIFY(r.ready());
    QSignalSpy gone(r.remote, &Remote::gone);
    ::close(r.keyboard.theirs);
    r.keyboard.theirs = -1;
    QTRY_COMPARE(gone.count(), 1);
  }

  void deletingTheRemoteReleasesTheDiversions() {
    Rig r;
    QVERIFY(r.ready());
    QTRY_COMPARE(r.fake->requests.count(hidpp::vibrate(0x09, 3, 0x80)), 1);  // the connection pulse has arrived
    r.fake->requests.clear();
    delete r.remote;
    r.remote = nullptr;
    QTRY_VERIFY(r.fake->requests.size() >= 2);
    QCOMPARE(r.fake->requests.at(0).left(7), hex("1101073d00d822"));
    QCOMPARE(r.fake->requests.at(1).left(7), hex("1101073d00df22"));
  }
};

QTEST_MAIN(RemoteTest)
#include "test_remote.moc"
