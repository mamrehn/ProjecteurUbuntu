# SPDX-License-Identifier: MIT
"""End-to-end test: the real projecteurd binary + the real extension, inside the isolated headless shell.

A fake Spotlight speaks the byte formats recorded from the real hardware (see doc/ubuntu/INPUT-MODEL.md):
HID++ over a SOCK_SEQPACKET socket (hidraw), `struct input_event` records over stream sockets (the two evdev
nodes), and a fourth socket receives the keys the daemon forwards (instead of /dev/uinput). The daemon talks to
the extension over the private D-Bus of the harness; screenshots of the virtual monitor show the result.
"""
import os
import select
import signal
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as common  # noqa: E402  (pure helpers: tile_at, close, W, H, diameter, ...)

EV = struct.Struct('llHHi')
EV_SYN, EV_KEY = 0, 1
KEY_LEFT, KEY_RIGHT, BTN_LEFT = 105, 106, 0x110
REPROG_IDX, PRESENTER_IDX = 0x07, 0x09

HOLD_DOWN = bytes.fromhex('11010700' '00d8' '000000000000')
HOLD_UP = bytes.fromhex('110107000000000000000000')
DOUBLE_CLICK = bytes.fromhex('11010700' '00df' '000000000000')


def raw_move(dx, dy):  # 11 01 07 10 <dx:int16> <dy:int16> ... as measured
    return bytes([0x11, 0x01, REPROG_IDX, 0x10]) + struct.pack('>hh', dx, dy) + bytes(12)


def ev(t, c, v):
    return EV.pack(0, 0, t, c, v)


class FakeSpotlight(threading.Thread):
    """Answers the daemon's HID++ requests like the real remote and lets the test inject notifications."""

    def __init__(self, sock):
        super().__init__(daemon=True)
        self.sock, self.requests, self.stop_flag = sock, [], False

    def run(self):
        while not self.stop_flag:
            if not select.select([self.sock], [], [], 0.1)[0]:
                continue
            try:
                m = self.sock.recv(64)
            except OSError:
                return
            if not m:
                return
            self.requests.append(m)
            if m[2] == 0x00:      # IRoot.GetFeature(feature id) -> index
                fid = (m[4] << 8) | m[5]
                reply = bytes([0x10, m[1], 0x00, m[3], {0x1b04: REPROG_IDX, 0x1a00: PRESENTER_IDX}.get(fid, 0), 0, 2])
            else:                 # acknowledge by echoing, as the real remote does
                reply = m
            try:
                self.sock.send(reply)
            except OSError:
                pass   # the daemon already closed its end (shutdown): keep reading what it sent before

    def notify(self, data):
        self.sock.send(data)


