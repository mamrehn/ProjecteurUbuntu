#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test of tools/spotlight_calibrate.py against a fake remote (a SOCK_SEQPACKET socket, like hidraw keeps message
boundaries). No hardware, no desktop session, no notifications."""
import io
import json
import os
import select
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import spotlight_calibrate as cal  # noqa: E402

REPROG, PRESENTER = 0x07, 0x09
failures = []


def check(name, ok, detail=''):
    print(f'  [{"PASS" if ok else "FAIL"}] {name}' + (f'  ({detail})' if detail and not ok else ''))
    if not ok:
        failures.append(name)


class FakeRemote(threading.Thread):
    def __init__(self, sock, index, long_only):
        super().__init__(daemon=True)
        self.sock, self.index, self.long_only, self.requests, self.stop = sock, index, long_only, [], False

    def run(self):
        while not self.stop:
            if not select.select([self.sock], [], [], 0.05)[0]:
                continue
            try:
                m = self.sock.recv(64)
            except OSError:
                return    # the tool closed its end
            if not m:
                return
            self.requests.append(m)
            if self.long_only and len(m) != 20:
                continue                      # the Bluetooth node rejects short reports: no answer
            if m[2] == 0x00:
                fid = (m[4] << 8) | m[5]
                reply = bytearray(m)
                reply[4] = {0x1b04: REPROG, 0x1a00: PRESENTER}.get(fid, 0)
                reply = bytes(reply)
            else:
                reply = m
            try:
                self.sock.send(reply)
            except OSError:
                pass

    def report(self, kind_function, payload):
        self.sock.send(bytes([0x11, self.index, REPROG, kind_function]) + payload + bytes(16 - len(payload)))

    def hold(self, moves, gap=0.004):
        self.report(0x00, bytes.fromhex('00d8'))
        for dx, dy in moves:
            self.report(0x10, dx.to_bytes(2, 'big', signed=True) + dy.to_bytes(2, 'big', signed=True))
            time.sleep(gap)
        self.report(0x00, b'')

    def pulses(self):
        return sum(1 for m in self.requests if len(m) == 20 and m[2] == PRESENTER and m[3] == 0x1d)


def scenario(index, long_only):
    print(f'== {"Bluetooth (index 0xff, long reports only)" if long_only else "USB receiver (index 0x01)"}')
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    fake = FakeRemote(b, index, long_only)
    fake.start()
    steps = [cal.Step(180, True, 'fast'), cal.Step(180, False, 'fast')]
    out, notes = io.StringIO(), []
    result = {}

    def worker():
        result['accepted'] = cal.run(cal.Link(a.fileno(), index, long_only), steps, out,
                                     lambda t, body: notes.append(body), idle_seconds=20, emit=lambda s: None)

    t = threading.Thread(target=worker)
    t.start()

    def wait_pulses(n, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end and fake.pulses() < n:
            time.sleep(0.02)
        return fake.pulses() == n

    clockwise = [(150, 2)] * 30
    time.sleep(0.5)    # handshake
    fake.hold([(10, 0)] * 3)
    check('a hold that barely moved is rejected with two pulses', wait_pulses(2), f'{fake.pulses()} pulses')
    fake.hold([(150, 0)] * 15 + [(-150, 0)] * 15)
    check('turning back and forth is rejected', wait_pulses(4), f'{fake.pulses()} pulses')
    fake.hold([(150, 90)] * 30)
    check('a tilted remote is rejected', wait_pulses(6), f'{fake.pulses()} pulses')
    fake.hold(clockwise)
    check('a clean clockwise turn is accepted with one pulse', wait_pulses(7), f'{fake.pulses()} pulses')
    fake.hold(clockwise)
    check('the wrong direction for the counter-clockwise step is rejected', wait_pulses(9), f'{fake.pulses()} pulses')
    fake.hold([(-150, 3)] * 30)
    check('a clean counter-clockwise turn is accepted and three pulses announce the end', wait_pulses(12), f'{fake.pulses()} pulses')
    t.join(5)
    check('the run ends by itself with both turns accepted', not t.is_alive() and result.get('accepted') == 2, str(result))

    rows = [json.loads(l) for l in out.getvalue().splitlines()]
    check('every hold is recorded, with its reason', [r['accepted'] for r in rows] == [False, False, False, True, False, True], str([r['reason'] for r in rows]))
    good = [r for r in rows if r['accepted']]
    check('the sum of the counts is recorded', [r['sum_dx'] for r in good] == [4500, -4500], str([r['sum_dx'] for r in good]))
    check('the raw reports are kept', len(good[0]['reports_xy']) == 30)
    check('the notification names the turn', any('180 degrees CLOCKWISE' in n for n in notes), str(notes[:1]))
    undivert = [m for m in fake.requests if len(m) == 20 and m[2] == REPROG and m[3] == 0x3d and m[6] == cal.UNDIVERT]
    check('the action button is released again at the end', len(undivert) == 1, str(len(undivert)))
    divert = [m for m in fake.requests if len(m) == 20 and m[2] == REPROG and m[3] == 0x3d and m[6] == cal.DIVERT_WITH_RAW_XY]
    check('the hold is diverted with raw X/Y at the start', len(divert) == 1, str(len(divert)))
    check('all requests carry the device index', all(m[1] == index for m in fake.requests))
    fake.stop = True


if __name__ == '__main__':
    scenario(0x01, False)
    scenario(0xff, True)
    print('FAILED: ' + ', '.join(failures) if failures else 'all checks passed')
    sys.exit(1 if failures else 0)
