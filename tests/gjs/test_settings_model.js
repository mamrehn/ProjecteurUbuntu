// SPDX-License-Identifier: MIT
// Plain gjs test (no shell): the settings table against the schema file, and the profile logic.
//   gjs -m tests/gjs/test_settings_model.js
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import System from 'system';

import {KEYS, HOLD_ACTIONS, defaults, validValue, parseProfiles, effectiveValues, daemonConfig, overlayConfig, isProfileKey,
    addProfile, removeProfile, setOverride, clearOverride, serializeProfiles, alertsToSlots, slotsToAlerts, isModifierCode}
    from '../../gnome-shell/projecteur-overlay@mamrehn.github.io/settingsModel.js';

let failures = 0;
function check(name, ok, detail = '') {
    print(`  [${ok ? 'PASS' : 'FAIL'}] ${name}${!ok && detail ? `  (${detail})` : ''}`);
    if (!ok)
        failures++;
}
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const extDir = GLib.build_filenamev([GLib.path_get_dirname(import.meta.url.replace('file://', '')), '..', '..', 'gnome-shell', 'projecteur-overlay@mamrehn.github.io']);

print('== schema and table agree');
const tmp = GLib.dir_make_tmp('projecteur-schema-XXXXXX');
GLib.file_set_contents(GLib.build_filenamev([tmp, 'org.gnome.shell.extensions.projecteur-overlay.gschema.xml']),
    GLib.file_get_contents(GLib.build_filenamev([extDir, 'schemas', 'org.gnome.shell.extensions.projecteur-overlay.gschema.xml']))[1]);
const [ok, , err] = GLib.spawn_command_line_sync(`glib-compile-schemas --strict ${tmp}`);
check('the schema compiles (strict)', ok, String(err));
const source = Gio.SettingsSchemaSource.new_from_directory(tmp, Gio.SettingsSchemaSource.get_default(), false);
const schema = source.lookup('org.gnome.shell.extensions.projecteur-overlay', false);
check('the schema can be looked up', schema !== null);
const schemaKeys = schema.list_keys();
check('every key of the table is in the schema', Object.keys(KEYS).every(k => schemaKeys.includes(k)),
    Object.keys(KEYS).filter(k => !schemaKeys.includes(k)).join(','));
check('every key of the schema is in the table', schemaKeys.every(k => k in KEYS), schemaKeys.filter(k => !(k in KEYS)).join(','));
for (const key of Object.keys(KEYS)) {
    const sk = schema.get_key(key);
    const k = KEYS[key];
    check(`${key}: type ${k.type}`, sk.get_value_type().dup_string() === k.type);
    check(`${key}: default`, eq(sk.get_default_value().recursiveUnpack(), k.def), `${JSON.stringify(sk.get_default_value().recursiveUnpack())} vs ${JSON.stringify(k.def)}`);
    const [rangeKind, rangeData] = sk.get_range().recursiveUnpack();
    if (k.min !== undefined)
        check(`${key}: range ${k.min}..${k.max}`, rangeKind === 'range' && eq(rangeData, [k.min, k.max]), JSON.stringify([rangeKind, rangeData]));
    if (k.choices)
        check(`${key}: choices`, rangeKind === 'enum' && eq(rangeData, k.choices), JSON.stringify(rangeData));
}

print('== values');
check('valid and invalid booleans', validValue('freeze-effects', true) && !validValue('freeze-effects', 1));
check('range', validValue('pointer-speed', 0) && validValue('pointer-speed', 100) && !validValue('pointer-speed', 101) && !validValue('pointer-speed', 3.5));
check('timer minutes start at 1', !validValue('timer-minutes', 0) && validValue('timer-minutes', 1));
check('choices', HOLD_ACTIONS.every(a => validValue('hold-next-action', a)) && !validValue('hold-next-action', 'format-disk'));
check('lists of key codes', validValue('hold-next-shortcut', [29, 42, 25]) && !validValue('hold-next-shortcut', [-1]) && !validValue('hold-next-shortcut', 'x'));
check('unknown keys are invalid', !validValue('nonsense', true));
check('show-indicator and profiles are not profile settings', !isProfileKey('show-indicator') && !isProfileKey('profiles') && isProfileKey('pointer-speed'));

print('== colours go into CSS');
check('#rrggbb is a colour', validValue('laser-color', '#00ff00') && validValue('magnify-color', '#00F8BE'));
check('anything else is not', !validValue('laser-color', 'red') && !validValue('laser-color', '#fff') && !validValue('laser-color', '#00ff00; background-image: url(x)') && !validValue('laser-color', 5));
const bad = overlayConfig({...defaults(), 'laser-color': 'red; x', 'magnify-color': 5});
check('a broken colour falls back to the default', bad.laser.color === '#ff0000' && bad.magnify.color === '#00f8be');