def run_daemon_tests(h, daemon):
    print('== daemon end to end (projecteurd + extension; fake remote speaking the recorded bytes)')
    hid_d, hid_f = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    kb_d, kb_f = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    mouse_d, mouse_f = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    key_d, key_f = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    daemon_fds = [s.fileno() for s in (hid_d, kb_d, mouse_d, key_d)]

    fake = FakeSpotlight(hid_f)
    fake.start()
    log = open(h.tmp / 'daemon.log', 'wb')
    proc = subprocess.Popen([daemon, '--test-fds', ','.join(map(str, daemon_fds)), '--verbose'],
                            pass_fds=daemon_fds, env=dict(os.environ), stdout=log, stderr=subprocess.STDOUT)
    for s in (hid_d, kb_d, mouse_d, key_d):
        s.close()

    def wait(cond, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            if cond():
                return True
            time.sleep(0.05)
        return False

    def state():
        return h.call('GetState')   # (visible, mode, x, y)

    def forwarded_keys():
        keys = []
        key_f.setblocking(False)
        try:
            while True:
                data = key_f.recv(EV.size * 16)
                if not data:
                    break
                for i in range(0, len(data) - EV.size + 1, EV.size):
                    _, _, t, c, v = EV.unpack_from(data, i)
                    if t == EV_KEY and v == 1:
                        keys.append(c)
        except BlockingIOError:
            pass
        return keys

    try:
        # --- safety: the daemon must be on the PRIVATE bus, never on the user's real session bus
        def daemon_on_bus():
            names = h.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'ListNames',
                                    None, None, 0, 2000, None).unpack()[0]
            for n in (x for x in names if x.startswith(':')):
                try:
                    pid = h.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                          'GetConnectionUnixProcessID', h.GLib.Variant('(s)', (n,)), None, 0, 1000, None).unpack()[0]
                except Exception:   # noqa: BLE001 - connection vanished meanwhile
                    continue
                if pid == proc.pid:
                    return True
            return False
        h.check('the daemon is connected to the private test bus', wait(daemon_on_bus) and proc.poll() is None)

        # --- handshake and the one longer "connected" pulse
        h.check('daemon starts the handshake and diverts the action button with raw X/Y',
                wait(lambda: bytes.fromhex('1101073d00d833') in [m[:7] for m in fake.requests]))
        h.check('daemon diverts the double click', bytes.fromhex('1101073d00df03') in [m[:7] for m in fake.requests])
        h.check('daemon sends the one longer "connected" pulse (length 3)',
                wait(lambda: bytes.fromhex('1101091d03e880') in [m[:7] for m in fake.requests]))

        # --- hold and move: the effect appears at the pointer and follows the remote's raw movement
        h.call('Hide')
        h.call('SetMode', 's', 'highlight')
        h.call('TestPattern', 'b', True)
        fake.notify(HOLD_DOWN)
        h.check('hold shows the highlight', wait(lambda: state()[0] and state()[1] == 'highlight'), str(state()))
        for _ in range(40):
            fake.notify(raw_move(16, 9))
        h.check('raw movement moves the effect (40 x (16, 9) counts = (640, 360) px)',
                wait(lambda: state()[2:] == (640.0, 360.0)), str(state()))
        time.sleep(0.4)
        s = h.shot('e2e-highlight')
        inside, outside = (690, 380), (1000, 600)
        h.check('screen: inside the hole unchanged', common.close(s.px(*inside), common.tile_at(*inside), 3))
        want = tuple(round(c * common.DIM_MULTIPLIER) for c in common.tile_at(*outside))
        h.check('screen: outside the hole dimmed to 29 %', common.close(s.px(*outside), want, 5), f'got {s.px(*outside)} want {want}')

        # --- release: frozen
        fake.notify(HOLD_UP)
        time.sleep(0.4)
        h.check('release freezes the effect (still visible, same place)', state() == (True, 'highlight', 640.0, 360.0), str(state()))

        # --- short click (a plain mouse click) hides it
        mouse_f.send(ev(EV_KEY, BTN_LEFT, 1) + ev(EV_SYN, 0, 0) + ev(EV_KEY, BTN_LEFT, 0) + ev(EV_SYN, 0, 0))
        h.check('a short click hides it', wait(lambda: not state()[0]), str(state()))

        # --- double click selects the next mode
        fake.notify(DOUBLE_CLICK)
        fake.notify(HOLD_UP)   # the double click's release report (all controls up)
        h.check('a double click selects Magnify', wait(lambda: state()[1] == 'magnify'), str(state()))
        h.check('...and shows nothing yet', not state()[0])

        # --- hold again: the lens appears at the pointer and moves
        fake.notify(HOLD_DOWN)
        for _ in range(40):
            fake.notify(raw_move(16, 9))
        h.check('hold shows the live lens at the moved position', wait(lambda: state() == (True, 'magnify', 640.0, 360.0)), str(state()))
        time.sleep(0.4)
        s = h.shot('e2e-magnify')
        cx, cy = 640, 360
        sx, sy = cx + 100 / 2, cy + 20 / 2
        got, want, wrong = s.px(cx + 100, cy + 20), common.tile_at(sx, sy), common.tile_at(cx + 100, cy + 20)
        h.check('screen: the lens shows the magnified source (zoom 2)', common.close(got, want, 3) and not common.close(got, wrong, 3),
                f'got {got} want {want} plain {wrong}')
        fake.notify(HOLD_UP)

        # --- Next / Back are forwarded; a visible effect is recentered on a slide change
        fake.notify(HOLD_DOWN)
        for _ in range(4):
            fake.notify(raw_move(25, 12))
        fake.notify(HOLD_UP)
        h.check('the frozen effect can be moved on from where it was', wait(lambda: state()[2:] == (740.0, 408.0)), str(state()))
        kb_f.send(ev(EV_KEY, KEY_RIGHT, 1) + ev(EV_SYN, 0, 0) + ev(EV_KEY, KEY_RIGHT, 0) + ev(EV_SYN, 0, 0))
        got_keys = []
        h.check('Next is forwarded on the virtual keyboard', wait(lambda: (got_keys.extend(forwarded_keys()), KEY_RIGHT in got_keys)[1]), str(got_keys))
        h.check('Next also recenters the visible effect', wait(lambda: state()[2:] == (640.0, 360.0)), str(state()))
        kb_f.send(ev(EV_KEY, KEY_LEFT, 1) + ev(EV_SYN, 0, 0) + ev(EV_KEY, KEY_LEFT, 0) + ev(EV_SYN, 0, 0))
        h.check('Back is forwarded on the virtual keyboard', wait(lambda: (got_keys.extend(forwarded_keys()), KEY_LEFT in got_keys)[1]), str(got_keys))
    finally:
        # --- clean shutdown gives the buttons back
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
        try:
            rc = proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = -9
        time.sleep(0.2)
        undivert = [m[:7] for m in fake.requests if m[3] == 0x3d and m[6] == 0x22]
        h.check('the daemon exits cleanly on SIGTERM', rc == 0, f'exit status {rc}')
        h.check('...and releases both diversions', bytes.fromhex('1101073d00d822') in undivert and bytes.fromhex('1101073d00df22') in undivert, str(undivert))
        fake.stop_flag = True
        log.close()
        text = (h.tmp / 'daemon.log').read_text(errors='replace')
        bad = [l for l in text.splitlines() if 'warning' in l.lower() or 'critical' in l.lower() or 'error' in l.lower()]
        h.check('no warnings in the daemon log', not bad, '; '.join(bad[:2])[:200])
