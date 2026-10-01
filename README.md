# Logitech Spotlight on Ubuntu 26.04

A fork of [Projecteur](https://github.com/gbin/Projecteur) for the **Logitech Spotlight** (original version, USB
receiver or Bluetooth) on **Ubuntu 26.04 LTS with GNOME Shell 50 on Wayland**. Upstream's own README is kept in
[README.upstream.md](README.upstream.md).

Projecteur's zoom cannot work on GNOME (the desktop does not allow it to take screenshots), and its overlay steals the
keyboard focus on Wayland. This fork therefore does the drawing **inside GNOME Shell** and keeps the device handling in a
small Qt6 daemon. The aim is everything the Windows app (Logi Options+) offers for this remote:

| Feature | How it works here |
|---|---|
| **Highlight**, **Magnify** (live, 2x, round lens), **Laser** | hold the top button: the effect appears at the mouse pointer and follows the *remote's* movement; release: it stays (freeze); short click: hides it; double click: next effect |
| Size, contrast, colours | settings window, values calibrated against recordings of the Windows app ([FEATURE-PARITY.md](doc/ubuntu/FEATURE-PARITY.md)) |
| Pointer speed, cursor control, re-center on slide change | settings window |
| Hold **Next** / **Back** | start presentation (F5), blank screen (B), fast forward / rewind, volume or scrolling by tilting the remote, or any keyboard shortcut |
| Presentation timer | 1 to 600 minutes, up to three alerts, started by the first slide change; the remote vibrates in short pulses (two for an alert, three for the end), shown in the top bar |
| Vibration, battery | strength setting, test button, battery level, four short pulses when it runs low |
| **Per-application profiles** | an application's own settings apply while it has the focus |
| Remote page | connection (USB / Bluetooth), battery, firmware versions |

Not included (by decision): cloud backup of settings, firmware update.

## Install

```bash
git clone https://github.com/mamrehn/ProjecteurUbuntu && cd ProjecteurUbuntu
./setup.sh --enable        # builds the package, installs it with sudo apt, switches it on for you
```

Then **log out and in once** (GNOME Shell on Wayland only notices a new extension at login). Settings:
`gnome-extensions prefs projecteur-overlay@mamrehn.github.io`, or the Extensions app. `./setup.sh --check` diagnoses
and changes nothing; `./setup.sh --uninstall` removes everything. The package replaces Ubuntu's Qt5 `projecteur`
package (the two cannot run together). Without `--enable` nothing is switched on: run `projecteur-setup enable` later.

If anything misbehaves, `projecteur-setup disable` returns the remote to a plain keyboard and mouse at once.

## What was verified, and what was not

Verified automatically (`tests/run-all.sh`, about 90 s): the daemon with a simulated remote speaking the recorded bytes;
the extension in an **isolated, headless GNOME Shell 50.1** with pixel checks of every effect, the settings window
(real widgets, and opened through GNOME), per-application profiles with a real Wayland window, and the daemon talking to
the extension; two monitors of different sizes, 1.5x on 1080p, mixed 1.25x / 1.33x, 4K at 2x and 1.5x. Verified by hand
on one remote: Next/Back, the action button, hold and double click, raw movement (49.5 counts per degree of turn), held
Next/Back, battery, firmware, vibration, over **Bluetooth** and the **USB receiver**.

**Not verified yet:** the extension in a real desktop session (so far only in throwaway headless shells, on purpose:
an extension runs inside the compositor, and a bug there can freeze the desktop); AMD and NVIDIA graphics (only an
Intel iGPU was available); what Windows does on several monitors (this fork dims and magnifies the monitor the effect is
on); timing of the hold actions against the Windows app; the Spotlight 2, which is not supported. Details and the
input measurements: [doc/ubuntu/INPUT-MODEL.md](doc/ubuntu/INPUT-MODEL.md), plan: [doc/ubuntu/PLAN.md](doc/ubuntu/PLAN.md).

## Safety net

GNOME Shell writes `$XDG_RUNTIME_DIR/gnome-shell-disable-extensions` while it enables extensions; if the shell crashes
at that moment, Ubuntu starts it again with extensions off. To switch this one off by hand from a text console
(Ctrl+Alt+F3): `gnome-extensions disable projecteur-overlay@mamrehn.github.io`. **Do not use GNOME's built-in magnifier**
(Settings > Accessibility > Zoom) as a substitute: it froze one test machine.

---

# Alternative: Ubuntu's packaged Projecteur 0.10 (no zoom)

