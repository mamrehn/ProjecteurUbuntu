// SPDX-License-Identifier: MIT
// The settings window. Builds the pages from one table of rows and two "stores": the general settings, and the
// settings of one application's profile (which show the general value until the user changes them). No imports from
// the shell or the Extensions app: prefs.js is a thin wrapper, and tests/headless/prefstest.js drives this module.
import Adw from 'gi://Adw';
import Gdk from 'gi://Gdk';
import Gio from 'gi://Gio';
import GioUnix from 'gi://GioUnix';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk';

import {
    HOLD_ACTIONS, KEYS, addProfile, alertsToSlots, clearOverride, effectiveValues, isModifierCode, parseProfiles, removeProfile,
    serializeProfiles, setOverride, slotsToAlerts,
} from './settingsModel.js';
import {describeStatus} from './statusText.js';

const ACTION_LABELS = {
    'none': 'Nothing',
    'start-presentation': 'Start presentation (F5)',
    'blank-screen': 'Blank screen (B)',
    'fast-forward': 'Fast forward',
    'fast-rewind': 'Fast rewind',
    'volume': 'Volume (tilt up/down)',
    'scroll': 'Scroll (tilt up/down)',
    'shortcut': 'Keyboard shortcut',
};

/**
 * Every row of the settings, grouped. `kind` selects the widget. The same table builds the general pages and the
 * rows inside a profile.
 */
export const PAGES = [
    {id: 'effects', title: 'Pointer effects', icon: 'find-location-symbolic', groups: [
        {title: 'Highlight', description: 'Darkens everything except a circle around the pointer.', rows: [
            {key: 'highlight-enabled', kind: 'switch', title: 'Enabled'},
            {key: 'highlight-contrast', kind: 'scale', title: 'Contrast', unit: '%'},
            {key: 'highlight-size', kind: 'scale', title: 'Size', unit: '%'},
        ]},
        {title: 'Magnify', description: 'A live, round magnifier (2x) around the pointer.', rows: [
            {key: 'magnify-enabled', kind: 'switch', title: 'Enabled'},
            {key: 'magnify-size', kind: 'scale', title: 'Size', unit: '%'},
            {key: 'magnify-color', kind: 'color', title: 'Ring colour'},
        ]},
        {title: 'Laser', description: 'A red dot at the pointer.', rows: [
            {key: 'laser-enabled', kind: 'switch', title: 'Enabled'},
            {key: 'laser-size', kind: 'scale', title: 'Size', unit: '%'},
            {key: 'laser-color', kind: 'color', title: 'Colour'},
        ]},
        {title: 'Behaviour', rows: [
            {key: 'pointer-speed', kind: 'scale', title: 'Pointer speed', unit: '%',
                subtitle: 'How far the effect moves when you turn the remote.'},
            {key: 'freeze-effects', kind: 'switch', title: 'Freeze effects',
                subtitle: 'The effect stays on screen after you release the action button. A short click hides it, a double click switches to the next effect.'},
            {key: 'recenter-effects', kind: 'switch', title: 'Re-center effects',
                subtitle: 'On a slide change a visible effect moves back to the centre of its screen.'},
            {key: 'cursor-control', kind: 'switch', title: 'Cursor control',
                subtitle: 'The remote moves the mouse cursor (to use videos and links) instead of showing effects.'},
        ]},
    ]},
    {id: 'buttons', title: 'Buttons', icon: 'input-gaming-symbolic', groups: [
        {title: 'Hold Next',
            description: 'Press and hold for about a second. Fast forward and rewind repeat the Next or Back key while you hold; volume and scrolling follow the remote as you tilt it up and down.',
            rows: [
            {key: 'hold-next-action', kind: 'choice', title: 'Action'},
            {key: 'hold-next-shortcut', kind: 'shortcut', title: 'Shortcut', needs: ['hold-next-action', 'shortcut']},
        ]},
        {title: 'Hold Back', rows: [
            {key: 'hold-back-action', kind: 'choice', title: 'Action'},
            {key: 'hold-back-shortcut', kind: 'shortcut', title: 'Shortcut', needs: ['hold-back-action', 'shortcut']},
        ]},
    ]},
    {id: 'vibration', title: 'Vibration and timer', icon: 'preferences-system-time-symbolic', groups: [
        {title: 'Vibration', rows: [
            {key: 'vibration-intensity', kind: 'scale', title: 'Intensity', unit: '%', subtitle: '0 turns all vibration off.', test: true},
            {key: 'battery-warning', kind: 'switch', title: 'Battery warning', subtitle: 'The remote vibrates four short pulses when its battery is low.'},
        ]},
        {title: 'Timer', description: 'Counts down while you present. The remote vibrates two short pulses at each alert and three when the time is up.', rows: [
            {key: 'timer-enabled', kind: 'switch', title: 'Timer'},
            {key: 'timer-minutes', kind: 'spin', title: 'Duration', subtitle: 'Minutes', min: 1, max: 600},
            {key: 'timer-auto-start', kind: 'switch', title: 'Start automatically', subtitle: 'With the first slide change or "Start presentation".'},
            {key: 'timer-notification', kind: 'switch', title: 'Vibrate for alerts'},
            {key: 'timer-alerts', kind: 'alerts', title: 'Alert'},
            {key: 'timer-minute-pulses', kind: 'switch', title: 'Count the minutes',
                subtitle: 'The remote buzzes the minute of the talk, like a tally: minutes 1 to 4 are that many short pulses, 5 is one long pulse, 6 to 9 a long pulse and 1 to 4 short ones, 10 is two long pulses, and 11 starts again like 1.'},
        ], minuteTry: true},
    ]},
];

