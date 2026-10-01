// SPDX-License-Identifier: MIT
import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {describeStatus} from './statusText.js';

export const Indicator = GObject.registerClass(
class ProjecteurIndicator extends PanelMenu.Button {
    _init(extension, daemon) {
        super._init(0.5, 'Projecteur', false);
        this._extension = extension;
        this._daemon = daemon;

        const box = new St.BoxLayout({style_class: 'panel-status-menu-box'});
        this._icon = new St.Icon({icon_name: 'input-mouse-symbolic', style_class: 'system-status-icon'});
        this._label = new St.Label({y_align: Clutter.ActorAlign.CENTER, visible: false});
        box.add_child(this._icon);
        box.add_child(this._label);
        this.add_child(box);

        this._connection = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._battery = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._firmware = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._timer = new PopupMenu.PopupMenuItem('', {reactive: false});
        for (const item of [this._connection, this._battery, this._firmware, this._timer])
            this.menu.addMenuItem(item);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._start = this.menu.addAction('Start timer', () => this._daemon.timerStart());
        this._pause = this.menu.addAction('Pause / resume timer', () => this._daemon.timerPause());
        this._reset = this.menu.addAction('Reset timer', () => this._daemon.timerReset());
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addAction('Settings', () => this._extension.openPreferences());
        this.setStatus(null);
    }

    /** What the indicator shows, for the tests. */
    texts() {
        return {
            label: this._label.visible ? this._label.text : '',
            connection: this._connection.visible ? this._connection.label.text : '',
            battery: this._battery.visible ? this._battery.label.text : '',
            firmware: this._firmware.visible ? this._firmware.label.text : '',
            timer: this._timer.visible ? this._timer.label.text : '',
            dimmed: this._icon.opacity < 255,
        };
    }

    setStatus(status) {
        const d = describeStatus(status);
        const show = (item, text) => {
            item.label.text = text;
            item.visible = !!text;
        };
        show(this._connection, d.connection);
        show(this._battery, d.battery);
        show(this._firmware, d.firmware);
        show(this._timer, d.timer);
        const timerOn = !!status && (status.timer.enabled || status.timer.state !== 'idle');
        for (const item of [this._start, this._pause, this._reset])
            item.visible = timerOn;
        this._label.text = d.label;
        this._label.visible = !!d.label;
        // dimmed while there is no remote to talk to
        this._icon.opacity = status?.connected ? 255 : 110;
    }
});
