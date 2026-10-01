// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

export const BUS_NAME = 'org.projecteur.Overlay';
export const OBJECT_PATH = '/org/projecteur/Overlay';

const TESTING = GLib.getenv('PROJECTEUR_OVERLAY_TESTING') === '1';

const IFACE_XML = `
<node>
  <interface name="org.projecteur.Overlay1">
    <method name="Show"><arg type="s" direction="in" name="mode"/></method>
    <method name="ShowAtPointer"><arg type="s" direction="in" name="mode"/></method>
    <method name="GetPointer"><arg type="d" direction="out" name="x"/><arg type="d" direction="out" name="y"/></method>
    <method name="Hide"/>
    <method name="SetMode"><arg type="s" direction="in" name="mode"/></method>
    <method name="MoveBy"><arg type="d" direction="in" name="dx"/><arg type="d" direction="in" name="dy"/></method>
    <method name="MoveTo"><arg type="d" direction="in" name="x"/><arg type="d" direction="in" name="y"/></method>
    <method name="Recenter"/>
    <method name="SetConfig"><arg type="s" direction="in" name="json"/></method>
    <method name="GetState">
      <arg type="b" direction="out" name="visible"/>
      <arg type="s" direction="out" name="mode"/>
      <arg type="d" direction="out" name="x"/>
      <arg type="d" direction="out" name="y"/>
    </method>
    ${TESTING ? `
    <method name="TestPattern"><arg type="b" direction="in" name="on"/></method>
    <method name="StageChildCount"><arg type="i" direction="out" name="count"/></method>
    <method name="PickAt"><arg type="d" direction="in" name="x"/><arg type="d" direction="in" name="y"/><arg type="s" direction="out" name="actor"/></method>
    ` : ''}
  </interface>
</node>`;

/** Exposes an Overlay on the session bus as org.projecteur.Overlay1. */
export class OverlayService {
    constructor(overlay) {
        this._overlay = overlay;
        this._impl = Gio.DBusExportedObject.wrapJSObject(IFACE_XML, this);
    }

    export() {
        this._impl.export(Gio.DBus.session, OBJECT_PATH);
        this._nameId = Gio.bus_own_name(
            Gio.BusType.SESSION, BUS_NAME, Gio.BusNameOwnerFlags.NONE, null, null, null);
    }

    unexport() {
        if (this._nameId) {
            Gio.bus_unown_name(this._nameId);
            this._nameId = 0;
        }
        this._impl?.unexport();
        this._impl = null;
    }

    Show(mode) { this._overlay.show(mode); }
    ShowAtPointer(mode) { this._overlay.showAtPointer(mode); }
    GetPointer() { return this._overlay.pointer(); }
    Hide() { this._overlay.hide(); }
    SetMode(mode) { this._overlay.setMode(mode); }
    MoveBy(dx, dy) { this._overlay.moveBy(dx, dy); }
    MoveTo(x, y) { this._overlay.moveTo(x, y); }
    Recenter() { this._overlay.recenter(); }
    SetConfig(json) { this._overlay.setConfig(JSON.parse(json)); }
    GetState() {
        const [x, y] = this._overlay.position;
        return [this._overlay.visible, this._overlay.mode, x, y];
    }

    TestPattern(on) { this._overlay.testPattern(on); }
    PickAt(x, y) { return this._overlay.pickAt(x, y); }
    StageChildCount() { return global.stage.get_n_children(); }
}
