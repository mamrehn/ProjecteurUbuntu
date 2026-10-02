// SPDX-License-Identifier: MIT
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {DaemonClient} from './daemonClient.js';
import {Indicator} from './indicator.js';
import {Overlay} from './overlay.js';
import {OverlayService} from './service.js';
import {KEYS, daemonConfig, effectiveValues, overlayConfig, parseProfiles} from './settingsModel.js';

export default class ProjecteurOverlayExtension extends Extension {
    enable() {
        // Test builds only (the headless harness sets this variable): let the harness take screenshots
        // through the shell's D-Bus API, which otherwise only answers a fixed list of system services.
        if (GLib.getenv('PROJECTEUR_OVERLAY_TESTING') === '1') {
            this._unsafeModeBefore = global.context.unsafe_mode;
            global.context.unsafe_mode = true;
        }

        this._settings = this.getSettings();
        this._overlay = new Overlay();
        this._appId = null;
        this._pushed = null;     // test support: what was last handed to the daemon

        this._daemon = new DaemonClient(status => this._indicator?.setStatus(status));
        this._service = new OverlayService(this._overlay, () => ({
            appId: this._appId,
            pushed: this._pushed,
            indicator: this._indicator ? this._indicator.texts() : null,
        }));
        this._service.export();

        this._settingsId = this._settings.connect('changed', (_s, key) => this._settingsChanged(key));
        this._focusId = global.display.connect('notify::focus-window', () => this._focusChanged());
        this._focusChanged();
        this._syncIndicator();
        console.log('projecteur-overlay: enabled');
    }

    disable() {
        if (this._focusId)
            global.display.disconnect(this._focusId);
        if (this._settingsId)
            this._settings.disconnect(this._settingsId);
        this._focusId = this._settingsId = 0;
        this._indicator?.destroy();
        this._indicator = null;
        this._service?.unexport();
        this._service = null;
        this._daemon?.destroy();
        this._daemon = null;
        this._overlay?.destroy();
        this._overlay = null;
        this._settings = null;
        if (this._unsafeModeBefore !== undefined) {   // only what enable() changed, and only in test builds
            global.context.unsafe_mode = this._unsafeModeBefore;
            this._unsafeModeBefore = undefined;
        }
        console.log('projecteur-overlay: disabled');
    }

    _readSettings() {
        const values = {};
        for (const key of Object.keys(KEYS))
            values[key] = this._settings.get_value(key).recursiveUnpack();
        return values;
    }

    /** The application with the keyboard focus, as its desktop file id (null: none, or unknown). */
    _focusChanged() {
        const window = global.display.focus_window;
        const app = window ? Shell.WindowTracker.get_default().get_window_app(window) : null;
        const id = app?.get_id() ?? null;
        if (id === this._appId && this._pushed)
            return;
        this._appId = id;
        this._apply();
    }

    _settingsChanged(key) {
        if (key === 'show-indicator')
            this._syncIndicator();
        this._apply();
    }

    /** Work out the settings in force (general values + the profile of the focused application) and hand them on. */
    _apply() {
        const base = this._readSettings();
        const values = effectiveValues(base, parseProfiles(base.profiles), this._appId);
        this._overlay.setConfig(overlayConfig(values));
        const config = daemonConfig(values);
        this._pushed = config;
        this._daemon.setConfig(config);
    }

    _syncIndicator() {
        const wanted = this._settings.get_boolean('show-indicator');
        if (wanted && !this._indicator) {
            this._indicator = new Indicator(this, this._daemon);
            Main.panel.addToStatusArea('projecteur-overlay', this._indicator);
            this._indicator.setStatus(this._daemon.status);
        } else if (!wanted && this._indicator) {
            this._indicator.destroy();
            this._indicator = null;
        }
    }
}