/** The combination of keys (evdev codes, modifiers first) for a key press in a GTK key event. */
export function chordFromKeyEvent(keyval, keycode, state) {
    const code = keycode - 8;   // GDK hardware keycodes are evdev codes + 8
    const mods = [];
    if (state & Gdk.ModifierType.CONTROL_MASK) mods.push(29);
    if (state & Gdk.ModifierType.SHIFT_MASK) mods.push(42);
    if (state & Gdk.ModifierType.ALT_MASK) mods.push(56);
    if (state & Gdk.ModifierType.SUPER_MASK) mods.push(125);
    if (isModifierCode(code))
        return null;   // a modifier alone is not a shortcut yet
    return [...mods, code];
}

/** "Ctrl+Shift+P" for a chord. */
export function chordLabel(codes, keymapLookup = null) {
    const names = {29: 'Ctrl', 97: 'Ctrl', 42: 'Shift', 54: 'Shift', 56: 'Alt', 100: 'AltGr', 125: 'Super', 126: 'Super',
        1: 'Esc', 14: 'Backspace', 15: 'Tab', 28: 'Enter', 57: 'Space', 102: 'Home', 103: 'Up', 104: 'Page Up', 105: 'Left',
        106: 'Right', 107: 'End', 108: 'Down', 109: 'Page Down', 110: 'Insert', 111: 'Delete'};
    const rows = {16: 'q', 17: 'w', 18: 'e', 19: 'r', 20: 't', 21: 'y', 22: 'u', 23: 'i', 24: 'o', 25: 'p', 30: 'a', 31: 's',
        32: 'd', 33: 'f', 34: 'g', 35: 'h', 36: 'j', 37: 'k', 38: 'l', 44: 'z', 45: 'x', 46: 'c', 47: 'v', 48: 'b', 49: 'n', 50: 'm'};
    return codes.map(c => {
        if (names[c])
            return names[c];
        if (rows[c])
            return rows[c].toUpperCase();
        if (c >= 2 && c <= 10)
            return String(c - 1);
        if (c === 11)
            return '0';
        if (c >= 59 && c <= 68)
            return `F${c - 58}`;
        if (c === 87 || c === 88)
            return `F${c - 76}`;
        return keymapLookup?.(c) ?? `Key ${c}`;
    }).join('+');
}

// -- stores ---------------------------------------------------------------------------------------------
export class SettingsStore {
    constructor(settings) {
        this._settings = settings;
    }

