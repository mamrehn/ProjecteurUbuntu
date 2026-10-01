#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Listen to the Spotlight's HID++ button events (action button click / double click, Next / Back hold).

The action (top) button sends nothing a normal input device would show: it only makes the Spotlight stream
pointer motion. Its click and double-click exist as HID++ events once they are "diverted" (feature 0x1b04,
ReprogramControlsV4). This tool does exactly that, temporarily (reset when the device power-cycles; nothing is
stored in the device), and prints every HID++ message that arrives.

    tools/spotlight_hidpp.py                    # divert click (0xd8) and double click (0xdf), print events
    tools/spotlight_hidpp.py --cids d8 df da dc # also Next / Back hold
    tools/spotlight_hidpp.py --seconds 60 --out hidpp.log --notify

Stop projecteur first (`projecteur -c quit`). Works over the USB receiver; needs a local desktop session.
"""
import argparse
import glob
import os
import select
import subprocess
import sys
import time

DEVICE_INDEX = 0x01          # the Spotlight behind the receiver
SW_ID = 0x0d                 # our software id in the low nibble of byte 3
REPROG_V4 = 0x1b04
PRESENTER_CONTROL = 0x1a00     # original Spotlight: vibration
KNOWN_CIDS = {0xd8: 'action button CLICK', 0xdf: 'action button DOUBLE CLICK', 0xda: 'Next hold', 0xdc: 'Back hold'}


def find_hidraw():
    """The vendor HID++ interface (interface 2) of the receiver 046d:c53e, via sysfs."""
    for node in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        dev = os.path.realpath(os.path.join(node, 'device'))
        try:
            uevent = open(os.path.join(dev, 'uevent')).read().upper()
            iface = open(os.path.join(os.path.dirname(dev), 'bInterfaceNumber')).read().strip()
        except OSError:
            continue
        if '0000046D:0000C53E' in uevent and iface == '02':
            return '/dev/' + os.path.basename(node)
    return None


def hexs(b):
    return ' '.join(f'{x:02x}' for x in b)


class Hidpp:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)

    def send(self, data):
        os.write(self.fd, bytes(data))

    def read(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        return os.read(self.fd, 64) if r else None

    def request(self, data, timeout=1.0):
        """Send a request and return the matching answer (same feature index + function/swid), or None."""
        self.send(data)
        end = time.time() + timeout
        while time.time() < end:
            msg = self.read(end - time.time())
            if not msg:
                continue
            if msg[2] == 0xff and msg[3] == data[2]:                      # HID++ 2.0 error
                raise RuntimeError(f'device returned error {msg[5]:#x} for {hexs(data[:7])}')
            if msg[1] == data[1] and msg[2] == data[2] and msg[3] == data[3]:
                return msg
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cids', nargs='+', default=['d8:33', 'df:03'],
                    help='controls to divert as CID[:FLAGS] in hex (default d8:33 df:03). FLAGS: 01 divert, 02 valid, '
                         '10 also divert raw X/Y (movement arrives as HID++, the system pointer stays still), 20 valid')
    ap.add_argument('--seconds', type=float, default=0, help='stop after this many seconds (default: Ctrl+C)')
    ap.add_argument('--out', help='also append to this file')
    ap.add_argument('--no-buzz', action='store_true', help='do not vibrate the remote for every button event')
    ap.add_argument('--notify', action='store_true', help='one (critical, so Do-Not-Disturb cannot hide it) notification when the listener is ready')
    a = ap.parse_args()

    path = find_hidraw()
    if not path:
        sys.exit('No Spotlight receiver HID++ interface found (USB receiver plugged in? Bluetooth is not supported by this tool).')
    out = open(a.out, 'a') if a.out else None

    def emit(msg):
        line = f'{time.strftime("%H:%M:%S")}.{int(time.time() * 1000) % 1000:03d} {msg}'
        print(line, flush=True)
        if out:
            out.write(line + '\n')
            out.flush()

    try:
        dev = Hidpp(path)
    except PermissionError:
        sys.exit(f'No permission for {path} (local desktop session needed, projecteur must not be running).')

    # 1. which feature index does ReprogramControlsV4 have on this device?
    # (a sleeping remote does not answer the first request: keep asking for a while, it wakes on a button press)
    ans = None
    for attempt in range(30):
        ans = dev.request([0x10, DEVICE_INDEX, 0x00, SW_ID, REPROG_V4 >> 8, REPROG_V4 & 0xff, 0x00])
        if ans:
            break
        if attempt == 0:
            emit('the remote does not answer yet (asleep?). Press any button on it; retrying for 30 s ...')
    if not ans or ans[4] == 0:
        sys.exit('The device did not answer / has no ReprogramControlsV4. Is it in range and switched on?')
    idx = ans[4]
    emit(f'{path}: ReprogramControlsV4 is feature index {idx:#04x}')

    # vibration = feedback that no desktop setting (Do Not Disturb, muted speakers) can hide
    ans = dev.request([0x10, DEVICE_INDEX, 0x00, SW_ID, PRESENTER_CONTROL >> 8, PRESENTER_CONTROL & 0xff, 0x00])
    pc_idx = ans[4] if ans else 0

    def buzz():
        # Same long (20 byte) message Projecteur sends: {dev, idx, function 1 | swid, length 0..10, 0xe8, intensity}
        if pc_idx and not a.no_buzz:
            dev.send([0x11, DEVICE_INDEX, pc_idx, 0x10 | SW_ID, 1, 0xe8, 0x80] + [0] * 13)   # length 0: not felt, 2: clearly felt, 3 + 0xc0: strong and long

    # 2. divert the wanted controls: flags 0x03 = divert now (temporary), not stored in the device
    cids, flags_of = [], {}
    for spec in a.cids:
        cid_s, _, fl_s = spec.partition(':')
        cid = int(cid_s, 16)
        cids.append(cid)
        flags_of[cid] = int(fl_s, 16) if fl_s else 0x03
    for cid in cids:
        r = dev.request([0x11, DEVICE_INDEX, idx, 0x30 | SW_ID, cid >> 8, cid & 0xff, flags_of[cid]] + [0] * 13)
        emit(f'divert {cid:#06x} ({KNOWN_CIDS.get(cid, "?")}) with flags {flags_of[cid]:#04x}: ' + ('acknowledged' if r else 'NO ANSWER'))

    emit(f'listening; the remote buzzes for each HID++ button event listed below (a plain mouse click is NOT one)'
         f'{"" if pc_idx and not a.no_buzz else " (vibration unavailable)"}. Ctrl+C to stop')
    if a.notify:
        subprocess.Popen(['notify-send', '-u', 'critical', '-a', 'Spotlight tools', 'Spotlight HID++ listener is ready',
                          'Click, double-click and hold the action button. The remote buzzes only for HID++ events (double-click, hold); a plain mouse click does not buzz.'],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end = time.time() + a.seconds if a.seconds else None
    held = set()
    raw_n = raw_dx = raw_dy = 0
    raw_since = 0.0
    try:
        while end is None or time.time() < end:
            msg = dev.read(0.2)
            if not msg:
                if raw_n and time.time() - raw_since > 0.4:
                    emit(f'  HID++ raw X/Y movement x{raw_n} over {time.time() - raw_since:.2f}s, sum dx={raw_dx} dy={raw_dy}')
                    raw_n = raw_dx = raw_dy = 0
                continue
            note = ''
            # notification of ReprogramControlsV4: event 0 = list of currently pressed diverted controls
            if len(msg) >= 4 and msg[0] in (0x10, 0x11) and msg[2] == idx and msg[3] >> 4 == 0 and (msg[3] & 0x0f) == 0:
                pressed = set()
                for i in range(4, min(len(msg), 12), 2):
                    cid = (msg[i] << 8) | msg[i + 1]
                    if cid:
                        pressed.add(cid)
                for cid in sorted(pressed - held):
                    note += f'  >> {KNOWN_CIDS.get(cid, hex(cid))} DOWN'
                for cid in sorted(held - pressed):
                    note += f'  >> {KNOWN_CIDS.get(cid, hex(cid))} UP'
                held = pressed
                if 'DOWN' in note:
                    buzz()
            if pc_idx and msg[2] == pc_idx and (msg[3] & 0x0f) == SW_ID:
                continue                                       # answer to our own vibrate request
            if len(msg) >= 8 and msg[0] == 0x11 and msg[2] == idx and msg[3] >> 4 == 1:     # diverted raw X/Y
                if not raw_n:
                    raw_since = time.time()
                raw_n += 1
                raw_dx += int.from_bytes(msg[4:6], 'big', signed=True)
                raw_dy += int.from_bytes(msg[6:8], 'big', signed=True)
                continue
            if raw_n:
                emit(f'  HID++ raw X/Y movement x{raw_n} over {time.time() - raw_since:.2f}s, sum dx={raw_dx} dy={raw_dy}')
                raw_n = raw_dx = raw_dy = 0
            emit(f'rx {hexs(msg[:12])}{note}')
    except KeyboardInterrupt:
        pass
    finally:
        # release the diversion again (flags 0x02 = "change valid", divert off)
        for cid in cids:
            try:
                dev.send([0x11, DEVICE_INDEX, idx, 0x30 | SW_ID, cid >> 8, cid & 0xff, 0x22] + [0] * 13)
            except OSError:
                pass
        emit('diversions released; done')


if __name__ == '__main__':
    main()
