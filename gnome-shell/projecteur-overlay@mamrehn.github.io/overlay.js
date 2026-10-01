// SPDX-License-Identifier: MIT
import Clutter from 'gi://Clutter';
import St from 'gi://St';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {CircleMaskEffect} from './maskEffect.js';

export const MODES = ['highlight', 'magnify', 'laser'];

// Defaults are the values of the reference Windows configuration (see doc/ubuntu/FEATURE-PARITY.md).
//
// CALIBRATION. The mapping from the percentage settings to pixels was measured on a 1920x1080 screencast
// of Logi Options+ with exactly these settings (one data point per effect, so the curves are HYPOTHESES):
//   highlight size 43 % -> hole 429 px      magnify size 80 % -> lens 703 px (zoom 2.005 -> 2.0)
//   two points fit  diameter / screen height = 0.102 + 0.686 * size   (shared by both effects)
//   highlight contrast 80 % -> screen drawn at 29 % brightness  => black overlay alpha 0.71 (assumed linear)
//   laser size 12 % -> saturated core about 22 px, soft glow about 40 px (assumed proportional to size)
// More data points (other sizes/contrasts) are needed to confirm the curves.
const DEFAULTS = {
    highlight: {contrast: 0.80, size: 0.43},
    magnify: {size: 0.80, zoom: 2.0, color: '#00f8be'},
    laser: {size: 0.12, color: '#ff0000'},
};
export const CAL = {
    sizeBase: 0.102, sizeSlope: 0.686,          // diameter / screen height = base + slope * size
    dimAlphaPerContrast: 0.8875,                // overlay alpha = this * contrast (0.71 at 80 %)
    laserCorePerSize: 22 / 0.12,                // px of saturated core per unit size
    laserGlowFactor: 40 / 22,                   // whole dot / core
    ringWidth: 4, ringOutline: 4,               // px, teal band and the black outline outside it
};

const TEST_TILE = 80;

/** Colours are written into CSS: only plain #rrggbb is accepted (anything on the session bus can call SetConfig). */
export const COLOR_PATTERN = /^#[0-9a-fA-F]{6}$/;

function clamp(value, min, max, fallback) {
    return typeof value === 'number' && Number.isFinite(value) ? Math.min(Math.max(value, min), max) : fallback;
}

