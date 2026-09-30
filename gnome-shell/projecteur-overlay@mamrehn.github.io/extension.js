// SPDX-License-Identifier: MIT
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

import {Overlay} from './overlay.js';
import {OverlayService} from './service.js';

export default class ProjecteurOverlayExtension extends Extension {
    enable() {
        // Test builds only (the headless harness sets this variable): let the harness take screenshots
        // through the shell's D-Bus API, which otherwise only answers a fixed list of system services.
        if (GLib.getenv('PROJECTEUR_OVERLAY_TESTING') === '1')
            global.context.unsafe_mode = true;

        this._overlay = new Overlay();
        this._service = new OverlayService(this._overlay);
        this._service.export();
        console.log('projecteur-overlay: enabled');
    }

    disable() {
        this._service?.unexport();
        this._service = null;
        this._overlay?.destroy();
        this._overlay = null;
        console.log('projecteur-overlay: disabled');
    }
}
