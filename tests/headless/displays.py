# SPDX-License-Identifier: MIT
"""Several monitors of different sizes, and fractional scaling, in the isolated headless shell.

    tests/headless/run.py --monitors 1280x720,1024x768
    tests/headless/run.py --scale 1.5
    tests/headless/run.py --monitors 1920x1080,1280x1024 --scale 1.25
"""
import json
import math
import time

import run as common
from run import TILE

DISPLAY_CONFIG = ('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig', 'org.gnome.Mutter.DisplayConfig')


def display_state(h):
    r = h.bus.call_sync(*DISPLAY_CONFIG, 'GetCurrentState', None, None, 0, 10000, None)
    return r.unpack()


def apply_scale(h, wanted):
    """Give every monitor the supported scale closest to `wanted`, side by side, the first one primary. Returns the scales."""
    serial, monitors, _logical, _props = display_state(h)
    x = 0
    config, chosen = [], []
    for i, (spec, modes, _mprops) in enumerate(monitors):
        current = next(m for m in modes if m[6].get('is-current'))
        scale = min(current[5], key=lambda s: abs(s - wanted))
        chosen.append(scale)
        config.append((x, 0, scale, 0, i == 0, [(spec[0], current[0], {})]))
        x += int(round(current[1] / scale))
    h.bus.call_sync(*DISPLAY_CONFIG, 'ApplyMonitorsConfig',
                    h.GLib.Variant('(uua(iiduba(ssa{sv}))a{sv})', (serial, 1, config, {})), None, 0, 20000, None)
    return chosen


def geometry(h):
    return json.loads(h.call('Monitors')[0])