    get isProfile() { return false; }
    get(key) { return this._settings.get_value(key).recursiveUnpack(); }
    set(key, value) { this._settings.set_value(key, new GLib.Variant(KEYS[key].type, value)); }
    isOverridden() { return false; }
    reset() {}
    connect(callback) { return this._settings.connect('changed', (_s, key) => callback(key)); }
    disconnect(id) { this._settings.disconnect(id); }
}

/** The settings of one application: its own values where it has them, the general ones otherwise. */
export class ProfileStore {
    constructor(settings, appId) {
        this._settings = settings;
        this._appId = appId;
    }

    get isProfile() { return true; }
    _profiles() { return parseProfiles(this._settings.get_string('profiles')); }
    _write(profiles) { this._settings.set_string('profiles', serializeProfiles(profiles)); }
    _general() {
        const out = {};
        for (const key of Object.keys(KEYS))
            out[key] = this._settings.get_value(key).recursiveUnpack();
        return out;
    }

    get(key) { return effectiveValues(this._general(), this._profiles(), this._appId)[key]; }
    set(key, value) { this._write(setOverride(addProfile(this._profiles(), this._appId), this._appId, key, value)); }
    isOverridden(key) { return key in (this._profiles()[this._appId] ?? {}); }
    reset(key) { this._write(clearOverride(this._profiles(), this._appId, key)); }
    connect(callback) { return this._settings.connect('changed', (_s, key) => callback(key)); }
    disconnect(id) { this._settings.disconnect(id); }
}

// -- rows -----------------------------------------------------------------------------------------------
/**
 * Builds the widget for one table row. Returns {row, refresh, widgets}. `refresh()` shows the store's current value;
 * a change made by the user writes to the store, a change in the store refreshes the widget.
 */