The rest of this page documents the earlier interim route: Ubuntu's own Qt5 package, started under XWayland, with
**no zoom**. `./install.sh` sets it up. Use it only if you cannot use the fork's version above.

```bash
./install.sh --autostart      # run as your normal user, not with sudo
```

`./install.sh --check` only diagnoses and changes nothing. Attach its output when asking for help.

## What you get

| Feature | Status |
|---|---|
| Next / Back buttons (as virtual keyboard) | works, as long as Projecteur runs via XWayland, see [below](#why-projecteur-must-run-via-xwayland-xcb) |
| Laser-pointer button → dimmed spotlight overlay | works via XWayland; on native Wayland it steals keyboard and mouse focus |
| Zoom / magnifier | **does not work on GNOME**, see [Zoom](#zoom-does-not-work-on-gnome) |

Environment: Ubuntu 26.04.1, GNOME Shell 50.1, Wayland, Projecteur 0.10-4build1 (Ubuntu `universe`),
Spotlight USB receiver `046d:c53e`. The Bluetooth variant should work through the packaged udev
rules but is untested. See [Status of testing](#status-of-testing) for what was and was not verified.

## What `install.sh` does

The script is **idempotent**: run it as often as you like. Every step first checks whether its work
is already done and says so; a second run changes no files and exits 0 when all is well.

1. Offers to remove the obsolete Cloudsmith apt repo (see [below](#advice-that-does-not-apply-on-2604)).
2. `apt-get install projecteur` from the Ubuntu archive (skipped if already installed).
3. Checks that `/dev/uinput` is writable; if not, reloads udev rules.
4. On GNOME + Wayland, makes sure **zoom is off** (the one setting that otherwise breaks the overlay).
   A running Projecteur is told via `projecteur -c zoom=false`; otherwise the config file is edited.
5. Fixes root-owned files in `~/.config/Projecteur` if you ever ran Projecteur with `sudo`.
6. Installs `~/.local/bin/projecteur-gnome`, a launcher that starts Projecteur on the **xcb** platform
   and hides its preferences window, plus a per-user `projecteur.desktop` so the app-grid icon uses it.
   `projecteur-gnome --restart` restarts a running instance.
7. With `--autostart`: adds a login entry that runs that launcher.
8. Starts Projecteur if it is not running. If a running instance uses native Wayland, it is restarted
   on xcb. Then prints a summary of checks.

Options: `--check`, `--autostart`, `--keep-zoom`, `--remove-stale-repo`, `--help`.
Flags are additive: dropping `--autostart` on a later run does not remove the autostart entry
(see [Uninstall](#uninstall)).

## Manual steps

```bash
sudo apt-get update && sudo apt-get install projecteur   # needs the 'universe' component
projecteur -c zoom=false                                 # only if you switched zoom on before
QT_QPA_PLATFORM=xcb projecteur &                         # NOT plain `projecteur`, see next section
sleep 2 && projecteur -c settings=hide                   # it opens its preferences window on Wayland
```

Zoom is off by default in 0.10; if Projecteur is not running, set `enableZoom=false` in
`~/.config/Projecteur/Projecteur.conf`. **No udev rules to download, no group changes, no re-login**:
the package ships `/usr/lib/udev/rules.d/55-projecteur.rules`, which grants the logged-in user access
through `uaccess` ACLs. Check with `projecteur -d`; it should say "1 readable, 1 writable".

Useful commands: `projecteur -c spot=toggle` (bind it to a GNOME keyboard shortcut for use without
the presenter), `projecteur -c settings=show`, `projecteur -c quit`.

## Bluetooth instead of the USB dongle

The remote is invisible to Bluetooth until you put it in pairing mode. Per
[Logitech](https://support.logi.com/hc/en-us/articles/360023347933-Connect-the-Spotlight-Presentation-Remote-to-a-device-using-the-USB-receiver-or-Bluetooth):
hold **Cursor (top) + Back (bottom)** together until the remote vibrates. It is then pairable for
**3 minutes**, and pressing any other button within the first 20 seconds cancels pairing mode.
Then, on the computer:

```bash
bluetoothctl devices | grep -i spotlight     # appears as "SPOTLIGHT" once the chord worked
bluetoothctl pair    <MAC>                   # "Pairing successful"; it connects by itself
bluetoothctl trust   <MAC>                   # reconnect automatically next time
projecteur -d                                # should list "Logitech Spotlight (Bluetooth)"
```

Verified: this pairs on Ubuntu 26.04 without a PIN, the kernel creates `SPOTLIGHT Keyboard` and
`SPOTLIGHT Mouse` input devices, and Projecteur (0.10) recognises it (`046d:b503`), reads its battery
level and brings the HID++ link online. The packaged udev rules already grant access. The dongle
and Bluetooth were **not** tested at the same time; Logitech's article does not say whether that is
supported. Over Bluetooth, Next/Back send the Right/Left arrow keys; see
[Status of testing](#status-of-testing) for what else was checked.

## Why Projecteur must run via XWayland (xcb)

Ubuntu's Projecteur is a Qt5 program. Started plainly on GNOME it picks the native Wayland backend,
and that goes wrong. The overlay is a fullscreen window that Projecteur is meant to make
click-through when the spotlight is off. Qt 5 on Wayland never does that: a `WAYLAND_DEBUG=1` trace
shows no `set_input_region` call, and no unmap either. What the trace showed (1920×1080, GNOME 50):

| Time | Event |
|---|---|
| spot **on** + 33 ms | overlay gets **keyboard focus** |
| spot **on** + 51 ms | overlay gets **pointer focus** |
| spot **off** | nothing is unmapped; 45 more frames are rendered |
| spot off + 3.5 s | focus finally leaves, 260 ms after the user pressed Alt (probably Alt-Tab) |
| in between | the user's Esc and Alt key presses were delivered to the overlay, plus 94 pointer-motion events |

So after the first use of the laser button, clicks and keystrokes, including the Spotlight's own
Next/Back keys, go to an invisible Projecteur window instead of your slides.

Projecteur's source has a workaround for exactly this, but only for the `xcb` platform on a Wayland
session (it hides the overlay when the spot goes off). With `QT_QPA_PLATFORM=xcb` (XWayland),
inspected with `xwininfo`:

| State | Overlay window |
|---|---|
| spot off | unmapped |
| spot on | mapped, 1920×1080, **override-redirect** (window managers do not focus such windows) |
| spot off again | unmapped |

`install.sh` therefore always starts Projecteur through `projecteur-gnome`, which sets
`QT_QPA_PLATFORM=xcb`. `./install.sh --check` reports a running instance that uses native Wayland as a
failure and `./install.sh` restarts it. Someone who tried `QT_QPA_PLATFORM=xcb projecteur` before and
saw `Screenshot via GNOME DBus interface failed` was hit by the **zoom** problem below, not by xcb.

## Zoom does not work on GNOME

Projecteur's zoom takes a screenshot of the desktop through `org.gnome.Shell.Screenshot`. GNOME Shell
only answers allow-listed callers and refuses everyone else:

```console
$ gdbus call --session --dest org.gnome.Shell.Screenshot --object-path /org/gnome/Shell/Screenshot \
    --method org.gnome.Shell.Screenshot.Screenshot false false /tmp/x.png
Error: GDBus.Error:org.freedesktop.DBus.Error.AccessDenied: Screenshot is not allowed
```

Projecteur then logs `Screenshot via GNOME DBus interface failed.` on every activation. The
screenshot is only taken when zoom is enabled (`enableZoom=true`), so disabling zoom removes the
error. Making zoom work would need Projecteur to use the `xdg-desktop-portal` screenshot API. No
build of it does: not Ubuntu's 0.10, not the Cloudsmith alpha, not even the newest commit of
upstream's `legacy/qt5` branch (Nov 2024).

**Do not use GNOME's built-in magnifier (Super+Alt+8, Settings → Accessibility → Zoom) as a substitute.**
An earlier version of this README suggested it. On the test machine (GNOME Shell 50.1) it was
followed by a frozen desktop that needed a hard reset, see [below](#the-gnome-magnifier-froze-the-desktop).
So there is currently no zoom on this setup.

### The GNOME magnifier froze the desktop

What the journal of that boot shows (`journalctl -b -1`), with the magnifier on and an Impress
slideshow running:

| Time | Event |
|---|---|
| 10:50:48 | `gsd-media-keys: … 'mag-factor' … outside of valid range` (the zoom keys, repeatedly) |
| 10:54:39 | Settings → Accessibility panel opened; gnome-shell logs an AT-SPI error |
| 10:54:49 | gnome-shell starts logging `g_closure_add_invalidate_notifier: assertion … CLOSURE_MAX_N_INOTIFIERS failed` |
| 10:54:49–10:56:24 | **23,262** of those lines (5,803 / 14,339 / 3,120 per minute); the log then ends (hard reset) |

The three other boots on that machine (about a day of uptime) contain none of these messages. The same assertion
is [a reported gnome-shell bug](https://gitlab.gnome.org/GNOME/gnome-shell/-/work_items/7189)
(also [Ubuntu #2030947](https://bugs.launchpad.net/ubuntu/+source/gjs/+bug/2030947)) that occurs when the Zoom
accessibility feature is enabled. Not proven: which exact action tipped it over (zoom keys, opening the
panel, or the slideshow), and whether it is that bug or a regression; nothing in the journal involves
Projecteur, whose own zoom was off. To check whether a boot was affected:
`journalctl -b -1 | grep -c CLOSURE_MAX_N_INOTIFIERS`. Turning the magnifier off again:

```bash
gsettings set org.gnome.desktop.a11y.applications screen-magnifier-enabled false
```

I have not tested any recovery from an already frozen desktop.

## Advice that does not apply on 26.04

Many guides, and AI chat answers, tell you to do things that fail here. Checked on 2026-09-29:

| Advice | Reality on Ubuntu 26.04 |
|---|---|
| Add the `cloudsmith.io/public/jahnf/projecteur-develop` apt repo | It only carries builds up to **Ubuntu 23.04**; `mantic` through `resolute` (26.04) are empty. The newest package, `1.0.0-0alpha.208`, needs `libqt5widgets5`, which no longer exists on 26.04 (renamed `libqt5widgets5t64`), so it cannot be installed, and the Qt5 code line has no better GNOME zoom anyway. Use the Ubuntu package. Remove the repo: `./install.sh --remove-stale-repo`. |
| Log in with "Ubuntu on Xorg" to get screenshots working | **There is no Xorg session.** `/usr/share/xsessions/` does not exist, no X11 session package is in the archive (`gnome-session-xsession`, `ubuntu-session-xorg`, `gnome-xorg`), and `gnome-shell` only has a `--wayland` mode. Projecteur can still use XWayland, see above. |
| Run plain `projecteur` (native Wayland, or `QT_QPA_PLATFORM=wayland`) | The overlay keeps keyboard and pointer focus after the spotlight is off, see [above](#why-projecteur-must-run-via-xwayland-xcb). |
| `usermod -aG input $USER` | Not needed. `/dev/uinput` and `/dev/hidraw*` are `root:root`; access comes from `uaccess` ACLs. The `input` group also lets your processes read every keyboard. Undo with `sudo gpasswd -d $USER input`. |
| Download `99-projecteur.rules` from GitHub | The URL returns **404** (`curl -f` then fails silently). The package already ships `55-projecteur.rules`. |
| `apt-key add` | Deprecated. If you ever add a third-party repo, use `signed-by=` keyrings. |
| Run `sudo projecteur` | Creates root-owned config files; then "Settings file not readable/writable". Run as your user; if broken: `sudo chown -R $USER: ~/.config/Projecteur`. |
| Build the latest upstream commits instead of the 3-year-old v0.10 | GitHub's "89 commits since v0.10" are on `develop`, and 81 of them (Nov 2025 to Sept 2026, mostly July–Sept 2026) are a **KDE-only Qt6 rewrite**: it requires Plasma 6.7+, needs `LayerShellQt` for its overlay and KWin's screencast protocol for zoom, and has no GNOME code left. GNOME's compositor advertises neither (`wayland-info` lists no layer-shell and no KDE globals), so it could not draw the overlay here. The GNOME-capable Qt5 line (`legacy/qt5`) has only **8** commits after v0.10: CI bumps, Kensington PowerPointer support and a `--hide-tray-icon` option. Nothing touches Wayland, GNOME or zoom. Ubuntu's `0.10-4build1` is a no-change rebuild of Debian's May 2024 upload: nobody is developing it, but it matches 26.04's libraries and is the only version that installs cleanly. |

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Desktop froze while the GNOME magnifier was on; journal full of `CLOSURE_MAX_N_INOTIFIERS` | Known gnome-shell Zoom bug, see [above](#the-gnome-magnifier-froze-the-desktop). Keep the magnifier off. |
| Border ring turns white and stays white, whatever color is set | Projecteur 0.10 bug: after the **spot shape** is changed in Preferences (or via `projecteur -c spot.shape=…`) the border's color binding is lost. Colors, sizes and opacities keep working until then. Fix: `~/.local/bin/projecteur-gnome --restart` (settings are kept). Change the shape first, then restart. Reproduced with a replica of Projecteur's QML loader on Qt 5.15, see [Status of testing](#status-of-testing). |
| Clicks or Next/Back do nothing after using the laser button | Projecteur runs on native Wayland. `./install.sh --check`; fix with `./install.sh` or start it from the app grid. |
| `Screenshot via GNOME DBus interface failed` | Zoom is on. `projecteur -c zoom=false`. |
| `Unable to open: /dev/uinput` | ACL missing. `./install.sh` reloads udev; otherwise log out and in on the local desktop (ACLs are not granted to SSH sessions). |
| `Settings file … not readable/writable` | Root-owned `~/.config/Projecteur`, see `sudo` row above. |
| `Another application instance is already running. Exiting.` | Normal: it is already running. Clicking the app-grid icon reopens the preferences of the running instance. |
| `Wayland does not support QWindow::requestActivate()` | Harmless Qt warning (native Wayland only). |
| `Qt 'xcb' platform and Wayland session detected.` | Expected and intended. |
| Preferences window flashes at start | Projecteur 0.10 always opens it on Wayland sessions. The launcher hides it about a second after start (`projecteur -c settings=hide`); closing it does not quit the app. |
| No tray icon | Needs the `ubuntu-appindicators` GNOME extension (on by default on Ubuntu). |
| Buttons do nothing at all | `projecteur -d` should list the device as readable and writable. Replug the receiver. |

Live log: `journalctl --user -f | grep -i projecteur`, or run `projecteur -l dbg` in a terminal.

## Uninstall

```bash
projecteur -c quit
sudo apt-get remove projecteur
rm -f ~/.config/autostart/projecteur.desktop ~/.local/share/applications/projecteur.desktop \
      ~/.local/bin/projecteur-gnome
rm -rf ~/.config/Projecteur      # optional: your settings
```

## Status of testing

Everything here was run on a single machine (Ubuntu 26.04.1, GNOME 50.1, one 1920×1080 monitor, one
Spotlight used with its USB receiver and over Bluetooth). **Verified:**

- `--check`, the zoom fix (running-instance and config-file paths), and repeated runs of `install.sh`
  (files, modification times and output identical from the second run on).
- The launcher starts on xcb, hides the preferences window, and reopens it on a second launch; the
  installer restarts an instance that was started on native Wayland.
- Window and focus behaviour of both platforms from `WAYLAND_DEBUG` and `xwininfo` traces (tables above).
- **Over Bluetooth** (Projecteur on xcb, VS Code focused): the focus ring appears while the action
  button is held, follows the remote and fades out shortly after release. Next sends `KEY_RIGHT` and
  Back sends `KEY_LEFT` (recorded on Projecteur's virtual keyboard). Both also arrive while the action
  button is held, so the overlay does not steal keyboard focus.
- The same interaction (ring while holding, Next/Back, Next/Back while holding) works in a
  **LibreOffice Impress slideshow** and in **Microsoft's web PowerPoint** in a browser, both over
  Bluetooth (reported by the tester; the recording shows only `LEFT`/`RIGHT` keys and no errors).
- **White border after a shape change:** in a replica of Projecteur's border `Loader` (same code as
  `qml/main.qml`, `Settings` stubbed, PyQt5's Qt 5.15.14 headless) the border item is green at startup,
  turns `#ffffff` after one shape change and stays white even when the color is set to red, while
  color-only changes follow correctly. This is a replica, not the real app: shapes are plain rectangles,
  Ubuntu ships Qt 5.15.18, and the ring itself was not seen on screen. That the tester changed the shape
  is inferred from a new `[Shape.Ngon]` section in the config, not confirmed.
- The paired remote **reconnected by itself after a reboot** (bluez `Connected: yes`, kernel input
  devices created about two minutes after boot, no manual step). Projecteur does not start
  automatically unless you ran `./install.sh --autostart`.

**Not verified:**

- What the overlay looks like beyond "the ring showed" (reported by eye; I cannot screenshot, because
  GNOME refuses that to non-allow-listed programs).
- A PDF viewer's presentation mode, and a browser or Impress slideshow on a **second screen or projector**.
- Buttons over the **USB dongle**: a first test reported Back not working and Next/Back dead while the
  action button was held, but the receiver dropped off the bus during that test and no key recording
  exists for it. Unexplained, being re-tested.
- Fractional scaling, and more than one monitor.
- The `sudo` steps from a clean state (fresh `apt-get install`, stale-repo removal, `udevadm`
  reload). On this machine the package and ACLs were already in place.
- The autostart entry after a real logout/login (in particular that `DISPLAY` is set when it runs;
  the launcher falls back to `:0`).

Reports welcome.
