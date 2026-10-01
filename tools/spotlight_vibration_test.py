#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Blind vibration test for the original Logitech Spotlight (USB receiver).

Plays 12 pulses (lengths 1, 2, 3 at four strengths) in a random order that is NOT shown while it runs.
You rate each pulse by feel; the order and parameters are written to --log for later.

    tools/spotlight_vibration_test.py --log vib.log

One pulse per button press, at your own pace: NEXT plays the next pulse, BACK repeats the last one.
The presses are swallowed (nothing reaches the desktop). 3 short pulses = finished.
Rating scale: 0 none, 1 faint, 2 clear, 3 strong, 4 too strong.
Stop projecteur first (`projecteur -c quit`).
"""
import argparse
import fcntl
import os
import random
import select
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spotlight_events import EVENT, EVIOCGRAB, EV_KEY, spotlight_nodes   # noqa: E402
from spotlight_hidpp import DEVICE_INDEX, PRESENTER_CONTROL, SW_ID, Hidpp, find_hidraw   # noqa: E402

KEY_RIGHT = 106
KEY_LEFT = 105
LENGTHS = (1, 2, 3)
INTENSITIES = (0x40, 0x80, 0xc0, 0xff)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--log', required=True, help='file that receives the (hidden) trial order')
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()

    nid = {'id': None}

    def notify(title, body):
        cmd = ['notify-send', '-u', 'critical', '-a', 'Spotlight tools', '-p']
        if nid['id']:
            cmd += ['-r', nid['id']]
        r = subprocess.run(cmd + [title, body], capture_output=True, text=True)
        nid['id'] = r.stdout.strip() or nid['id']

    nodes = [n for n in spotlight_nodes() if n[1] == 'Logitech USB Receiver']      # the keyboard node
    if not nodes:
        sys.exit('No Spotlight keyboard node found (USB receiver plugged in?).')
    fd = os.open(nodes[0][0], os.O_RDONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, EVIOCGRAB, 1)

    def wait_for_key():
        """Block until NEXT or BACK is pressed; return 'next' or 'back'."""
        while True:
            if select.select([fd], [], [], 1.0)[0]:
                data = os.read(fd, EVENT.size * 32)
                for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    _, _, typ, code, val = struct.unpack_from('llHHi', data, i)
                    if typ == EV_KEY and val == 1 and code in (KEY_RIGHT, KEY_LEFT):
                        return 'next' if code == KEY_RIGHT else 'back'

    notify('Vibration test: ready', 'Hold the remote and press NEXT for the first pulse. '
           'Rate: 0 none, 1 faint, 2 clear, 3 strong, 4 too strong.')
    wait_for_key()                                                 # the first press also wakes the remote

    dev = Hidpp(find_hidraw())

    def feature_index():
        for _ in range(20):                                        # a sleeping remote ignores the first requests
            ans = dev.request([0x10, DEVICE_INDEX, 0x00, SW_ID, PRESENTER_CONTROL >> 8, PRESENTER_CONTROL & 0xff, 0x00])
            if ans and ans[4]:
                return ans[4]
        return 0

    idx = feature_index()
    if not idx:
        notify('Vibration test failed', 'The remote did not answer.')
        sys.exit('no answer from the remote')

    def pulse(length, intensity):
        dev.send([0x11, DEVICE_INDEX, idx, 0x10 | SW_ID, length, 0xe8, intensity] + [0] * 13)

    trials = [(l, i) for l in LENGTHS for i in INTENSITIES]
    random.Random(a.seed).shuffle(trials)
    with open(a.log, 'a') as log:
        log.write(f'# order of trials (length, intensity), seed={a.seed}\n')
        for n, (l, i) in enumerate(trials, 1):
            log.write(f'trial {n}: length {l} intensity {i:#04x}\n')
        log.flush()

        n, first = 0, True
        while n < len(trials):
            if not first:
                key = wait_for_key()
                if key == 'back' and n > 0:                        # repeat the previous pulse
                    n -= 1
                    log.write(f'{time.strftime("%H:%M:%S")} repeated trial {n + 1}\n')
                if not feature_index():                            # re-wake the remote if it dozed off
                    continue
            first = False
            l, i = trials[n]
            n += 1
            pulse(l, i)
            log.write(f'{time.strftime("%H:%M:%S")} played trial {n}\n')
            log.flush()
            notify(f'Vibration test: pulse {n} of {len(trials)}',
                   'Rate it (0 none, 1 faint, 2 clear, 3 strong, 4 too strong). NEXT = next pulse, BACK = repeat this one.')
        while True:                                                # the last pulse can still be repeated (BACK)
            if wait_for_key() == 'next':
                break
            if feature_index():
                pulse(*trials[-1])
                log.write(f'{time.strftime("%H:%M:%S")} repeated trial {len(trials)}\n')
                log.flush()
        for _ in range(3):
            pulse(2, 0x80)
            time.sleep(0.5)
    os.close(fd)
    notify('Vibration test: finished', f'Please send your {len(trials)} ratings in order, e.g. "0 1 1 2 ...".')


if __name__ == '__main__':
    main()