function makeRow(desc, store, ctx) {
    const k = KEYS[desc.key];
    const guard = {busy: false};
    const apply = fn => {   // run `fn` without it being seen as a user change
        guard.busy = true;
        try { fn(); } finally { guard.busy = false; }
    };
    let row, refreshWidget, widgets = {};
    const title = store.isProfile && desc.profileTitle ? desc.profileTitle : desc.title;

    switch (desc.kind) {
    case 'switch': {
        row = new Adw.SwitchRow({title, subtitle: desc.subtitle ?? ''});
        row.connect('notify::active', () => { if (!guard.busy) store.set(desc.key, row.active); });
        refreshWidget = () => { row.active = store.get(desc.key); };
        widgets = {switch: row};
        break;
    }
    case 'scale': {
        row = new Adw.ActionRow({title, subtitle: desc.subtitle ?? ''});
        const scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, k.min, k.max, 1);
        scale.set_size_request(240, -1);
        scale.set_valign(Gtk.Align.CENTER);
        scale.set_draw_value(true);
        scale.set_value_pos(Gtk.PositionType.RIGHT);
        scale.set_format_value_func((_s, v) => `${Math.round(v)}${desc.unit === '%' ? ' %' : ''}`);
        scale.connect('value-changed', () => { if (!guard.busy) store.set(desc.key, Math.round(scale.get_value())); });
        row.add_suffix(scale);
        refreshWidget = () => { scale.set_value(store.get(desc.key)); };
        widgets = {scale};
        if (desc.test && ctx.daemon) {
            const test = new Gtk.Button({label: 'Test', valign: Gtk.Align.CENTER, tooltip_text: 'Vibrate the remote three times'});
            test.connect('clicked', () => ctx.daemon.vibrate(3));
            row.add_suffix(test);
            widgets.test = test;
        }
        break;
    }
    case 'spin': {
        row = new Adw.SpinRow({title, subtitle: desc.subtitle ?? '', adjustment: new Gtk.Adjustment({lower: desc.min, upper: desc.max, step_increment: 1, page_increment: 5})});
        row.connect('notify::value', () => { if (!guard.busy) store.set(desc.key, Math.round(row.value)); });
        refreshWidget = () => { row.value = store.get(desc.key); };
        widgets = {spin: row};
        break;
    }
    case 'choice': {
        row = new Adw.ComboRow({title, subtitle: desc.subtitle ?? '', model: Gtk.StringList.new(HOLD_ACTIONS.map(a => ACTION_LABELS[a]))});
        row.connect('notify::selected', () => { if (!guard.busy) store.set(desc.key, HOLD_ACTIONS[row.selected]); });
        refreshWidget = () => { row.selected = Math.max(0, HOLD_ACTIONS.indexOf(store.get(desc.key))); };
        widgets = {combo: row};
        break;
    }
    case 'color': {
        row = new Adw.ActionRow({title, subtitle: desc.subtitle ?? ''});
        const button = new Gtk.ColorDialogButton({dialog: new Gtk.ColorDialog({with_alpha: false}), valign: Gtk.Align.CENTER});
        button.connect('notify::rgba', () => {
            if (guard.busy)
                return;
            const c = button.rgba;
            const hex = n => Math.round(n * 255).toString(16).padStart(2, '0');
            store.set(desc.key, `#${hex(c.red)}${hex(c.green)}${hex(c.blue)}`);
        });
        row.add_suffix(button);
        refreshWidget = () => {
            const c = new Gdk.RGBA();
            if (c.parse(store.get(desc.key)))
                button.rgba = c;
        };
        widgets = {color: button};
        break;
    }
    case 'shortcut': {
        row = new Adw.ActionRow({title, subtitle: desc.subtitle ?? ''});
        const label = new Gtk.Label({valign: Gtk.Align.CENTER, css_classes: ['dim-label']});
        const record = new Gtk.Button({label: 'Record…', valign: Gtk.Align.CENTER});
        const clear = new Gtk.Button({icon_name: 'edit-clear-symbolic', valign: Gtk.Align.CENTER, tooltip_text: 'Remove the shortcut', css_classes: ['flat']});
        const keys = new Gtk.EventControllerKey();
        let recording = false;
        const stop = () => {
            recording = false;
            record.label = 'Record…';
            refreshWidget();
        };
        keys.connect('key-pressed', (_c, keyval, keycode, state) => {
            if (!recording)
                return false;
            if (keyval === Gdk.KEY_Escape) {
                stop();
                return true;
            }
            const chord = chordFromKeyEvent(keyval, keycode, state);
            if (chord) {
                store.set(desc.key, chord);
                stop();
            }
            return true;
        });
        record.add_controller(keys);
        record.connect('clicked', () => {
            recording = true;
            record.label = 'Press the keys… (Esc cancels)';
            record.grab_focus();
        });
        clear.connect('clicked', () => store.set(desc.key, []));
        row.add_suffix(label);
        row.add_suffix(record);
        row.add_suffix(clear);
        refreshWidget = () => { label.label = chordLabel(store.get(desc.key)) || 'Not set'; };
        widgets = {label, record, clear, keys, press: (keyval, keycode, state) => keys.emit('key-pressed', keyval, keycode, state)};
        break;
    }
    default:
        throw new Error(`unknown row kind ${desc.kind}`);
    }

    // a profile shows a marker and a "back to general" button for the settings it overrides
    let reset = null;
    if (store.isProfile) {
        reset = new Gtk.Button({icon_name: 'edit-undo-symbolic', valign: Gtk.Align.CENTER, css_classes: ['flat'], tooltip_text: 'Use the general setting again'});
        reset.connect('clicked', () => store.reset(desc.key));
        row.add_suffix(reset);
        widgets.reset = reset;
    }

    const refresh = () => {
        apply(refreshWidget);
        if (reset)
            reset.visible = store.isOverridden(desc.key);
        if (desc.needs)
            row.sensitive = store.get(desc.needs[0]) === desc.needs[1];
    };
    refresh();
    return {row, refresh, widgets, key: desc.key};
}

