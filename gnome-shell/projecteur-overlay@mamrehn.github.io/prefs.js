// SPDX-License-Identifier: MIT
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

import {DaemonClient} from './daemonClient.js';
import {buildPreferences} from './prefsUI.js';

export default class ProjecteurOverlayPreferences extends ExtensionPreferences {
    fillPreferencesWindow(window) {
        const settings = this.getSettings();
        let registry = null;
        const daemon = new DaemonClient(status => registry?.updateStatus(status));
        registry = buildPreferences(window, settings, daemon);
        window.set_default_size(720, 760);
        window.connect('close-request', () => {
            registry.destroy();
            daemon.destroy();
            return false;
        });
    }
}
