# SPDX-License-Identifier: MIT
"""Extension features beyond the effects themselves, in the isolated headless shell: settings that reach the overlay
and the daemon, per-application profiles (a real Wayland window with a known application id), the panel indicator.

Settings are changed with the `gsettings` tool against the harness's private keyfile backend, the same way another
process (the settings window) changes them, so the extension's change handling is what is tested.
"""
import json
import os
import pathlib
import signal
import subprocess
import time

import run as common

APP_ID = 'org.projecteur.TestApp'
SCHEMA = 'org.gnome.shell.extensions.projecteur-overlay'


def isolated_env(h):
    """The environment in which gsettings may only ever touch the harness's own keyfile."""
    t = h.tmp
    env = {'PATH': os.environ['PATH'], 'HOME': str(t / 'home'), 'XDG_CONFIG_HOME': str(t / 'config'),
           'XDG_DATA_HOME': str(t / 'data'), 'XDG_CACHE_HOME': str(t / 'cache'), 'XDG_RUNTIME_DIR': str(t / 'runtime'),
           'GSETTINGS_BACKEND': 'keyfile', 'DBUS_SESSION_BUS_ADDRESS': os.environ['DBUS_SESSION_BUS_ADDRESS']}
    return env


def set_setting(h, key, value):
    """gsettings set; `value` in GVariant text form ('70', 'true', "'#00ff00'", '[29, 42]')."""
    env = isolated_env(h)
    subprocess.run(['gsettings', '--schemadir', str(h.ext_dir / 'schemas'), 'set', SCHEMA, key, value], env=env, check=True, timeout=20)
    got = subprocess.run(['gsettings', '--schemadir', str(h.ext_dir / 'schemas'), 'get', SCHEMA, key], env=env, check=True,
                         capture_output=True, text=True, timeout=20).stdout.strip()
    keyfile = (h.tmp / 'config/glib-2.0/settings/keyfile').read_text()
    assert 'projecteur-overlay' in keyfile, 'the setting did not land in the private keyfile'
    return got


def wait(cond, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.1)
    return False


def extension_state(h):
    return json.loads(h.call('ExtensionState')[0])


