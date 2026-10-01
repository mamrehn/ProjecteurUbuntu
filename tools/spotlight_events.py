#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Show what a Logitech Spotlight really sends: raw evdev events of all its input nodes.

Works for the USB receiver (046d:c53e) and Bluetooth (046d:b503). No extra packages needed.

    tools/spotlight_events.py                 # list the Spotlight input nodes
    tools/spotlight_events.py --watch         # print events until Ctrl+C
    tools/spotlight_events.py --watch --grab  # same, but keep the events away from the desktop
    tools/spotlight_events.py --watch --grab --notify   # ...and show a desktop notification per button press
    tools/spotlight_events.py --watch --seconds 120 --out events.log

Stop projecteur first (`projecteur -c quit`): it grabs the device exclusively, then nothing can be read.
Mouse movement is summarised ("moves x47 over 0.43 s") because the pointer button produces hundreds of
events per second.
"""
import argparse
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import time

EVENT = struct.Struct('llHHi')                    # struct input_event: time (sec, usec), type, code, value
EV_SYN, EV_KEY, EV_REL, EV_MSC = 0, 1, 2, 4
EVIOCGRAB = 0x40044590
IDS = {('046d', 'c53e'): 'USB receiver', ('046d', 'b503'): 'Bluetooth'}


def code_names():
    """code -> name, read from the kernel headers (KEY_*/BTN_*), first definition wins."""
    names = {}
    try:
        text = open('/usr/include/linux/input-event-codes.h').read()
    except OSError:
        return names
    for m in re.finditer(r'#define\s+((?:KEY|BTN)_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', text):
        names.setdefault(int(m.group(2), 0), m.group(1))
    return names


def spotlight_nodes():
    """[(path, device name, connection)] for every input node of a Spotlight."""
    found = []
    for block in open('/proc/bus/input/devices').read().split('\n\n'):
        m = re.search(r'Vendor=(\w+) Product=(\w+)', block)
        if not m or (m.group(1).lower(), m.group(2).lower()) not in IDS:
            continue
        name = re.search(r'Name="([^"]*)"', block).group(1)
        for ev in re.findall(r'\bevent(\d+)\b', block):
            found.append((f'/dev/input/event{ev}', name, IDS[(m.group(1).lower(), m.group(2).lower())]))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--watch', action='store_true', help='print events')
    ap.add_argument('--grab', action='store_true', help='grab the nodes exclusively (events do not reach the desktop)')
    ap.add_argument('--notify', action='store_true', help='show a desktop notification for every button press (needs notify-send)')
    ap.add_argument('--seconds', type=float, default=0, help='stop after this many seconds (default: until Ctrl+C)')
    ap.add_argument('--out', help='also append the output to this file')
    a = ap.parse_args()

    nodes = spotlight_nodes()
    if not nodes:
        sys.exit('No Spotlight input device found. Plugged in / awake / paired? (lsusb | grep 046d)')
    for path, name, conn in nodes:
        print(f'{path:18s} {name}  [{conn}]')
    if not a.watch:
        return

    out = open(a.out, 'a') if a.out else None
    names = code_names()

    def emit(msg):
        line = f'{time.strftime("%H:%M:%S")}.{int(time.time() * 1000) % 1000:03d} {msg}'
        print(line, flush=True)
        if out:
            out.write(line + '\n')
            out.flush()

    fds = {}
    for path, name, _ in nodes:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except PermissionError:
            sys.exit(f'No permission for {path}. (Is this a local desktop session? SSH sessions get no device ACLs.)')
        if a.grab:
            try:
                fcntl.ioctl(fd, EVIOCGRAB, 1)
            except OSError as e:
                sys.exit(f'Cannot grab {path}: {e}. Is projecteur still running?')
        fds[fd] = name.replace('Logitech ', '')
    emit(f'watching {len(fds)} nodes{" (grabbed: nothing reaches the desktop)" if a.grab else ""}; Ctrl+C to stop')

    rel, rel_since = 0, 0.0
    end = time.time() + a.seconds if a.seconds else None

    def flush_rel():
        nonlocal rel
        if rel:
            emit(f'  pointer motion x{rel} over {time.time() - rel_since:.2f}s')
            rel = 0

    try:
        while end is None or time.time() < end:
            ready, _, _ = select.select(list(fds), [], [], 0.2)
            for fd in ready:
                try:
                    data = os.read(fd, EVENT.size * 64)
                except BlockingIOError:
                    continue
                for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    _, _, typ, code, val = EVENT.unpack_from(data, i)
                    if typ == EV_REL:
                        if not rel:
                            rel_since = time.time()
                        rel += 1
                    elif typ == EV_KEY:
                        flush_rel()
                        state = {1: 'down', 0: 'up', 2: 'repeat'}.get(val, val)
                        emit(f'{fds[fd]:28s} {names.get(code, code)} {state}')
                        if a.notify and val == 1:
                            subprocess.Popen(['notify-send', '-t', '1500', '-h', 'string:x-canonical-private-synchronous:spotlight',
                                              'Spotlight', f'{names.get(code, code)} pressed'],
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    elif typ == EV_MSC and code == 4:     # MSC_SCAN: the raw HID usage, useful for odd keys
                        flush_rel()
                        emit(f'{fds[fd]:28s}   scancode {val:#x}')
            if rel and time.time() - rel_since > 0.4:
                flush_rel()
    except KeyboardInterrupt:
        pass
    finally:
        flush_rel()
        emit('done')


if __name__ == '__main__':
    main()
