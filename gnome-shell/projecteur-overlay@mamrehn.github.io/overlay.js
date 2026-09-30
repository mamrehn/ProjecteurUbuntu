// SPDX-License-Identifier: MIT
import Clutter from 'gi://Clutter';
import St from 'gi://St';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {CircleMaskEffect} from './maskEffect.js';

export const MODES = ['highlight', 'magnify', 'laser'];

// Defaults are the values of the reference Windows configuration (see doc/ubuntu/FEATURE-PARITY.md).
// The mapping from these percentages to pixels is a PLACEHOLDER until it is calibrated against the
// Windows app.
const DEFAULTS = {
    highlight: {contrast: 0.80, size: 0.43},
    magnify: {size: 0.80, zoom: 2.0, color: '#20e8b0'},
    laser: {size: 0.12, color: '#ff2020'},
};

const TEST_TILE = 80;

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
        this._ring = this._addOverlayActor(new St.Widget({name: 'projecteur-ring'}));

        // Laser: a plain round dot.
        this._dot = this._addOverlayActor(new St.Widget({name: 'projecteur-dot'}));
    }

    destroy() {
        this.testPattern(false);
        for (const a of [this._dim, this._lens, this._ring, this._dot])
            a?.destroy();
        this._dim = this._lens = this._ring = this._dot = this._clone = null;
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
        for (const [mode, values] of Object.entries(partial)) {
            if (this._cfg[mode])
                Object.assign(this._cfg[mode], values);
        }
        this._sync();
    }

    // -- geometry -----------------------------------------------------------------------------
    _clampX(x) { return Math.min(Math.max(x, 0), global.stage.width); }
    _clampY(y) { return Math.min(Math.max(y, 0), global.stage.height); }

    _monitor() {
        return Main.layoutManager.findMonitorForPoint(this._x, this._y) ?? Main.layoutManager.primaryMonitor;
    }

    // PLACEHOLDER pixel mapping, see DEFAULTS.
    _diameter(size) {
        return Math.round(this._monitor().height * (0.10 + 0.40 * size));
    }

    // -- rendering ----------------------------------------------------------------------------
    _sync() {
        const on = this._visible;
        this._dim.visible = on && this._mode === 'highlight';
        this._lens.visible = this._ring.visible = on && this._mode === 'magnify';
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
        this._dim.opacity = Math.round(255 * c.contrast);
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

        const ringWidth = 4;
        this._ring.set_position(Math.round(this._x - d / 2), Math.round(this._y - d / 2));
        this._ring.set_size(d, d);
        this._ring.style = `border: ${ringWidth}px solid ${c.color}; border-radius: ${d}px; box-shadow: 0 0 0 2px black;`;
    }

    _syncLaser() {
        const c = this._cfg.laser;
        const d = Math.round(6 + 34 * c.size);
        this._dot.set_position(Math.round(this._x - d / 2), Math.round(this._y - d / 2));
        this._dot.set_size(d, d);
        this._dot.style = `background-color: ${c.color}; border-radius: ${d}px;`;
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