print('== profiles');
check('broken text means no profiles', eq(parseProfiles('{{'), {}) && eq(parseProfiles('[1]'), {}) && eq(parseProfiles('null'), {}));
const profiles = parseProfiles(JSON.stringify({
    'impress.desktop': {'pointer-speed': 60, 'laser-color': '#00ff00', 'profiles': 'x', 'freeze-effects': 'yes', 'bogus': 1},
    'broken.desktop': 'nope',
}));
check('only valid settings survive', eq(profiles, {'impress.desktop': {'pointer-speed': 60, 'laser-color': '#00ff00'}}), JSON.stringify(profiles));
const base = defaults();
const inImpress = effectiveValues(base, profiles, 'impress.desktop');
check('a profile overrides', inImpress['pointer-speed'] === 60 && inImpress['laser-color'] === '#00ff00');
check('and leaves the rest alone', inImpress['highlight-size'] === 43 && inImpress['freeze-effects'] === true);
check('another application gets the general settings', eq(effectiveValues(base, profiles, 'firefox.desktop'), base));
check('no focused application: general settings', eq(effectiveValues(base, profiles, null), base));
check('the base is never modified', base['pointer-speed'] === 35);

print('== what goes where');
const d = daemonConfig(base);
check('the daemon gets its keys under their own names', d['pointer-speed'] === 35 && d['hold-next-action'] === 'start-presentation' && Array.isArray(d['timer-alerts']));
check('the daemon does not get appearance or extension settings', !('laser-color' in d) && !('profiles' in d) && !('show-indicator' in d) && !('highlight-size' in d));
const o = overlayConfig(base);
check('the overlay gets fractions', o.highlight.contrast === 0.8 && o.highlight.size === 0.43 && o.magnify.size === 0.8 && o.laser.size === 0.12);
check('and colours', o.magnify.color === '#00f8be' && o.laser.color === '#ff0000');

print('== editing profiles');
let p = addProfile({}, 'a.desktop');
check('adding a profile', eq(p, {'a.desktop': {}}));
check('adding it again changes nothing', addProfile(p, 'a.desktop') === p);
const p2 = setOverride(p, 'a.desktop', 'pointer-speed', 60);
check('an override is stored', eq(p2, {'a.desktop': {'pointer-speed': 60}}));
check('the input was not modified', eq(p, {'a.desktop': {}}));
check('an invalid value is refused', setOverride(p2, 'a.desktop', 'pointer-speed', 500) === p2);
check('a key that is not a profile setting is refused', setOverride(p2, 'a.desktop', 'show-indicator', false) === p2 && setOverride(p2, 'a.desktop', 'profiles', '{}') === p2);
check('an unknown key is refused', setOverride(p2, 'a.desktop', 'nonsense', 1) === p2);
const p3 = setOverride(p2, 'b.desktop', 'freeze-effects', false);
check('profiles of several applications', eq(p3, {'a.desktop': {'pointer-speed': 60}, 'b.desktop': {'freeze-effects': false}}));
check('clearing an override', eq(clearOverride(p3, 'a.desktop', 'pointer-speed'), {'a.desktop': {}, 'b.desktop': {'freeze-effects': false}}));
check('clearing what is not there changes nothing', clearOverride(p3, 'a.desktop', 'laser-size') === p3 && clearOverride(p3, 'c.desktop', 'laser-size') === p3);
check('removing a profile', eq(removeProfile(p3, 'a.desktop'), {'b.desktop': {'freeze-effects': false}}));
check('what the window writes, the extension reads back', eq(parseProfiles(serializeProfiles(p3)), p3));
const listProfile = setOverride({}, 'a.desktop', 'hold-next-shortcut', [29, 42, 25]);
check('a list value is copied', eq(listProfile, {'a.desktop': {'hold-next-shortcut': [29, 42, 25]}}));

print('== timer alert slots');
check('slots from alerts', eq(alertsToSlots([5]), [5, 0, 0]) && eq(alertsToSlots([10, 5, 1]), [10, 5, 1]) && eq(alertsToSlots([]), [0, 0, 0]));
check('alerts from slots keep their positions (a row being edited must not move)', eq(slotsToAlerts([0, 5, 10]), [0, 5, 10]) && eq(slotsToAlerts([0, 0, 0]), [0, 0, 0]));
check('slots are clamped and made whole', eq(slotsToAlerts([-3, 1000, 2.5]), [0, 600, 0]));
check('a list with zeros is a valid timer-alerts value, one with a fourth slot or a value over 600 is not', validValue('timer-alerts', [0, 5, 0]) && !validValue('timer-alerts', [1, 2, 3, 4]) && !validValue('timer-alerts', [601]));
check('modifier codes', isModifierCode(29) && isModifierCode(42) && isModifierCode(125) && !isModifierCode(25));

print(failures ? `FAILED: ${failures}` : 'all checks passed');
System.exit(failures ? 1 : 0);
