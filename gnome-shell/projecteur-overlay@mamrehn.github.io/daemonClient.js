// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';

export const DAEMON_NAME = 'org.projecteur.Daemon';
const DAEMON_PATH = '/org/projecteur/Daemon';

const XML = `
<node>
  <interface name="org.projecteur.Daemon1">
    <method name="SetConfig"><arg type="s" direction="in" name="json"/><arg type="s" direction="out" name="problems"/></method>
    <method name="GetConfig"><arg type="s" direction="out" name="json"/></method>
    <method name="GetStatus"><arg type="s" direction="out" name="json"/></method>
    <method name="TimerStart"/>
    <method name="TimerPause"/>
    <method name="TimerReset"/>
    <method name="Vibrate"><arg type="u" direction="in" name="pulses"/></method>
    <method name="VibrateMinute"><arg type="u" direction="in" name="minute"/></method>
    <signal name="StatusChanged"><arg type="s" name="json"/></signal>
  </interface>
</node>`;
const DaemonProxy = Gio.DBusProxy.makeProxyWrapper(XML);

/**
 * The extension's and the settings window's connection to projecteurd. The daemon may start before or after the
 * client, and may be restarted: the client follows the bus name, and hands over the last configuration again
 * whenever the daemon appears.
 */
export class DaemonClient {
    /** @param {function(object|null)} onStatus called with the status object, or null when the daemon is not running */
    constructor(onStatus) {
        this._onStatus = onStatus;
        this._lastConfigJson = null;
        this._status = null;
        this._proxy = null;
        this._destroyed = false;
        this._cancellable = new Gio.Cancellable();
        new DaemonProxy(Gio.DBus.session, DAEMON_NAME, DAEMON_PATH, (proxy, error) => {
            if (this._destroyed)
                return;
            if (error) {
                console.error(`projecteur-overlay: cannot watch the daemon: ${error.message}`);
                return;
            }
            this._proxy = proxy;
            this._ownerId = proxy.connect('notify::g-name-owner', () => this._ownerChanged());
            this._signalId = proxy.connectSignal('StatusChanged', (_p, _sender, [json]) => this._setStatus(json));
            this._ownerChanged();
        }, this._cancellable, Gio.DBusProxyFlags.DO_NOT_AUTO_START | Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES);
    }

    get available() { return !!this._proxy?.g_name_owner; }
    get status() { return this._status; }

    _ownerChanged() {
        if (!this.available) {
            this._status = null;
            this._onStatus(null);
            return;
        }
        // a (re)started daemon knows nothing yet: hand over the settings, then ask how it is
        if (this._lastConfigJson)
            this._proxy.SetConfigRemote(this._lastConfigJson, () => {});
        this._proxy.GetStatusRemote(([json] = [], error) => {
            if (!error && json)
                this._setStatus(json);
        });
    }

    _setStatus(json) {
        try {
            this._status = JSON.parse(json);
        } catch (e) {
            return;
        }
        this._onStatus(this._status);
    }

    /** Send settings (an object of daemon keys). Remembered, and sent again if the daemon restarts. */
    setConfig(config) {
        const json = JSON.stringify(config);
        if (json === this._lastConfigJson)
            return;
        this._lastConfigJson = json;
        if (this.available)
            this._proxy.SetConfigRemote(json, ([problems] = [], error) => {
                if (error)
                    console.error(`projecteur-overlay: SetConfig failed: ${error.message}`);
                else if (problems)
                    console.warn(`projecteur-overlay: the daemon ignored settings: ${problems}`);
            });
    }

    timerStart() { this._call('TimerStartRemote'); }
    timerPause() { this._call('TimerPauseRemote'); }
    timerReset() { this._call('TimerResetRemote'); }
    vibrate(pulses) { this._call('VibrateRemote', pulses); }
    vibrateMinute(minute) { this._call('VibrateMinuteRemote', minute); }

    _call(method, ...args) {
        if (this.available)
            this._proxy[method](...args, () => {});
    }

    destroy() {
        this._destroyed = true;
        this._cancellable.cancel();
        if (this._proxy) {
            if (this._ownerId)
                this._proxy.disconnect(this._ownerId);
            if (this._signalId)
                this._proxy.disconnectSignal(this._signalId);
        }
        this._proxy = null;
    }
}
