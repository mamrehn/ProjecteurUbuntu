// SPDX-License-Identifier: MIT
// The settings of the extension in one table: the schema, the push to the daemon, the overlay configuration and the
// settings window all follow it. No gi imports: this module is also tested with plain gjs.

/** `scope`: where the setting is used. 'daemon' keys are sent to projecteurd as they are (same key names). */
export const KEYS = {
    'highlight-enabled': {type: 'b', def: true, scope: 'daemon'},
    'highlight-contrast': {type: 'i', def: 80, min: 0, max: 100, scope: 'overlay'},
    'highlight-size': {type: 'i', def: 43, min: 0, max: 100, scope: 'overlay'},
    'magnify-enabled': {type: 'b', def: true, scope: 'daemon'},
    'magnify-size': {type: 'i', def: 80, min: 0, max: 100, scope: 'overlay'},
    'magnify-color': {type: 's', def: '#00f8be', scope: 'overlay'},
    'laser-enabled': {type: 'b', def: true, scope: 'daemon'},
    'laser-size': {type: 'i', def: 12, min: 0, max: 100, scope: 'overlay'},
    'laser-color': {type: 's', def: '#ff0000', scope: 'overlay'},
    'pointer-speed': {type: 'i', def: 35, min: 0, max: 100, scope: 'daemon'},
    'cursor-control': {type: 'b', def: false, scope: 'daemon'},
    'recenter-effects': {type: 'b', def: true, scope: 'daemon'},
    'freeze-effects': {type: 'b', def: true, scope: 'daemon'},
    'hold-next-action': {type: 's', def: 'start-presentation', choices: null, scope: 'daemon'},
    'hold-back-action': {type: 's', def: 'blank-screen', choices: null, scope: 'daemon'},
    'hold-next-shortcut': {type: 'ai', def: [], scope: 'daemon'},
    'hold-back-shortcut': {type: 'ai', def: [], scope: 'daemon'},
    'vibration-intensity': {type: 'i', def: 50, min: 0, max: 100, scope: 'daemon'},
    'battery-warning': {type: 'b', def: true, scope: 'daemon'},
    'timer-enabled': {type: 'b', def: false, scope: 'daemon'},
    'timer-minutes': {type: 'i', def: 30, min: 1, max: 600, scope: 'daemon'},
    'timer-auto-start': {type: 'b', def: true, scope: 'daemon'},
    'timer-alerts': {type: 'ai', def: [5], scope: 'daemon'},
    'timer-notification': {type: 'b', def: true, scope: 'daemon'},
    // not part of a profile
    'show-indicator': {type: 'b', def: true, scope: 'extension', profile: false},
    'profiles': {type: 's', def: '{}', scope: 'extension', profile: false},
};

export const HOLD_ACTIONS = ['none', 'start-presentation', 'blank-screen', 'fast-forward', 'fast-rewind', 'volume', 'scroll', 'shortcut'];
KEYS['hold-next-action'].choices = HOLD_ACTIONS;
KEYS['hold-back-action'].choices = HOLD_ACTIONS;

/** The same pattern as overlay.js: colours end up in CSS. */
const COLOR_PATTERN = /^#[0-9a-fA-F]{6}$/;
const COLOR_KEYS = ['magnify-color', 'laser-color'];

export function isProfileKey(key) {
    return key in KEYS && KEYS[key].profile !== false;
}

/** Is `value` acceptable for `key` (type, range, choices)? Profiles come from a text field: never trust them. */
export function validValue(key, value) {
    const k = KEYS[key];
    if (!k)
        return false;
    switch (k.type) {
    case 'b':
        return typeof value === 'boolean';
    case 'i':
        return Number.isInteger(value) && (k.min === undefined || value >= k.min) && (k.max === undefined || value <= k.max);
    case 's':
        if (COLOR_KEYS.includes(key))
            return typeof value === 'string' && COLOR_PATTERN.test(value);
        return typeof value === 'string' && (!k.choices || k.choices.includes(value));
    case 'ai':
        return Array.isArray(value) && value.length <= 6 && value.every(x => Number.isInteger(x) && x >= 0);
    }
    return false;
}

export function defaults() {
    const out = {};
    for (const [key, k] of Object.entries(KEYS))
        out[key] = Array.isArray(k.def) ? [...k.def] : k.def;
    return out;
}

/** The profiles key is text; a broken value means "no profiles", never an exception in the compositor. */
export function parseProfiles(text) {
    try {
        const parsed = JSON.parse(text);
        if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed))
            return {};
        const out = {};
        for (const [app, overrides] of Object.entries(parsed)) {
            if (overrides === null || typeof overrides !== 'object' || Array.isArray(overrides))
                continue;
            const clean = {};
            for (const [key, value] of Object.entries(overrides))
                if (isProfileKey(key) && validValue(key, value))
                    clean[key] = value;
            out[app] = clean;
        }
        return out;
    } catch (e) {
        return {};
    }
}

/** The settings in force for the application with id `appId`: the general values, overridden by its profile. */
export function effectiveValues(base, profiles, appId) {
    const overrides = appId ? profiles[appId] : null;
    return overrides ? {...base, ...overrides} : {...base};
}

/** What projecteurd gets: the keys of scope 'daemon', under their own names. */
export function daemonConfig(values) {
    const out = {};
    for (const [key, k] of Object.entries(KEYS))
        if (k.scope === 'daemon' && key in values)
            out[key] = values[key];
    return out;
}

/** What the overlay gets (see Overlay.setConfig): sizes and contrast as fractions. */
export function overlayConfig(values) {
    const color = key => validValue(key, values[key]) ? values[key] : KEYS[key].def;   // a broken colour must not reach the CSS
    return {
        highlight: {contrast: values['highlight-contrast'] / 100, size: values['highlight-size'] / 100},
        magnify: {size: values['magnify-size'] / 100, color: color('magnify-color')},
        laser: {size: values['laser-size'] / 100, color: color('laser-color')},
    };
}