class TestWindow:
    def __init__(self, h):
        env = dict(isolated_env(h))
        sockets = sorted(p.name for p in (h.tmp / 'runtime').glob('wayland-*') if not p.name.endswith('.lock'))
        assert sockets, 'no Wayland socket of the headless shell'
        env.update({'WAYLAND_DISPLAY': sockets[0], 'GDK_BACKEND': 'wayland', 'GSETTINGS_BACKEND': 'memory'})
        self.proc = subprocess.Popen(['gjs', '-m', str(pathlib.Path(__file__).with_name('testwindow.js'))], env=env,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    def close(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def run_extension_tests(h):
    print('== settings reach the overlay (changed from another process)')
    h.call('Hide')
    h.call('TestPattern', 'b', True)
    h.call('Show', 's', 'highlight')
    h.call('MoveTo', 'dd', 650.0, 370.0)
    # 170 px from the centre: outside the default hole (radius 143), inside a 70 % hole (radius 210)
    probe = (650 + 170, 370 + 0)
    want_plain = common.tile_at(*probe)
    assert common.tile_margin(*probe) >= 6, 'probe too close to a tile edge'
    s = h.shot('settings-default')
    dimmed = tuple(round(c * common.DIM_MULTIPLIER) for c in want_plain)
    h.check('default size 43 %: the probe point is dimmed', common.close(s.px(*probe), dimmed, 5), f'{s.px(*probe)} want {dimmed}')

    set_setting(h, 'highlight-size', '70')
    time.sleep(0.5)
    s = h.shot('settings-size70')
    h.check('highlight-size 70 %: the hole grew, the probe point is now clear', common.close(s.px(*probe), want_plain, 3), f'{s.px(*probe)} want {want_plain}')

    set_setting(h, 'highlight-size', '43')
    set_setting(h, 'highlight-contrast', '50')
    time.sleep(0.5)
    s = h.shot('settings-contrast50')
    far = (1000, 600)
    half = tuple(round(c * (1 - 0.8875 * 0.5)) for c in common.tile_at(*far))
    h.check('highlight-contrast 50 %: dimmed to 56 % brightness', common.close(s.px(*far), half, 5), f'{s.px(*far)} want {half}')
    set_setting(h, 'highlight-contrast', '80')

    h.call('Show', 's', 'laser')
    h.call('MoveTo', 'dd', 300.0, 200.0)
    set_setting(h, 'laser-color', "'#00ff00'")
    time.sleep(0.5)
    s = h.shot('settings-laser-green')
    h.check('laser-color: the dot is green now', common.close(s.px(300, 200), (0, 255, 0), 12), f'{s.px(300, 200)}')
    # not just the centre: at the rim the dot must fade to transparent green, not tint towards red
    # A pure green dot with alpha a over a background scales red and blue by the same factor (1 - a); a rim that
    # fades towards red would add red and break that equality.
    rim = (300 + 14, 200)
    bg = common.tile_at(*rim)
    assert common.tile_margin(*rim) >= 6 and bg[0] > 100 and bg[2] > 60, 'rim probe unsuitable'
    r_rim = s.px(*rim)
    ratio_r, ratio_b = r_rim[0] / bg[0], r_rim[2] / bg[2]
    h.check('the rim of the dot fades to transparent green (red and blue scale alike)', ratio_r < 0.95 and abs(ratio_r - ratio_b) < 0.03,
            f'{r_rim} over {bg}: red x{ratio_r:.2f}, blue x{ratio_b:.2f}')

    h.call('SetConfig', 's', json.dumps({'laser': {'color': 'red; background-image: url(x)', 'size': 'huge'}, 'bogus': {'x': 1}}))
    time.sleep(0.3)
    s = h.shot('settings-laser-badinput')
    h.check('colour and size from D-Bus are validated: nothing changed', common.close(s.px(300, 200), (0, 255, 0), 20), f'{s.px(300, 200)}')
    h.check('the overlay survived it', h.call('GetState')[0] is True)
    set_setting(h, 'laser-color', "'#ff0000'")
    h.call('Hide')
    h.call('TestPattern', 'b', False)

    print('== settings reach the daemon (what the extension pushes)')
    pushed = extension_state(h)['pushed']
    h.check('the extension pushes the daemon keys under their own names', pushed and pushed.get('pointer-speed') == 35 and pushed.get('hold-next-action') == 'start-presentation', str(pushed)[:200])
    h.check('appearance and extension keys are not pushed', pushed and not any(k in pushed for k in ('laser-color', 'highlight-size', 'profiles', 'show-indicator')), str(sorted(pushed or {})))
    set_setting(h, 'pointer-speed', '50')
    h.check('a changed setting is pushed at once', wait(lambda: (extension_state(h)['pushed'] or {}).get('pointer-speed') == 50))
    set_setting(h, 'pointer-speed', '35')
    wait(lambda: (extension_state(h)['pushed'] or {}).get('pointer-speed') == 35)

    print('== per-application profiles (a real Wayland window)')
    profiles = {f'{APP_ID}.desktop': {'pointer-speed': 60, 'freeze-effects': False, 'hold-next-action': 'volume', 'bogus': 1}}
    set_setting(h, 'profiles', "'" + json.dumps(profiles) + "'")
    win = TestWindow(h)
    try:
        got_focus = wait(lambda: extension_state(h)['appId'] == f'{APP_ID}.desktop', 20)
        err = ''
        if not got_focus and win.proc.poll() is not None:
            err = win.proc.stderr.read().decode(errors='replace')[:300]
        h.check('the focused application is recognised from its window', got_focus, f'appId={extension_state(h)["appId"]} {err}')
        pushed = extension_state(h)['pushed'] or {}
        h.check('its profile overrides the general settings', pushed.get('pointer-speed') == 60 and pushed.get('freeze-effects') is False and pushed.get('hold-next-action') == 'volume', str(pushed)[:200])
        h.check('settings it does not override stay general', pushed.get('hold-back-action') == 'blank-screen' and pushed.get('recenter-effects') is True)
        h.check('an unknown key in a profile is ignored', 'bogus' not in pushed)
    finally:
        win.close()
    h.check('closing the window returns to the general settings', wait(lambda: (extension_state(h)['pushed'] or {}).get('pointer-speed') == 35, 10),
            str(extension_state(h)))
    set_setting(h, 'profiles', "'{}'")

    print('== a broken profiles text never breaks the extension')
    set_setting(h, 'profiles', "'{{ not json'")
    time.sleep(0.5)
    st = extension_state(h)
    h.check('still running, general settings in force', (st['pushed'] or {}).get('pointer-speed') == 35, str(st)[:200])
    set_setting(h, 'profiles', "'{}'")

    print('== panel indicator without a daemon')
    ind = extension_state(h)['indicator']
    h.check('the indicator exists', ind is not None)
    h.check('it says the daemon is not running', ind and ind['connection'] == 'Daemon not running', str(ind))
    h.check('it is dimmed', ind and ind['dimmed'])
    set_setting(h, 'show-indicator', 'false')
    h.check('show-indicator=false removes it', wait(lambda: extension_state(h)['indicator'] is None))
    set_setting(h, 'show-indicator', 'true')
    h.check('and true brings it back', wait(lambda: extension_state(h)['indicator'] is not None))