def wait_geometry(h, predicate, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        g = geometry(h)
        if predicate(g):
            return g
        time.sleep(0.3)
    return geometry(h)


def js_round(x):
    return math.floor(x + 0.5)


def diameter_on(m, size):
    """Highlight hole / lens diameter on monitor m, in logical px (mirrors CAL in overlay.js)."""
    return js_round(m['height'] * (0.102 + 0.686 * size))


class View:
    """A screenshot addressed in stage (logical) coordinates."""

    def __init__(self, shot, scale):
        self.shot, self.scale = shot, scale

    def px(self, x, y):
        return self.shot.px(min(int(round(x * self.scale)), self.shot.w - 1), min(int(round(y * self.scale)), self.shot.h - 1))


def run_display_tests(h):
    if h.scale:
        print(f'== fractional scale {h.scale}')
        chosen = apply_scale(h, h.scale)
        wait_geometry(h, lambda g: any(abs(m['scale'] - 1.0) > 0.01 for m in g['monitors']))
        print(f'      scales applied: {[round(c, 4) for c in chosen]}')
    g = geometry(h)
    mons = g['monitors']
    scale = 1.0
    print(f'== layout: {len(mons)} monitor(s), stage {g["stage"]}, monitors {[(m["x"], m["y"], m["width"], m["height"], round(m["scale"], 3)) for m in mons]}')
    h.check('all the virtual monitors are there', len(mons) == len(h.monitors), str(mons))
    # each monitor's logical size times its own scale is its physical size (monitors may have different scales)
    physical = [(round(m['width'] * m['scale']), round(m['height'] * m['scale'])) for m in mons]
    h.check('their logical sizes follow their scales', all(abs(p[0] - w) <= 1 and abs(p[1] - hh) <= 1 for p, (w, hh) in zip(physical, h.monitors)), f'{physical} vs {h.monitors}')
    h.call('TestPattern', 'b', True)
    first = h.shot('displays-pattern')
    scale = first.w / g['stage'][0]      # the screenshot is rendered at one scale for the whole stage (the highest)
    view = View(first, scale)
    print(f'      screenshot {view.shot.w}x{view.shot.h} px for a stage of {g["stage"]} at scale {scale:.3f}')
    h.check('the screenshot covers the whole stage', abs(view.shot.h - g['stage'][1] * scale) <= 2, f'{view.shot.w}x{view.shot.h} for {g["stage"]}')
    tol = 3 if abs(scale - 1) < 0.01 else 14   # a fractional scale resamples: colours blend near edges only, but allow for it
    margin = 6

    def tile_ok(x, y):
        return common.tile_margin(x, y) >= margin

    dim = 1 - 0.8875 * 0.80
    for k, m in enumerate(mons):
        cx, cy = m['x'] + m['width'] // 2 + 7, m['y'] + m['height'] // 2 + 5
        print(f'== monitor {k}: {m["width"]}x{m["height"]} at ({m["x"]},{m["y"]}), effects centred at ({cx},{cy})')

        # --- highlight
        h.call('Show', 's', 'highlight')
        h.call('MoveTo', 'dd', float(cx), float(cy))
        view = View(h.shot(f'displays-m{k}-highlight'), scale)
        r = diameter_on(m, 0.43) / 2
        inside = (cx + 0.6 * r, cy)
        outside = (cx + 1.5 * r, cy + 4)
        for name, (px_, py_), want_dim in (('inside the hole', inside, False), ('outside the hole', outside, True)):
            if not tile_ok(px_, py_):   # move the probe off a tile edge
                px_ += 12
            want = common.tile_at(px_, py_)
            if want_dim:
                want = tuple(round(c * dim) for c in want)
            h.check(f'monitor {k} highlight: {name}', common.close(view.px(px_, py_), want, tol + 3), f'{view.px(px_, py_)} want {want}')
        corner = (m['x'] + 6, m['y'] + 6)
        want = tuple(round(c * dim) for c in common.tile_at(*corner))
        h.check(f'monitor {k} highlight: the whole monitor is dimmed (corner)', common.close(view.px(*corner), want, tol + 3), f'{view.px(*corner)} want {want}')
        for j, other in enumerate(mons):
            if j == k:
                continue
            pt = (other['x'] + other['width'] // 2 + 3, other['y'] + other['height'] // 2 + 3)
            h.check(f'monitor {k} highlight: monitor {j} stays untouched', common.close(view.px(*pt), common.tile_at(*pt), tol), f'{view.px(*pt)} want {common.tile_at(*pt)}')

        # --- magnify
        h.call('Show', 's', 'magnify')
        h.call('MoveTo', 'dd', float(cx), float(cy))
        view = View(h.shot(f'displays-m{k}-magnify'), scale)
        d = diameter_on(m, 0.80)
        # probe points inside the lens that are away from tile edges and where the lens must differ from the plain screen
        lens_r = d / 2
        probes = []
        for dy in range(-int(lens_r) + 24, int(lens_r) - 24, 17):
            for dx in range(-int(lens_r) + 24, int(lens_r) - 24, 23):
                if math.hypot(dx, dy) > lens_r - 20:
                    continue
                sx, sy = cx + dx / 2, cy + dy / 2
                if common.tile_margin(sx, sy) >= margin and common.tile_margin(cx + dx, cy + dy) >= margin and common.tile_at(sx, sy) != common.tile_at(cx + dx, cy + dy):
                    probes.append((dx, dy))
        step = max(1, len(probes) // 6)
        lens_checked = 0
        for dx, dy in probes[::step][:6]:
            sx, sy = cx + dx / 2, cy + dy / 2
            got, want, wrong = view.px(cx + dx, cy + dy), common.tile_at(sx, sy), common.tile_at(cx + dx, cy + dy)
            lens_checked += 1
            h.check(f'monitor {k} lens ({dx:+d},{dy:+d}) shows the content at half the distance (zoom 2)',
                    common.close(got, want, tol) and not common.close(got, wrong, tol), f'{got} want {want} plain {wrong}')
        h.check(f'monitor {k} lens: enough discriminating points were checked', lens_checked >= 3, str(lens_checked))
        corner = (cx - d / 2 + 4, cy - d / 2 + 4)   # the corner of the lens' square: plain screen, the lens is round
        if tile_ok(*corner):
            h.check(f'monitor {k} lens is round (square corner shows the plain screen)', common.close(view.px(*corner), common.tile_at(*corner), tol),
                    f'{view.px(*corner)} want {common.tile_at(*corner)}')
        ring = view.px(cx + d / 2 - 2, cy)
        h.check(f'monitor {k} lens ring colour', common.close(ring, (0, 248, 190), 40), str(ring))

        # --- recenter goes to the centre of THIS monitor
        h.call('Recenter')
        st = h.call('GetState')
        h.check(f'monitor {k}: recenter goes to its centre', abs(st[2] - (m['x'] + m['width'] / 2)) <= 1 and abs(st[3] - (m['y'] + m['height'] / 2)) <= 1, str(st))
        h.call('Hide')

    if len(mons) > 1:
        print('== crossing from one monitor to the next')
        a, b = mons[0], mons[1]
        h.call('Show', 's', 'highlight')
        h.call('MoveTo', 'dd', float(a['x'] + a['width'] - 5), 100.0)
        h.call('MoveBy', 'dd', 10.0, 0.0)
        st = h.call('GetState')
        h.check('moving by 10 px across the boundary continues on the next monitor', abs(st[2] - (a['x'] + a['width'] + 5)) <= 0.5 and st[3] == 100.0, str(st))
        view = View(h.shot('displays-cross'), scale)
        pa = (a['x'] + a['width'] // 2 + 3, a['y'] + a['height'] // 2 + 3)
        corner_b = (b['x'] + b['width'] - 8, b['y'] + b['height'] - 8)
        h.check('...and the first monitor is no longer dimmed', common.close(view.px(*pa), common.tile_at(*pa), tol))
        want = tuple(round(c * dim) for c in common.tile_at(*corner_b))
        h.check('...the next one is', common.close(view.px(*corner_b), want, tol + 3), f'{view.px(*corner_b)} want {want}')
        h.call('Hide')

        shorter, taller = (a, b) if a['height'] < b['height'] else (b, a)
        if shorter['height'] != taller['height']:
            print('== the gap below the shorter monitor')
            x = shorter['x'] + shorter['width'] // 2
            h.call('Show', 's', 'highlight')
            h.call('MoveTo', 'dd', float(x), float(taller['height'] - 2))
            st = h.call('GetState')
            h.check('a point below the shorter monitor stops at its bottom edge', st[3] <= shorter['height'] - 1 and abs(st[2] - x) <= 1, f'{st}, monitor height {shorter["height"]}')
            h.call('Hide')

    h.call('Hide')
    view = View(h.shot('displays-hidden'), scale)
    pt = (mons[0]['x'] + 200, mons[0]['y'] + 200)
    h.check('hidden: the screen is the plain pattern again', common.close(view.px(*pt), common.tile_at(*pt), tol))
    h.call('TestPattern', 'b', False)
    h.log.flush()
    bad = [l for l in (h.tmp / 'shell.log').read_text(errors='replace').splitlines() if 'projecteur-overlay' in l and ('JS ERROR' in l or 'Error' in l)]
    h.check('no errors from the extension in the shell log', not bad, '; '.join(bad[:2])[:200])