/** The three alert slots of the timer as spin rows. */
function makeAlertRows(desc, store) {
    const rows = [];
    const guard = {busy: false};
    for (let i = 0; i < 3; i++) {
        const row = new Adw.SpinRow({
            title: `${desc.title} ${i + 1}`,
            subtitle: 'Minutes before the end (0 = off)',
            adjustment: new Gtk.Adjustment({lower: 0, upper: 600, step_increment: 1, page_increment: 5}),
        });
        row.connect('notify::value', () => {
            if (guard.busy)
                return;
            const slots = alertsToSlots(store.get(desc.key));
            slots[i] = Math.round(row.value);
            store.set(desc.key, slotsToAlerts(slots));
        });
        rows.push(row);
    }
    const refresh = () => {
        guard.busy = true;
        const slots = alertsToSlots(store.get(desc.key));
        rows.forEach((r, i) => { r.value = slots[i]; });
        guard.busy = false;
    };
    refresh();
    return {rows, refresh};
}

/** Fill `add` (a function taking a row) with the rows of a group. */
function groupRows(group, store, ctx, registry, titlePrefix = null) {
    const out = [];
    for (const desc of group.rows) {
        const d = titlePrefix ? {...desc, title: `${titlePrefix}: ${desc.title}`} : desc;
        if (desc.kind === 'alerts') {
            const a = makeAlertRows(d, store);
            a.rows.forEach(r => out.push(r));
            registry.refreshers.push(a.refresh);
            registry.alertRows = a.rows;
        } else {
            const r = makeRow(d, store, ctx);
            out.push(r.row);
            registry.refreshers.push(r.refresh);
            registry.rows[desc.key] = r;
        }
    }
    return out;
}

function newRegistry() {
    return {rows: {}, refreshers: [], alertRows: null};
}

// -- the pages ------------------------------------------------------------------------------------------
/** A row to learn the minute code: pick a minute, feel it on the remote. */
function makeMinuteTryRow(ctx, registry) {
    const row = new Adw.SpinRow({
        title: 'Try the minute code',
        subtitle: 'Pick a minute of the talk and press Play; the remote buzzes it.',
        adjustment: new Gtk.Adjustment({lower: 1, upper: 60, step_increment: 1, page_increment: 5, value: 5}),
    });
    const play = new Gtk.Button({label: 'Play', valign: Gtk.Align.CENTER});
    play.connect('clicked', () => ctx.daemon?.vibrateMinute(Math.round(row.value)));
    row.add_suffix(play);
    registry.minuteTry = {row, play};
    return row;
}

function buildGeneralPage(pageDesc, store, ctx, registry) {
    const page = new Adw.PreferencesPage({title: pageDesc.title, icon_name: pageDesc.icon, name: pageDesc.id});
    for (const g of pageDesc.groups) {
        const group = new Adw.PreferencesGroup({title: g.title, description: g.description ?? ''});
        for (const row of groupRows(g, store, ctx, registry))
            group.add(row);
        if (g.minuteTry && ctx.daemon)
            group.add(makeMinuteTryRow(ctx, registry));
        page.add(group);
    }
    return page;
}

function appName(appId) {
    const info = GioUnix.DesktopAppInfo.new(appId);
    return info ? info.get_name() : appId.replace(/\.desktop$/, '');
}

function appIcon(appId) {
    const info = GioUnix.DesktopAppInfo.new(appId);
    return info?.get_icon() ?? new Gio.ThemedIcon({name: 'application-x-executable-symbolic'});
}

/** The installed applications that could get a profile, by name. */
export function installedApps() {
    return Gio.AppInfo.get_all().filter(a => a.should_show() && a.get_id()).sort((a, b) => a.get_name().localeCompare(b.get_name()));
}

