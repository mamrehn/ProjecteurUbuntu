#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Guided calibration of the Spotlight's movement counts (counts per degree of turn).

While the action button is held, the remote reports how it moves as "raw X/Y" counts. This tool asks for a fixed
list of turns (lay the remote flat on a table, hold the action button, turn it by 90 or 180 degrees, release) and
checks every hold itself, so no bookkeeping is left to you:

    1 short pulse   the hold was accepted, do the next turn
    2 short pulses  the hold was rejected (it says why in the terminal and in the notification): repeat the same turn
    3 short pulses  all turns are done

Every hold, accepted or not, is appended to a JSON lines file with its raw reports, for later analysis.

    tools/spotlight_calibrate.py --out calibration.jsonl

Works over the USB receiver and over Bluetooth. Stop projecteur / projecteurd first; needs a local desktop session
(access to the hidraw node).
"""
import argparse
import collections
import glob
import json
import os
import select
import signal
import subprocess
import sys
import time

SW_ID = 0x0d
REPROG_V4 = 0x1b04
PRESENTER_CONTROL = 0x1a00
CID_HOLD = 0x00d8
DIVERT_WITH_RAW_XY, UNDIVERT = 0x33, 0x22

Step = collections.namedtuple('Step', 'angle clockwise speed')

# How long the turn itself may take (seconds between 5 % and 95 % of the movement), per speed class.
SPEEDS = {'slow': (3.0, 15.0), 'medium': (1.0, 4.0), 'fast': (0.0, 1.0)}
SPEED_TEXT = {'slow': 'SLOW (about 6 s)', 'medium': 'MEDIUM (about 2 s)', 'fast': 'FAST (under 0.5 s)'}

STEPS = ([Step(180, True, s) for s in ('slow', 'medium', 'fast') for _ in range(2)]
         + [Step(180, False, 'medium')] * 2
         + [Step(90, True, s) for s in ('slow', 'medium', 'fast') for _ in range(2)]
         + [Step(90, False, 'medium')] * 2)

MIN_COUNTS = 300            # a real turn gives thousands
MIN_REPORTS = 10
MIN_STRAIGHTNESS = 0.8      # |net| / path: below this the remote was turned back and forth
MAX_OFF_AXIS = 0.3          # |sum dy| / |sum dx|: above this the remote was tilted, not turned flat


def describe(step):
    return f'{step.angle} degrees {"CLOCKWISE" if step.clockwise else "COUNTER-CLOCKWISE"}, {SPEED_TEXT[step.speed]}'


def find_remote():
    """(hidraw path, device index, long messages only, kind) of the Spotlight, or None."""
    for node in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        dev = os.path.realpath(os.path.join(node, 'device'))
        try:
            uevent = open(os.path.join(dev, 'uevent')).read().upper()
        except OSError:
            continue
        path = '/dev/' + os.path.basename(node)
        if 'HID_ID=0005:0000046D:0000B503' in uevent:
            return path, 0xff, True, 'Bluetooth'
        if '0000046D:0000C53E' in uevent:
            try:
                iface = open(os.path.join(os.path.dirname(dev), 'bInterfaceNumber')).read().strip()
            except OSError:
                continue
            if iface == '02':
                return path, 0x01, False, 'USB receiver'
    return None


def running_projecteur():
    for comm in glob.glob('/proc/[0-9]*/comm'):
        try:
            if open(comm).read().strip() in ('projecteur', 'projecteurd'):
                return comm.split('/')[2]
        except OSError:
            pass
    return None


def hexs(b):
    return ' '.join(f'{x:02x}' for x in b)


class Link:
    """HID++ on a hidraw file descriptor. `long_only`: the Bluetooth node accepts only 20 byte reports."""

    def __init__(self, fd, index, long_only):
        self.fd, self.index, self.long_only = fd, index, long_only

    def message(self, feature_index, function, params=(), long=True):
        size = 20 if (long or self.long_only) else 7
        m = bytearray(size)
        m[0:4] = bytes([0x11 if size == 20 else 0x10, self.index, feature_index, (function << 4) | SW_ID])
        m[4:4 + len(params)] = bytes(params)
        return bytes(m)

    def send(self, data):
        os.write(self.fd, data)

    def read(self, timeout):
        if not select.select([self.fd], [], [], max(timeout, 0))[0]:
            return None
        return os.read(self.fd, 64)

    def request(self, data, timeout=1.0):
        """The answer to `data`, or None. The first requests to a sleeping remote go unanswered."""
        self.send(data)
        end = time.time() + timeout
        while time.time() < end:
            msg = self.read(end - time.time())
            if not msg or msg[0] not in (0x10, 0x11):
                continue
            if msg[2] == 0xff and msg[3] == data[2]:
                raise RuntimeError(f'the remote rejected {hexs(data[:7])}: {hexs(msg[:8])}')
            if msg[1] == data[1] and msg[2] == data[2] and msg[3] == data[3]:
                return msg
        return None

    def feature_index(self, feature, attempts=30):
        for attempt in range(attempts):
            ans = self.request(self.message(0x00, 0, (feature >> 8, feature & 0xff), long=False))
            if ans:
                return ans[4]
            if attempt == 0:
                print('the remote does not answer yet (asleep?). Press any button on it; retrying for 30 s ...', flush=True)
        return 0


class Notifier:
    """One critical notification, updated in place (critical so Do-Not-Disturb cannot hide it)."""

    def __init__(self):
        self.id = None

    def show(self, title, body):
        cmd = ['notify-send', '-u', 'critical', '-p', '-a', 'Spotlight calibration']
        if self.id:
            cmd += ['-r', self.id]
        try:
            out = subprocess.run(cmd + [title, body], capture_output=True, text=True, timeout=5).stdout.strip()
            self.id = out if out.isdigit() else self.id
        except (OSError, subprocess.SubprocessError):
            pass


def turn_time(reports):
    """Seconds between 5 % and 95 % of the total movement: how long the turn itself took."""
    path = sum(abs(r[1]) for r in reports) or 1
    done, t_lo, t_hi = 0, reports[0][0], reports[-1][0]
    for t, dx, _ in reports:
        done += abs(dx)
        if done <= 0.05 * path:
            t_lo = t
        if done <= 0.95 * path:
            t_hi = t
    return t_hi - t_lo


def evaluate(reports, step, speeds, cw_sign):
    """(accepted, reason, metrics) for one hold."""
    n = len(reports)
    dxs = [r[1] for r in reports]
    net, path, off_axis = sum(dxs), sum(abs(x) for x in dxs), sum(r[2] for r in reports)
    metrics = {'reports': n, 'sum_dx': net, 'sum_dy': off_axis, 'path_dx': path}
    if n < MIN_REPORTS or abs(net) < MIN_COUNTS:
        return False, 'barely moved: keep the action button held while you turn the remote', metrics
    metrics['turn_seconds'] = round(turn_time(reports), 3)
    if abs(net) / path < MIN_STRAIGHTNESS:
        return False, 'turned back and forth: turn one way only, then release', metrics
    if abs(off_axis) > MAX_OFF_AXIS * abs(net):
        return False, 'tilted: keep the remote flat on the table and turn it like a clock hand', metrics
    lo, hi = speeds[step.speed]
    if metrics['turn_seconds'] < lo:
        return False, f'too fast ({metrics["turn_seconds"]:.1f} s): turn it more slowly', metrics
    if metrics['turn_seconds'] > hi:
        return False, f'too slow ({metrics["turn_seconds"]:.1f} s): turn it faster', metrics
    want = cw_sign if step.clockwise else -cw_sign if cw_sign else 0
    if want and (net > 0) != (want > 0):
        return False, 'wrong direction', metrics
    return True, 'ok', metrics


def run(link, steps, out, notify, speeds=SPEEDS, idle_seconds=600, emit=print):
    """Divert the action button with raw X/Y, collect one hold per step. Returns the number of accepted holds."""
    reprog = link.feature_index(REPROG_V4)
    if not reprog:
        raise RuntimeError('the remote has no ReprogramControlsV4 (or is out of range)')
    presenter = link.feature_index(PRESENTER_CONTROL, attempts=3)

    def pulses(count):
        for i in range(count):
            if presenter:
                link.send(link.message(presenter, 1, (1, 0xe8, 0x80)))   # length 1: a short pulse
            if i + 1 < count:
                time.sleep(0.45)

    def divert(flags):
        link.send(link.message(reprog, 3, (CID_HOLD >> 8, CID_HOLD & 0xff, flags)))

    link.request(link.message(reprog, 3, (CID_HOLD >> 8, CID_HOLD & 0xff, DIVERT_WITH_RAW_XY)))
    accepted, index, cw_sign = 0, 0, 0
    holding, reports, t0 = False, [], 0.0
    last_activity = time.time()

    def announce():
        text = f'Turn {index + 1} of {len(steps)}: {describe(steps[index])}'
        emit(text)
        notify('Spotlight calibration', text + '\nHold the action button, turn, release. 1 pulse = ok, 2 = repeat, 3 = done.')

    try:
        announce()
        while index < len(steps) and time.time() - last_activity < idle_seconds:
            msg = link.read(0.2)
            if not msg or msg[0] not in (0x10, 0x11) or len(msg) < 8 or msg[2] != reprog or (msg[3] & 0x0f) != 0:
                continue
            last_activity = time.time()
            kind = msg[3] >> 4
            if kind == 0:
                pressed = {(msg[i] << 8) | msg[i + 1] for i in range(4, min(len(msg), 12), 2)}
                if CID_HOLD in pressed and not holding:
                    holding, reports, t0 = True, [], time.time()
                elif CID_HOLD not in pressed and holding:
                    holding = False
                    if not reports:
                        continue   # pressed without moving: not an attempt
                    ok, reason, metrics = evaluate(reports, steps[index], speeds, cw_sign)
                    record = dict(step=index + 1, of=len(steps), want=describe(steps[index]), accepted=ok, reason=reason,
                                  hold_seconds=round(time.time() - t0, 3), **metrics,
                                  reports_xy=[[round(t - t0, 3), dx, dy] for t, dx, dy in reports])
                    out.write(json.dumps(record) + '\n')
                    out.flush()
                    if ok:
                        if not cw_sign and steps[index].clockwise:
                            cw_sign = 1 if metrics['sum_dx'] > 0 else -1
                        accepted += 1
                        index += 1
                        emit(f'  accepted: {metrics["sum_dx"]:+d} counts in {metrics["turn_seconds"]:.1f} s')
                        if index < len(steps):
                            pulses(1)
                            announce()
                    else:
                        emit(f'  rejected: {reason}')
                        pulses(2)
                        notify('Spotlight calibration: repeat', f'{reason}\nTurn {index + 1} of {len(steps)}: {describe(steps[index])}')
            elif kind == 1 and holding:
                reports.append((time.time(), int.from_bytes(msg[4:6], 'big', signed=True),
                                int.from_bytes(msg[6:8], 'big', signed=True)))
        if index >= len(steps):
            pulses(3)
            notify('Spotlight calibration', 'All turns done. Thank you.')
            emit('all turns done')
        else:
            emit('stopped: no activity for too long')
    finally:
        try:
            divert(UNDIVERT)
        except OSError:
            pass
    return accepted


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default='calibration.jsonl', help='append every hold (raw reports included) to this JSON lines file')
    ap.add_argument('--list', action='store_true', help='print the list of turns and exit')
    a = ap.parse_args()
    if a.list:
        for i, s in enumerate(STEPS, 1):
            print(f'{i:2d}. {describe(s)}')
        return
    pid = running_projecteur()
    if pid:
        sys.exit(f'projecteur / projecteurd is running (pid {pid}); stop it first.')
    found = find_remote()
    if not found:
        sys.exit('No Spotlight found (USB receiver plugged in, or the remote connected over Bluetooth? Press a button on it).')
    path, index, long_only, kind = found
    print(f'{kind}: {path}', flush=True)
    try:
        fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
    except PermissionError:
        sys.exit(f'No permission for {path} (local desktop session needed).')
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    notifier = Notifier()
    try:
        with open(a.out, 'a') as out:
            n = run(Link(fd, index, long_only), STEPS, out, notifier.show)
        print(f'{n} of {len(STEPS)} turns recorded in {a.out}')
    except KeyboardInterrupt:
        print('interrupted; the action button is released again')
    except RuntimeError as e:
        sys.exit(str(e))


if __name__ == '__main__':
    main()
