// SPDX-License-Identifier: MIT
// Drives the real settings window (prefsUI.js) as a Wayland client of the headless shell: changes widgets and checks
// the settings, changes settings and checks the widgets, saves a picture of every page.
// Run by ext_features.py; prints "CHECK PASS|FAIL <name>" lines.
import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import System from 'system';

import {buildPreferences, chordFromKeyEvent, chordLabel, installedApps, PAGES} from '../../gnome-shell/projecteur-overlay@mamrehn.github.io/prefsUI.js';
import {KEYS, isProfileKey} from '../../gnome-shell/projecteur-overlay@mamrehn.github.io/settingsModel.js';

const OUT = GLib.getenv('PREFSTEST_OUT');
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
let failures = 0;
function check(name, ok, detail = '') {
    print(`CHECK ${ok ? 'PASS' : 'FAIL'} ${name}${!ok && detail ? `  (${detail})` : ''}`);
    if (!ok)
        failures++;
}
const sleep = ms => new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => { resolve(); return GLib.SOURCE_REMOVE; }));

function screenshot(window, name) {
    const paintable = Gtk.WidgetPaintable.new(window);
    const snapshot = Gtk.Snapshot.new();
    paintable.snapshot(snapshot, window.get_width(), window.get_height());
    const node = snapshot.to_node();
    if (!node)
        return;
    window.get_native().get_renderer().render_texture(node, null).save_to_png(`${OUT}/${name}.png`);
}

const calls = [];
const fakeDaemon = {
    status: null,
    vibrate: n => calls.push(['vibrate', n]),
    timerStart: () => calls.push(['start']),
    timerPause: () => calls.push(['pause']),
    timerReset: () => calls.push(['reset']),
};

