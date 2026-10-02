// SPDX-License-Identifier: MIT
// Text for a daemon status. Pure functions (no gi imports): used by the top-bar indicator and the settings window.

/** "mm:ss", or "h:mm:ss" from an hour on. */
export function formatTime(seconds) {
    const s = Math.max(0, Math.round(seconds));
    const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), r = s % 60;
    const two = n => String(n).padStart(2, '0');
    return h > 0 ? `${h}:${two(m)}:${two(r)}` : `${two(m)}:${two(r)}`;
}

/** The text lines of the menu for a daemon status (null: the daemon is not running). Pure, tested on its own. */
export function describeStatus(status) {
    if (!status)
        return {connection: 'Daemon not running', battery: '', firmware: '', timer: '', label: ''};
    const connection = status.connected
        ? `Connected over ${status.connection === 'bluetooth' ? 'Bluetooth' : 'the USB receiver'}`
        : 'Remote not connected';
    let battery = '';
    if (status.battery) {
        const b = status.battery;
        battery = `Battery ${b.percent} %${b.charging ? ' (charging)' : ''}`;
    }
    const firmware = status.firmware ? `Firmware ${status.firmware}` : '';
    const t = status.timer;
    let timer = '', label = '';
    if (t?.enabled || (t && t.state !== 'idle')) {
        const names = {idle: 'ready', running: 'running', paused: 'paused', finished: 'time is up'};
        timer = `Timer ${formatTime(t.remaining)} (${names[t.state] ?? t.state})`;
        if (t.state === 'running' || t.state === 'paused')
            label = formatTime(t.remaining);
        else if (t.state === 'finished')
            label = formatTime(0);   // the same format as while it runs
    }
    return {connection, battery, firmware, timer, label};
}
