#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Headless GNOME Shell test harness for the projecteur-overlay extension.

Boots a throwaway `gnome-shell --headless` (virtual 1280x720 monitor) inside a *private* D-Bus session
with fully isolated settings, loads the extension, drives it over D-Bus, takes screenshots of the
virtual monitor and asserts exact pixel values against a known test pattern.

Nothing here touches the real desktop session: own bus, own GSettings keyfile, own HOME/XDG dirs.

    tests/headless/run.py [--keep] [--only NAME]

--keep keeps the temp directory (screenshots, shell log) and prints its path.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parents[2]
UUID = 'projecteur-overlay@mamrehn.github.io'
EXT_SRC = REPO / 'gnome-shell' / UUID
W, H, TILE = 1280, 720, 80


def outer():
    """Re-exec inside a private session bus so the real one is never used."""
    os.execvp('dbus-run-session',
              ['dbus-run-session', '--', sys.executable, os.path.abspath(__file__), '--inner'] + sys.argv[1:])


# ---------------------------------------------------------------------------------------------
def tile_color(i, j):  # must match testTileColor() in overlay.js
    return ((i * 53 + 40) % 256, (j * 97 + 60) % 256, ((i + j) * 71 + 20) % 256)


def tile_at(x, y):
    return tile_color(int(x) // TILE, int(y) // TILE)


def tile_margin(x, y):
    """Distance in px from (x, y) to the nearest tile edge (to keep sample points off edges)."""
    return min(x % TILE, TILE - x % TILE, y % TILE, TILE - y % TILE)


def close(a, b, tol):
    return all(abs(p - q) <= tol for p, q in zip(a, b))


class Shot:
    def __init__(self, path):
        import gi
        gi.require_version('GdkPixbuf', '2.0')
        from gi.repository import GdkPixbuf
        pb = GdkPixbuf.Pixbuf.new_from_file(str(path))
        self.w, self.h, self.n, self.rs = pb.get_width(), pb.get_height(), pb.get_n_channels(), pb.get_rowstride()
        self.data = pb.get_pixels()

    def px(self, x, y):
        i = int(y) * self.rs + int(x) * self.n
        return (self.data[i], self.data[i + 1], self.data[i + 2])


# ---------------------------------------------------------------------------------------------
class Harness:
    def __init__(self, keep):
        from gi.repository import Gio, GLib
        self.Gio, self.GLib = Gio, GLib
        self.keep = keep
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix='pj-headless-'))
        self.results = []
        self.proc = None
        self.bus = None

    # -- lifecycle -------------------------------------------------------------------------
    def start(self):
        Gio, GLib = self.Gio, self.GLib
        t = self.tmp
        for d in ('home', 'config/glib-2.0/settings', 'data/gnome-shell/extensions', 'cache', 'runtime'):
            (t / d).mkdir(parents=True, exist_ok=True)
        (t / 'runtime').chmod(0o700)
        (t / 'data/gnome-shell/extensions' / UUID).symlink_to(EXT_SRC)
        (t / 'config/glib-2.0/settings/keyfile').write_text(
            f"[org/gnome/shell]\nenabled-extensions=['{UUID}']\n")

        env = dict(os.environ)
        for k in ('WAYLAND_DISPLAY', 'DISPLAY', 'GNOME_SETUP_DISPLAY'):
            env.pop(k, None)
        env.update({
            'HOME': str(t / 'home'), 'XDG_CONFIG_HOME': str(t / 'config'),
            'XDG_DATA_HOME': str(t / 'data'), 'XDG_CACHE_HOME': str(t / 'cache'),
            'XDG_RUNTIME_DIR': str(t / 'runtime'),
            'GSETTINGS_BACKEND': 'keyfile', 'PROJECTEUR_OVERLAY_TESTING': '1',
        })
        self.log = open(t / 'shell.log', 'wb')
        self.proc = subprocess.Popen(
            ['gnome-shell', '--headless', '--wayland', '--no-x11', '--virtual-monitor', f'{W}x{H}'],
            env=env, stdout=self.log, stderr=subprocess.STDOUT)

        self.bus = self.connect_private_bus()
        deadline = time.time() + 60
        while not self.name_has_owner('org.projecteur.Overlay'):
            if self.proc.poll() is not None:
                raise RuntimeError(f'gnome-shell exited during startup (exit status {self.proc.returncode}; negative = killed by signal, -11 = SIGSEGV)')
            if time.time() > deadline:
                names = self.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                           'ListNames', None, None, 0, 2000, None).unpack()[0]
                shown = sorted(n for n in names if not n.startswith(':'))
                raise RuntimeError('extension never appeared on the bus (see shell.log); well-known names: ' + ', '.join(shown))
            time.sleep(0.4)

        # Screenshot() is normally restricted to system services; the extension enables the shell's
        # unsafe mode in test builds (PROJECTEUR_OVERLAY_TESTING=1), which lifts that check.
        time.sleep(1.0)  # let the first frames render

    def connect_private_bus(self):
        """Connect to the private bus created by dbus-run-session, and prove that it is private.

        Do NOT use Gio.bus_get_sync(SESSION) here: with PyGObject on this system it returns the user's
        REAL session bus (/run/user/UID/bus) even when DBUS_SESSION_BUS_ADDRESS points elsewhere (GJS
        and gdbus honour the variable, Python does not). Connect to the address explicitly instead.
        """
        Gio = self.Gio
        addr = os.environ.get('DBUS_SESSION_BUS_ADDRESS', '')
        real = f'/run/user/{os.getuid()}/bus'
        if not addr or real in addr:
            raise SystemExit(f'refusing to run: DBUS_SESSION_BUS_ADDRESS is not a private bus ({addr!r})')
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        bus = Gio.DBusConnection.new_for_address_sync(addr, flags, None, None)
        names = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                              'ListNames', None, None, 0, 2000, None).unpack()[0]
        foreign = [n for n in names if not n.startswith(':') and n != 'org.freedesktop.DBus']
        if foreign:
            raise SystemExit('refusing to run: the bus is not empty, it may be the real session bus: '
                             + ', '.join(foreign[:6]))
        return bus

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.keep:
            print(f'kept: {self.tmp}')
        else:
            shutil.rmtree(self.tmp, ignore_errors=True)

    # -- D-Bus helpers ---------------------------------------------------------------------
    def name_has_owner(self, name):
        r = self.bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                               'NameHasOwner', self.GLib.Variant('(s)', (name,)), None, 0, 2000, None)
        return r.unpack()[0]

    def call(self, method, sig='', *args):
        GLib = self.GLib
        params = GLib.Variant(f'({sig})', args) if sig else None
        r = self.bus.call_sync('org.projecteur.Overlay', '/org/projecteur/Overlay', 'org.projecteur.Overlay1',
                               method, params, None, self.Gio.DBusCallFlags.NONE, 15000, None)
        return r.unpack()

    def wait_name(self, name, present, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if self.name_has_owner(name) == present:
                return True
            time.sleep(0.1)
        return False

    def shell_extension(self, method):
        r = self.bus.call_sync('org.gnome.Shell', '/org/gnome/Shell', 'org.gnome.Shell.Extensions', method,
                               self.GLib.Variant('(s)', (UUID,)), None, 0, 10000, None)
        return r.unpack()[0]

    def cpu_ticks(self):
        f = pathlib.Path(f'/proc/{self.proc.pid}/stat').read_text().rsplit(')', 1)[1].split()
        return int(f[11]) + int(f[12])  # utime + stime

    def shot(self, name):
        path = self.tmp / f'{name}.png'
        time.sleep(0.5)
        r = self.bus.call_sync('org.gnome.Shell.Screenshot', '/org/gnome/Shell/Screenshot',
                               'org.gnome.Shell.Screenshot', 'Screenshot',
                               self.GLib.Variant('(bbs)', (False, False, str(path))), None, 0, 30000, None)
        ok = r.unpack()[0]
        if not ok:
            raise RuntimeError('Screenshot() reported failure')
        s = Shot(path)
        assert (s.w, s.h) == (W, H), f'unexpected screenshot size {s.w}x{s.h}'
        return s

    # -- reporting -------------------------------------------------------------------------
    def check(self, name, ok, detail=''):
        self.results.append((name, bool(ok), detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f'  ({detail})' if detail else ''))


# ---------------------------------------------------------------------------------------------
def run_tests(h):
    import json

    print('== boot')
    state = h.call('GetState')
    h.check('extension loaded and reachable over D-Bus', True)
    h.check('initial state hidden at monitor centre', state == (False, 'highlight', W / 2, H / 2), str(state))

    print('== test pattern (validates harness, screenshots and formula)')
    h.call('TestPattern', 'b', True)
    base = h.shot('pattern')
    for (i, j) in ((2, 1), (9, 5), (15, 8)):
        got, want = base.px(i * TILE + 40, j * TILE + 40), tile_color(i, j)
        h.check(f'tile ({i},{j}) colour', close(got, want, 2), f'got {got} want {want}')

    print('== highlight')
    h.call('Show', 's', 'highlight')
    h.call('MoveTo', 'dd', 650.0, 370.0)
    s = h.shot('highlight')
    inside, outside = (690, 390), (950, 470)
    h.check('inside the hole: pixels unchanged', close(s.px(*inside), tile_at(*inside), 3),
            f'got {s.px(*inside)} want {tile_at(*inside)}')
    want = tuple(round(c * 0.2) for c in tile_at(*outside))
    h.check('outside the hole: dimmed to 20 % (contrast 80 %)', close(s.px(*outside), want, 4),
            f'got {s.px(*outside)} want {want}')

    print('== laser')
    h.call('Show', 's', 'laser')
    h.call('MoveTo', 'dd', 300.0, 200.0)
    s = h.shot('laser')
    h.check('laser dot centre is red', close(s.px(300, 200), (255, 32, 32), 6), f'got {s.px(300, 200)}')
    far = (340, 200)
    h.check('pixels away from the dot untouched', close(s.px(*far), tile_at(*far), 2))

    print('== live magnifier (zoom 2, centre 650,370)')
    cx, cy, z = 650, 370, 2
    h.call('Show', 's', 'magnify')
    h.call('MoveTo', 'dd', float(cx), float(cy))
    s = h.shot('magnify')
    got = s.px(cx, cy)
    h.check('lens centre shows the pixel under the pointer', close(got, tile_at(cx, cy), 3), f'got {got}')
    # Discriminating points: the magnified source tile must differ from the plain-screen tile there,
    # otherwise the check could not tell a lens from no lens.
    for dx, dy in ((100, 20), (-120, 30), (110, -70), (-100, -80)):
        sx, sy = cx + dx / z, cy + dy / z
        assert tile_margin(sx, sy) >= 6, f'test point ({sx},{sy}) too close to a tile edge'
        got, want, wrong = s.px(cx + dx, cy + dy), tile_at(sx, sy), tile_at(cx + dx, cy + dy)
        assert want != wrong, f'point ({dx},{dy}) does not discriminate lens from no lens'
        h.check(f'lens offset ({dx:+d},{dy:+d}) shows the magnified source', close(got, want, 3) and not close(got, wrong, 3),
                f'got {got} want {want} (unmagnified would be {wrong})')
    corner = (cx + 140, cy + 140)
    h.check('lens corner outside the circle is cut away (not magnified)', close(s.px(*corner), tile_at(*corner), 3),
            f'got {s.px(*corner)} want {tile_at(*corner)}')
    ring = s.px(cx + 149, cy)
    h.check('ring drawn in the lens colour', close(ring, (32, 232, 176), 24), f'got {ring}')

    print('== no recursion / click-through')
    sx, sy = cx + 100 / z, cy + 20 / z
    h.check('lens content is the plain pattern, not a lens-in-lens', close(s.px(cx + 100, cy + 20), tile_at(sx, sy), 3))
    for mode in ('highlight', 'magnify', 'laser'):
        h.call('Show', 's', mode)
        picked = h.call('PickAt', 'dd', float(cx), float(cy))[0]
        h.check(f'{mode}: input passes through (nothing of ours is pickable)', 'projecteur' not in picked, f'picked "{picked}"')

    print('== freeze-style behaviour: moving and hiding')
    h.call('Show', 's', 'magnify')
    h.call('MoveTo', 'dd', 300.0, 300.0)
    h.call('MoveBy', 'dd', 50.0, -20.0)
    pos = h.call('GetState')[2:]
    h.check('MoveBy accumulates', pos == (350.0, 280.0), str(pos))
    h.call('Recenter')
    pos = h.call('GetState')[2:]
    h.check('Recenter goes to the monitor centre', pos == (W / 2, H / 2), str(pos))
    h.call('Hide')
    s = h.shot('hidden')
    p = (cx, cy)
    h.check('hidden: screen is the plain pattern again', close(s.px(*p), tile_at(*p), 2))

    print('== config')
    h.call('SetConfig', 's', json.dumps({'magnify': {'zoom': 3.0}}))
    h.call('Show', 's', 'magnify')
    h.call('MoveTo', 'dd', float(cx), float(cy))
    s = h.shot('zoom3')
    sx, sy = cx + 60 / 3, cy + 30 / 3
    assert tile_margin(sx, sy) >= 4
    h.check('zoom factor 3 is applied', close(s.px(cx + 60, cy + 30), tile_at(sx, sy), 3),
            f'got {s.px(cx + 60, cy + 30)} want {tile_at(sx, sy)}')

    print('== enable/disable cycles (leak check)')
    n0 = h.call('StageChildCount')[0]
    ok = True
    for _ in range(5):
        ok &= h.shell_extension('DisableExtension') and h.wait_name('org.projecteur.Overlay', False)
        ok &= h.shell_extension('EnableExtension') and h.wait_name('org.projecteur.Overlay', True)
    n1 = h.call('StageChildCount')[0]
    h.check('5x disable/enable works and the D-Bus name follows', ok)
    h.check('no leaked actors on the stage after 5 cycles', n0 == n1, f'{n0} before, {n1} after')
    h.call('TestPattern', 'b', True)
    h.call('Show', 's', 'magnify')
    h.call('MoveTo', 'dd', float(cx), float(cy))
    s = h.shot('after-cycles')
    sx, sy = cx + 100 / z, cy + 20 / z
    h.check('lens still correct after the cycles', close(s.px(cx + 100, cy + 20), tile_at(sx, sy), 3))

    print('== cost of moving the lens (informational; GPU time is not included)')
    tick = os.sysconf('SC_CLK_TCK')

    def cpu_percent(seconds, move):
        t0, c0 = time.time(), h.cpu_ticks()
        n = 0
        while time.time() - t0 < seconds:
            if move:
                h.call('MoveBy', 'dd', 1.0 if n % 2 == 0 else -1.0, 0.0)
                n += 1
            time.sleep(0.008)
        return (h.cpu_ticks() - c0) / tick / (time.time() - t0) * 100, n / seconds

    h.call('Hide')
    idle, _ = cpu_percent(4, False)
    h.call('Show', 's', 'magnify')
    busy, rate = cpu_percent(6, True)
    print(f'      shell CPU: idle {idle:.1f} %, lens moving at ~{rate:.0f} updates/s {busy:.1f} % of one core')
    h.check('moving the live lens stays under 60 % of one core', busy < 60, f'{busy:.1f} %')
    h.call('Hide')

    print('== log hygiene')
    h.log.flush()
    log = (h.tmp / 'shell.log').read_text(errors='replace')
    bad = [l for l in log.splitlines() if 'projecteur-overlay' in l and ('JS ERROR' in l or 'Error' in l)]
    h.check('no errors from the extension in the shell log', not bad, '; '.join(bad[:2])[:200])


def inner(argv):
    keep = '--keep' in argv
    h = Harness(keep)
    rc = 1
    try:
        print(f'temp dir: {h.tmp}')
        h.start()
        run_tests(h)
        failed = [r for r in h.results if not r[1]]
        print(f'\n{len(h.results) - len(failed)}/{len(h.results)} checks passed')
        rc = 1 if failed else 0
    except Exception as e:  # noqa: BLE001 - report anything, then clean up
        print(f'\nHARNESS ERROR: {type(e).__name__}: {e}')
        h.keep = True
        try:
            print((h.tmp / 'shell.log').read_text(errors='replace')[-3000:])
        except OSError:
            pass
    finally:
        h.stop()
    sys.exit(rc)


if __name__ == '__main__':
    if '--inner' in sys.argv:
        inner(sys.argv)
    else:
        outer()