function buildProfilesPage(settings, ctx, registry) {
    const page = new Adw.PreferencesPage({title: 'Applications', icon_name: 'view-app-grid-symbolic', name: 'applications'});
    const intro = new Adw.PreferencesGroup({
        title: 'Settings per application',
        description: 'While an application has the keyboard focus, its settings replace the general ones. Only the settings you change here differ; the others follow the general settings.',
    });
    const addButton = new Gtk.Button({label: 'Add application…', halign: Gtk.Align.START, css_classes: ['suggested-action']});
    const list = new Adw.PreferencesGroup();
    intro.add(addButton);
    page.add(intro);
    page.add(list);
    registry.addApplicationButton = addButton;

    let rows = [];
    const rebuild = () => {
        for (const r of rows)
            list.remove(r);
        rows = [];
        registry.profileRows = {};
        const profiles = parseProfiles(settings.get_string('profiles'));
        for (const appId of Object.keys(profiles).sort()) {
            const store = new ProfileStore(settings, appId);
            const expander = new Adw.ExpanderRow({title: appName(appId), subtitle: appId});
            expander.add_prefix(new Gtk.Image({gicon: appIcon(appId), pixel_size: 32}));
            const profileRegistry = newRegistry();
            for (const p of PAGES)
                for (const g of p.groups)
                    for (const r of groupRows(g, store, ctx, profileRegistry, g.title === 'Behaviour' ? null : g.title))
                        expander.add_row(r);
            const remove = new Adw.ActionRow({title: 'Remove this application'});
            const removeButton = new Gtk.Button({label: 'Remove', valign: Gtk.Align.CENTER, css_classes: ['destructive-action']});
            removeButton.connect('clicked', () => settings.set_string('profiles', serializeProfiles(removeProfile(parseProfiles(settings.get_string('profiles')), appId))));
            remove.add_suffix(removeButton);
            expander.add_row(remove);
            list.add(expander);
            rows.push(expander);
            registry.profileRows[appId] = {expander, store, registry: profileRegistry, removeButton};
        }
    };
    rebuild();

    // The profiles key is written by this window and by anything else; rebuild when the list of applications changes
    // and refresh the rows of the profiles otherwise.
    let known = Object.keys(parseProfiles(settings.get_string('profiles'))).sort().join('\n');
    const id = settings.connect('changed', (_s, key) => {
        if (key === 'profiles') {
            const now = Object.keys(parseProfiles(settings.get_string('profiles'))).sort().join('\n');
            if (now !== known) {
                known = now;
                rebuild();
                return;
            }
        }
        for (const p of Object.values(registry.profileRows ?? {}))
            p.registry.refreshers.forEach(f => f());
    });
    registry.disconnectors.push(() => settings.disconnect(id));

    addButton.connect('clicked', () => {
        registry.chooserDialog = chooseApplication(ctx.window, appId => {
            settings.set_string('profiles', serializeProfiles(addProfile(parseProfiles(settings.get_string('profiles')), appId)));
        });
    });
    return page;
}

function chooseApplication(parent, onChosen) {
    const dialog = new Adw.Dialog({title: 'Add application', content_width: 420, content_height: 520});
    const view = new Adw.ToolbarView();
    view.add_top_bar(new Adw.HeaderBar());
    const box = new Gtk.Box({orientation: Gtk.Orientation.VERTICAL, spacing: 8, margin_top: 8, margin_bottom: 8, margin_start: 12, margin_end: 12});
    const search = new Gtk.SearchEntry({placeholder_text: 'Search applications'});
    const list = new Gtk.ListBox({selection_mode: Gtk.SelectionMode.NONE, css_classes: ['boxed-list']});
    const scroller = new Gtk.ScrolledWindow({vexpand: true, child: list});
    for (const app of installedApps()) {
        const row = new Adw.ActionRow({title: GLib.markup_escape_text(app.get_name(), -1), subtitle: app.get_id(), activatable: true});
        row.add_prefix(new Gtk.Image({gicon: app.get_icon() ?? new Gio.ThemedIcon({name: 'application-x-executable-symbolic'}), pixel_size: 32}));
        row._appId = app.get_id();
        row._search = `${app.get_name()} ${app.get_id()}`.toLowerCase();
        row.connect('activated', () => {
            dialog.close();
            onChosen(row._appId);
        });
        list.append(row);
    }
    list.set_filter_func(row => !search.text || row._search.includes(search.text.toLowerCase()));
    search.connect('search-changed', () => list.invalidate_filter());
    box.append(search);
    box.append(scroller);
    view.set_content(box);
    dialog.set_child(view);
    dialog.present(parent);
    return dialog;
}