async function scenario(app) {
    const settings = new Gio.Settings({schema_id: 'org.gnome.shell.extensions.projecteur-overlay'});
    const window = new Adw.PreferencesWindow({application: app, default_width: 720, default_height: 820});
    const reg = buildPreferences(window, settings, fakeDaemon);
    window.present();
    await sleep(600);

    // ---- completeness: no setting without a row ----
    const profileKeys = Object.keys(KEYS).filter(isProfileKey);
    const withRows = [...Object.keys(reg.rows), ...(reg.alertRows ? ['timer-alerts'] : [])];
    check('every setting that can differ per application has a row', eq([...withRows].sort(), [...profileKeys].sort()),
        `missing ${profileKeys.filter(k => !withRows.includes(k))}; extra ${withRows.filter(k => !profileKeys.includes(k))}`);
    check('the table lists every row once', PAGES.flatMap(p => p.groups.flatMap(g => g.rows.map(r => r.key))).length === profileKeys.length);

    // ---- initial values ----
    const w = key => reg.rows[key].widgets;
    check('highlight size shows its setting (43)', w('highlight-size').scale.get_value() === 43);
    check('freeze shows its setting (on)', w('freeze-effects').switch.active === true);
    check('hold Next shows "start-presentation"', w('hold-next-action').combo.selected === 1);
    check('the shortcut row is disabled while the action is not "shortcut"', reg.rows['hold-next-shortcut'].row.sensitive === false);

    // ---- widget -> setting ----
    w('highlight-size').scale.set_value(70);
    check('moving the slider writes the setting', settings.get_int('highlight-size') === 70);
    w('laser-enabled').switch.active = false;
    check('the switch writes the setting', settings.get_boolean('laser-enabled') === false);
    w('hold-next-action').combo.selected = 3;
    check('the action chooser writes the setting', settings.get_string('hold-next-action') === 'fast-forward');
    w('timer-minutes').spin.value = 45;
    check('the spin button writes the setting', settings.get_int('timer-minutes') === 45);
    const green = new Gdk.RGBA();
    green.parse('#00ff00');
    w('laser-color').color.rgba = green;
    check('the colour button writes #rrggbb', settings.get_string('laser-color') === '#00ff00', settings.get_string('laser-color'));
    reg.alertRows[1].value = 10;
    check('a timer alert slot writes its position (5, 10, off)', eq(settings.get_value('timer-alerts').deepUnpack(), [5, 10, 0]), JSON.stringify(settings.get_value('timer-alerts').deepUnpack()));
    check('...and the rows do not jump while it is edited', reg.alertRows[0].value === 5 && reg.alertRows[1].value === 10 && reg.alertRows[2].value === 0);
    reg.alertRows[0].value = 0;
    check('switching a slot off leaves the others where they are', eq(settings.get_value('timer-alerts').deepUnpack(), [0, 10, 0]) && reg.alertRows[1].value === 10);

    // ---- setting -> widget ----
    settings.set_int('pointer-speed', 55);
    check('a change made elsewhere moves the slider', w('pointer-speed').scale.get_value() === 55);
    settings.set_boolean('laser-enabled', true);
    check('...and flips the switch', w('laser-enabled').switch.active === true);
    settings.set_string('hold-back-action', 'volume');
    check('...and changes the chooser', w('hold-back-action').combo.selected === 5);

    // ---- shortcut recording ----
    settings.set_string('hold-next-action', 'shortcut');
    check('the shortcut row is enabled for the action "shortcut"', reg.rows['hold-next-shortcut'].row.sensitive === true);
    const sc = w('hold-next-shortcut');
    check('no shortcut yet', sc.label.label === 'Not set');
    sc.record.emit('clicked');
    check('recording says so', sc.record.label.startsWith('Press the keys'));
    sc.press(Gdk.KEY_Control_L, 37, Gdk.ModifierType.CONTROL_MASK);   // a modifier alone is not a shortcut
    check('a modifier alone keeps waiting', sc.record.label.startsWith('Press the keys') && eq(settings.get_value('hold-next-shortcut').deepUnpack(), []));
    sc.press(Gdk.KEY_P, 33, Gdk.ModifierType.CONTROL_MASK | Gdk.ModifierType.SHIFT_MASK);   // Ctrl+Shift+P (keycode 33 = evdev 25 + 8)
    check('Ctrl+Shift+P is stored as evdev codes', eq(settings.get_value('hold-next-shortcut').deepUnpack(), [29, 42, 25]), JSON.stringify(settings.get_value('hold-next-shortcut').deepUnpack()));
    check('...and shown by name', sc.label.label === 'Ctrl+Shift+P', sc.label.label);
    check('recording ended', sc.record.label === 'Record…');
    sc.record.emit('clicked');
    sc.press(Gdk.KEY_Escape, 9, 0);
    check('Escape cancels without changing the shortcut', eq(settings.get_value('hold-next-shortcut').deepUnpack(), [29, 42, 25]) && sc.record.label === 'Record…');
    sc.clear.emit('clicked');
    check('the clear button removes the shortcut', eq(settings.get_value('hold-next-shortcut').deepUnpack(), []) && sc.label.label === 'Not set');
    check('chord names', chordLabel([56, 62]) === 'Alt+F4' && chordLabel([125, 32]) === 'Super+D' && chordLabel([57]) === 'Space');
    check('chord from a key event', eq(chordFromKeyEvent(Gdk.KEY_F5, 71, 0), [63]) && chordFromKeyEvent(Gdk.KEY_Shift_L, 50, Gdk.ModifierType.SHIFT_MASK) === null);

    // ---- vibration test button ----
    w('vibration-intensity').test.emit('clicked');
    check('the test button asks the daemon for three pulses', eq(calls.at(-1), ['vibrate', 3]));

    // ---- profiles ----
    const APP = 'org.projecteur.TestApp.desktop';
    check('no profiles at first', Object.keys(reg.profileRows).length === 0);
    settings.set_string('profiles', JSON.stringify({[APP]: {}}));
    check('a profile in the settings appears as a row', APP in reg.profileRows);
    const prof = reg.profileRows[APP].registry.rows;
    check('a profile shows the general value for a setting it does not override', prof['pointer-speed'].widgets.scale.get_value() === 55);
    check('...without a "use general" button', prof['pointer-speed'].widgets.reset.visible === false);
    prof['pointer-speed'].widgets.scale.set_value(80);
    check('changing it in the profile stores an override', eq(JSON.parse(settings.get_string('profiles')), {[APP]: {'pointer-speed': 80}}), settings.get_string('profiles'));
    check('...and leaves the general setting alone', settings.get_int('pointer-speed') === 55);
    check('...and shows the "use general" button', prof['pointer-speed'].widgets.reset.visible === true);
    settings.set_int('pointer-speed', 40);
    check('an override is not touched by a change of the general value', prof['pointer-speed'].widgets.scale.get_value() === 80);
    check('a setting without override follows the general value', prof['laser-size'].widgets.scale.get_value() === settings.get_int('laser-size'));
    settings.set_int('laser-size', 33);
    check('...also when it changes', prof['laser-size'].widgets.scale.get_value() === 33);
    prof['pointer-speed'].widgets.reset.emit('clicked');
    check('"use general" removes the override', eq(JSON.parse(settings.get_string('profiles')), {[APP]: {}}) && prof['pointer-speed'].widgets.scale.get_value() === 40);
    prof['hold-back-shortcut'].widgets.press;   // exists
    prof['freeze-effects'].widgets.switch.active = false;
    check('a switch in the profile stores an override', JSON.parse(settings.get_string('profiles'))[APP]['freeze-effects'] === false);
    check('the general freeze setting is unchanged', settings.get_boolean('freeze-effects') === true);
    screenshot(window, 'page-profile-expanded-before');
    reg.profileRows[APP].expander.expanded = true;
    window.set_visible_page_name('applications');
    await sleep(500);
    screenshot(window, 'page-applications');
    reg.addApplicationButton.emit('clicked');
    await sleep(500);
    check('the application chooser opens', !!reg.chooserDialog);
    screenshot(window, 'dialog-add-application');
    reg.chooserDialog?.close();
    await sleep(300);
    check('the chooser lists installed applications', installedApps().length > 0, String(installedApps().length));
    reg.profileRows[APP].removeButton.emit('clicked');
    check('"Remove" deletes the profile', Object.keys(reg.profileRows).length === 0 && settings.get_string('profiles') === '{}');
    settings.set_string('profiles', '{{ broken');
    check('a broken profiles text shows no profiles and does not throw', Object.keys(reg.profileRows).length === 0);
    settings.reset('profiles');

    // ---- the remote page ----
    const r = reg.remote;
    reg.updateStatus(null);
    check('without a daemon the page says how to start it', r.daemonRow.subtitle.includes('systemctl --user start projecteurd'), r.daemonRow.subtitle);
    window.set_visible_page_name('remote');
    await sleep(400);
    screenshot(window, 'page-remote-no-daemon');
    reg.updateStatus({connected: true, connection: 'bluetooth', battery: {percent: 63, state: 'discharging', charging: false}, firmware: '1.1.32', bootloader: '26.1.15',
        timer: {enabled: true, state: 'running', remaining: 754, total: 1800}});
    check('the connection is described', r.connection.subtitle === 'Connected over Bluetooth', r.connection.subtitle);
    check('the battery level is shown', r.battery.subtitle === '63 %', r.battery.subtitle);
    check('the firmware is shown', r.firmware.subtitle === '1.1.32' && r.bootloader.subtitle === '26.1.15');
    check('the timer is shown', r.timer.subtitle.startsWith('12:34'), r.timer.subtitle);
    r.start.emit('clicked'); r.pause.emit('clicked'); r.reset.emit('clicked');
    check('the timer buttons reach the daemon', eq(calls.slice(-3), [['start'], ['pause'], ['reset']]));
    await sleep(400);
    screenshot(window, 'page-remote');

    // ---- pictures of the other pages, with a few values set ----
    for (const p of PAGES) {
        window.set_visible_page_name(p.id);
        await sleep(500);
        screenshot(window, `page-${p.id}`);
    }

    // restore: the settings file is shared with the shell under test
    for (const key of Object.keys(KEYS))
        settings.reset(key);
    reg.destroy();
    window.destroy();
}

const app = new Adw.Application({application_id: 'org.projecteur.PrefsTest'});
app.connect('activate', () => {
    scenario(app).catch(e => {
        print(`CHECK FAIL scenario threw: ${e.message}\n${e.stack}`);
        failures++;
    }).finally(() => {
        print(failures ? `PREFSTEST FAILED: ${failures}` : 'PREFSTEST OK');
        app.quit();
    });
});
const status = app.run([]);
System.exit(failures ? 1 : status);