/** `rgba(r,g,b,a)` for a validated `#rrggbb`. */
export function rgba(color, alpha) {
    const n = parseInt(color.slice(1), 16);
    return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${alpha})`;
}

/** Only known keys, numbers inside their range, colours in #rrggbb; everything else is dropped. */
export function sanitizeConfig(partial) {
    const out = {};
    const limits = {contrast: [0, 1], size: [0, 1], zoom: [1, 8]};
    for (const [mode, values] of Object.entries(partial ?? {})) {
        if (!MODES.includes(mode) || values === null || typeof values !== 'object')
            continue;
        out[mode] = {};
        for (const [key, value] of Object.entries(values)) {
            if (key in limits) {
                const v = clamp(value, limits[key][0], limits[key][1], null);
                if (v !== null)
                    out[mode][key] = v;
            } else if (key === 'color' && typeof value === 'string' && COLOR_PATTERN.test(value)) {
                out[mode][key] = value.toLowerCase();
            }
        }
    }
    return out;
}

/** Colour of test-pattern tile (i, j); the test harness uses the same formula. */
export function testTileColor(i, j) {
    return [(i * 53 + 40) % 256, (j * 97 + 60) % 256, ((i + j) * 71 + 20) % 256];
}

/**
 * Draws the pointer effects. All actors live on `global.stage`, as siblings ABOVE `Main.uiGroup`:
 *  - the live lens is a Clutter.Clone of `Main.uiGroup`; because the lens is not a descendant of
 *    uiGroup it can never appear in its own clone (this is how GNOME's magnifier avoids recursion);
 *  - every actor is hidden from picking, so pointer and keyboard input pass through to the windows.
 */
export class Overlay {
    constructor() {
        this._cfg = JSON.parse(JSON.stringify(DEFAULTS));
        this._mode = 'highlight';
        this._visible = false;

        const primary = Main.layoutManager.primaryMonitor;
        this._x = primary.x + primary.width / 2;
        this._y = primary.y + primary.height / 2;

        this._buildActors();
        this._sync();
    }

    // -- construction -------------------------------------------------------------------------
    _addOverlayActor(actor) {
        actor.reactive = false;
        actor.visible = false;
        global.stage.add_child(actor);
        Shell.util_set_hidden_from_pick(actor, true);
        return actor;
    }

    _buildActors() {
        // Highlight: a black sheet over one monitor with a round hole.
        this._dim = this._addOverlayActor(new St.Widget({name: 'projecteur-dim', style: 'background-color: black;'}));
        this._dimMask = new CircleMaskEffect(false);
        this._dim.add_effect(this._dimMask);

        // Magnify: lens (clone of uiGroup, rectangular clip + round mask) and a ring above it.
        this._lens = this._addOverlayActor(new St.Widget({
            name: 'projecteur-lens',
            clip_to_allocation: true,
            layout_manager: new Clutter.FixedLayout(),
        }));
        this._clone = new Clutter.Clone({source: Main.uiGroup});
        this._lens.add_child(this._clone);
        this._lensMask = new CircleMaskEffect(true);
        this._lens.add_effect(this._lensMask);
        // Ring = black outline (outer) + teal band (inner), two plain bordered actors. Do NOT use CSS
        // box-shadow for the outline: St's stretched shadow texture leaves a hairline cross through the lens.
        this._outline = this._addOverlayActor(new St.Widget({name: 'projecteur-outline'}));
        this._ring = this._addOverlayActor(new St.Widget({name: 'projecteur-ring'}));

        // Laser: a plain round dot.
        this._dot = this._addOverlayActor(new St.Widget({name: 'projecteur-dot'}));
    }

    destroy() {
        this.testPattern(false);
        for (const a of [this._dim, this._lens, this._outline, this._ring, this._dot])
            a?.destroy();
        this._dim = this._lens = this._outline = this._ring = this._dot = this._clone = null;
    }

    // -- state --------------------------------------------------------------------------------
    get mode() { return this._mode; }
    get visible() { return this._visible; }
    get position() { return [this._x, this._y]; }

    show(mode) {
        if (mode && MODES.includes(mode))
            this._mode = mode;
        this._visible = true;
        this._sync();
    }

    /** Windows behaviour: the effect appears at the mouse cursor, not where it was last hidden. */
    showAtPointer(mode) {
        const [x, y] = global.get_pointer();
        this._x = this._clampX(x);
        this._y = this._clampY(y);
        this.show(mode);
    }

    pointer() {
        return global.get_pointer();
    }

    hide() {
        this._visible = false;
        this._sync();
    }

    setMode(mode) {
        if (!MODES.includes(mode))
            return;
        this._mode = mode;
        this._sync();
    }

    moveTo(x, y) {
        this._x = this._clampX(x);
        this._y = this._clampY(y);
        this._sync();
    }

    moveBy(dx, dy) {
        this.moveTo(this._x + dx, this._y + dy);
    }

    recenter() {
        const m = this._monitor();
        this.moveTo(m.x + m.width / 2, m.y + m.height / 2);
    }

    /** Merge a partial config, e.g. {magnify: {zoom: 3}}. */
    setConfig(partial) {
        for (const [mode, values] of Object.entries(sanitizeConfig(partial)))
            Object.assign(this._cfg[mode], values);
        this._sync();
    }

    // -- geometry -----------------------------------------------------------------------------
    _clampX(x) { return Math.min(Math.max(x, 0), global.stage.width); }
    _clampY(y) { return Math.min(Math.max(y, 0), global.stage.height); }

    _monitor() {
        return Main.layoutManager.findMonitorForPoint(this._x, this._y) ?? Main.layoutManager.primaryMonitor;
    }

    // Diameter in px of the highlight hole and of the lens (see CAL).
    _diameter(size) {
        return Math.round(this._monitor().height * (CAL.sizeBase + CAL.sizeSlope * size));
    }

    // -- rendering ----------------------------------------------------------------------------
    _sync() {
        const on = this._visible;
        this._dim.visible = on && this._mode === 'highlight';
        this._lens.visible = this._outline.visible = this._ring.visible = on && this._mode === 'magnify';
        this._dot.visible = on && this._mode === 'laser';
        if (!on)
            return;

        switch (this._mode) {
        case 'highlight': this._syncHighlight(); break;
        case 'magnify': this._syncMagnify(); break;
        case 'laser': this._syncLaser(); break;
        }
    }

    _syncHighlight() {
        const m = this._monitor();
        const c = this._cfg.highlight;
        this._dim.set_position(m.x, m.y);
        this._dim.set_size(m.width, m.height);
        this._dim.opacity = Math.round(255 * CAL.dimAlphaPerContrast * c.contrast);
        this._dimMask.setCircle([this._x - m.x, this._y - m.y], this._diameter(c.size) / 2, [m.width, m.height]);
    }

    _syncMagnify() {
        const c = this._cfg.magnify;
        const d = this._diameter(c.size);
        const z = c.zoom;
        this._lens.set_position(Math.round(this._x - d / 2), Math.round(this._y - d / 2));
        this._lens.set_size(d, d);
        // Put the point under the pointer at the centre of the lens.
        this._clone.set_scale(z, z);
        this._clone.set_position(d / 2 - this._x * z, d / 2 - this._y * z);
        this._lensMask.setCircle([d / 2, d / 2], d / 2, [d, d]);

        this._ring.set_position(Math.round(this._x - d / 2), Math.round(this._y - d / 2));
        this._ring.set_size(d, d);
        this._ring.style = `border: ${CAL.ringWidth}px solid ${c.color}; border-radius: ${d}px;`;
        const o = CAL.ringOutline, od = d + 2 * o;
        this._outline.set_position(Math.round(this._x - od / 2), Math.round(this._y - od / 2));
        this._outline.set_size(od, od);
        this._outline.style = `border: ${o}px solid black; border-radius: ${od}px;`;
    }

    _syncLaser() {
        const c = this._cfg.laser;
        const core = Math.max(4, Math.round(CAL.laserCorePerSize * c.size));
        const d = Math.round(core * CAL.laserGlowFactor);
        this._dot.set_position(Math.round(this._x - d / 2), Math.round(this._y - d / 2));
        this._dot.set_size(d, d);
        // from the colour at the centre to the same colour, transparent, at the rim (a fixed end colour would tint the edge)
        this._dot.style = `background-gradient-direction: radial; background-gradient-start: ${c.color}; ` +
            `background-gradient-end: ${rgba(c.color, 0)}; border-radius: ${d}px;`;
    }

    // -- test support (only reachable when the service exposes it) ------------------------------
    /** A deterministic grid of coloured tiles on top of everything inside uiGroup. */
    testPattern(on) {
        if (!on) {
            this._pattern?.destroy();
            this._pattern = null;
            return;
        }
        if (this._pattern)
            return;
        const m = Main.layoutManager.primaryMonitor;
        this._pattern = new St.Widget({
            name: 'projecteur-test-pattern',
            reactive: false,
            x: m.x, y: m.y, width: m.width, height: m.height,
            layout_manager: new Clutter.FixedLayout(),
        });
        for (let j = 0; j * TEST_TILE < m.height; j++) {
            for (let i = 0; i * TEST_TILE < m.width; i++) {
                const [r, g, b] = testTileColor(i, j);
                this._pattern.add_child(new St.Widget({
                    reactive: false,
                    x: i * TEST_TILE, y: j * TEST_TILE, width: TEST_TILE, height: TEST_TILE,
                    style: `background-color: rgb(${r},${g},${b});`,
                }));
            }
        }
        Main.uiGroup.add_child(this._pattern);
    }

    /** Name of the topmost reactive actor at a stage point ('' if none): click-through check. */
    pickAt(x, y) {
        const actor = global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y);
        return actor ? `${actor.constructor.name}:${actor.name ?? ''}` : '';
    }
}