const BATTERY_ICON = pct => pct > 80 ? 'battery-level-90-symbolic' : pct > 50 ? 'battery-level-70-symbolic' : pct > 20 ? 'battery-level-40-symbolic' : 'battery-level-10-symbolic';

function buildRemotePage(ctx, registry) {
    const page = new Adw.PreferencesPage({title: 'Remote', icon_name: 'input-mouse-symbolic', name: 'remote'});
    const daemonGroup = new Adw.PreferencesGroup({title: 'Background service'});
    const daemonRow = new Adw.ActionRow({title: 'projecteurd'});
    daemonGroup.add(daemonRow);
    page.add(daemonGroup);

    const group = new Adw.PreferencesGroup({title: 'Spotlight'});
    const connection = new Adw.ActionRow({title: 'Connection'});
    const battery = new Adw.ActionRow({title: 'Battery'});
    const batteryIcon = new Gtk.Image({icon_name: BATTERY_ICON(100)});
    battery.add_prefix(batteryIcon);
    const firmware = new Adw.ActionRow({title: 'Firmware'});
    const bootloader = new Adw.ActionRow({title: 'Bootloader'});
    for (const r of [connection, battery, firmware, bootloader]) {
        r.add_css_class('property');
        group.add(r);
    }
    page.add(group);

    const timerGroup = new Adw.PreferencesGroup({title: 'Timer'});
    const timer = new Adw.ActionRow({title: 'Time left'});
    const start = new Gtk.Button({label: 'Start', valign: Gtk.Align.CENTER});
    const pause = new Gtk.Button({label: 'Pause / resume', valign: Gtk.Align.CENTER});
    const reset = new Gtk.Button({label: 'Reset', valign: Gtk.Align.CENTER});
    start.connect('clicked', () => ctx.daemon?.timerStart());
    pause.connect('clicked', () => ctx.daemon?.timerPause());
    reset.connect('clicked', () => ctx.daemon?.timerReset());
    for (const b of [start, pause, reset])
        timer.add_suffix(b);
    timerGroup.add(timer);
    page.add(timerGroup);

    const show = (row, text) => {
        row.subtitle = text;
        row.visible = !!text;
    };
    const update = status => {
        const d = describeStatus(status);
        daemonRow.subtitle = status ? 'Running' : 'Not running. Start it with: systemctl --user start projecteurd';
        daemonRow.add_css_class('property');
        show(connection, d.connection);
        show(battery, d.battery.replace(/^Battery /, ''));
        show(firmware, d.firmware.replace(/^Firmware /, ''));
        show(bootloader, status?.bootloader ?? '');
        group.visible = !!status;
        timerGroup.visible = !!status;
        timer.subtitle = d.timer.replace(/^Timer /, '') || 'Not started';
        if (status?.battery)
            batteryIcon.icon_name = BATTERY_ICON(status.battery.percent);
    };
    registry.updateStatus = update;
    registry.remote = {daemonRow, connection, battery, firmware, bootloader, timer, start, pause, reset};
    update(ctx.daemon?.status ?? null);
    return page;
}

/**
 * Add all pages to `window` (an Adw.PreferencesWindow). `ctx` = {window, settings, daemon}; `daemon` may be null or
 * any object with status/vibrate/timerStart/timerPause/timerReset (the real one is a DaemonClient).
 * Returns a registry for the tests, with `destroy()` to disconnect everything.
 */
export function buildPreferences(window, settings, daemon) {
    const ctx = {window, settings, daemon};
    const registry = newRegistry();
    registry.disconnectors = [];
    const store = new SettingsStore(settings);

    for (const pageDesc of PAGES)
        window.add(buildGeneralPage(pageDesc, store, ctx, registry));
    window.add(buildProfilesPage(settings, ctx, registry));
    window.add(buildRemotePage(ctx, registry));

    // keep every row in step with changes made elsewhere (another window, gsettings, the profile list)
    const id = store.connect(() => registry.refreshers.forEach(f => f()));
    registry.disconnectors.push(() => store.disconnect(id));
    registry.destroy = () => registry.disconnectors.forEach(f => f());
    return registry;
}
