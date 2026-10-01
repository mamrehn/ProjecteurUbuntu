// SPDX-License-Identifier: MIT
// A plain window with a known application id, for the per-application profile tests (run by ext_features.py).
import Gtk from 'gi://Gtk?version=4.0';

const app = new Gtk.Application({application_id: 'org.projecteur.TestApp'});
app.connect('activate', () => {
    new Gtk.ApplicationWindow({application: app, title: 'Projecteur test window', default_width: 400, default_height: 300}).present();
});
app.run([]);
