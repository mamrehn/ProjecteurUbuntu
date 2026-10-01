# Plan: Logitech Spotlight for Ubuntu 26.04 (GNOME 50, Wayland, Qt6)

Status (2026-10-01): **M0 to M6 are done; M7 (hardening) is partly done.**
The daemon, the GNOME Shell extension, the settings window and the package exist and pass `tests/run-all.sh`; the
package installs on a clean Ubuntu 26.04 (container test), and the maintainer installed it in their real session on the
same day and reports that it works. What is **not** done: AMD and NVIDIA graphics, other machines, Teams/Zoom screen
sharing. GitHub Actions builds, tests and smoke-tests the `.deb` on every push to `main` and publishes a release for a
tag `gnome-vX.Y.Z` (`.github/workflows/gnome-deb.yml`).
Feature list: [FEATURE-PARITY.md](FEATURE-PARITY.md). What the remote sends: [INPUT-MODEL.md](INPUT-MODEL.md).
The first version of the plan follows; the table of milestones has the current state.

## Target

Ubuntu 26.04 LTS, GNOME Shell 50 on Wayland, Qt 6.10, Logitech Spotlight v1 over the USB receiver
**and** Bluetooth. Must work on **all GPUs** (Intel, AMD, NVIDIA), **multiple monitors/projectors**
and **fractional scaling**, and be usable while sharing the **whole screen** in Teams/Zoom.

## What was verified, and how (the design rests on these)

| # | Fact | Evidence |
|---|---|---|
| 1 | KDE's live zoom works only because KWin **excludes the app's own windows from the capture** | upstream `doc/TROUBLESHOOTING.md` |
| 2 | Mutter's ScreenCast D-Bus API (version 4) has `Start/Stop/RecordMonitor/RecordWindow/RecordArea/RecordVirtual` and **no exclusion option**; an unprivileged process could create a session and monitor stream without a prompt (never started) | D-Bus introspection and probe, 2026-09-29 |
| 3 | GNOME's own magnifier avoids recursion by putting its lens on `global.stage` **outside** `Main.uiGroup` and placing a live `Clutter.Clone` of `uiGroup` inside; the lens is `hidden_from_pick` (click-through) | `magnifier.js` extracted from `libshell-18.so` |
| 4 | Qt5 Projecteur on **native Wayland** leaves an invisible fullscreen overlay that keeps keyboard and pointer focus; XWayland (xcb) is a workaround | `WAYLAND_DEBUG` trace, see repository `README.md` |
| 5 | GNOME's built-in Zoom is **not safe to lean on**: on one boot it was followed by 23,262 `CLOSURE_MAX_N_INOTIFIERS` errors and a hard reset; known upstream bug | journal of that boot, gnome-shell issue 7189 |
| 6 | A disposable **headless GNOME Shell 50.1** (`--headless --virtual-monitor`) runs next to the real session with its own D-Bus and a ScreenCast service | test run, 2026-09-30 |
| 7 | Projecteur `develop` is Qt6 but hard-depends on KDE: CMake requires ECM, KF6, Plasma, LayerShellQt; 15 of 56 `src/` files use KDE APIs | `git grep` |

## Architecture

```
 Spotlight (USB receiver / Bluetooth)
        │  hidraw + evdev (HID++)
        ▼
 projecteur daemon (Qt6, systemd --user)  ── D-Bus ──►  GNOME Shell extension (GJS)
   device I/O, button actions, timers,                    draws: highlight, live magnify, laser,
   vibration, battery, settings store                     timer/battery HUD; per-app profile switch
        ▲                                                        │
        └────────────── D-Bus ◄── Prefs GUI (libadwaita, in the extension's prefs)
```

* **Live magnify** is a `Clutter.Clone` of `Main.uiGroup` inside a lens actor parented to
  `global.stage`, exactly as GNOME's magnifier does. No screen capture, no permission prompt, no
  "sharing" indicator, GPU-independent, per-monitor, follows fractional scaling.
* The daemon is the only place that touches hardware. It reuses Projecteur's device layer (MIT, with
  attribution), with KDE dependencies removed.
* The extension is installed system-wide by the package (no extensions.gnome.org review) and enabled
  by default through dconf.

### Alternatives considered

| Option | Live | Circular lens | Prompts | Verdict |
|---|---|---|---|---|
| **Shell extension** (above) | yes | yes (needs a shader/mask, to be proven) | none | chosen |
| Qt6 overlay + Mutter ScreenCast | yes | yes | none, but a recording indicator | **records its own lens** (fact 2) |
| Snapshot on activation (portal or ScreenCast, one frame) | **no** | yes | portal prompt unresolved | rejected: live zoom is required |
| Drive GNOME's built-in Zoom via gsettings | yes | **no** (rectangular lens) | none | rejected: fact 5 |

### Risks

* An extension runs inside the compositor: a native crash or a blocking loop can take the session down.
  Mitigation: develop and test in headless shells only; enable in the real session only after automated
  checks pass; keep `gnome-extensions disable` as the documented kill switch.
* Extension API changes between GNOME releases. Ubuntu 26.04 stays on GNOME 50 for its lifetime;
  later Ubuntu releases need a port.
* A circular lens needs a shader or mask in Clutter; not yet proven on GNOME 50 (first spike).
* Effects are invisible in a single-window screen share (same limitation as on Windows).

## Repository layout

* `main` – this fork's line (upstream `develop` + Ubuntu work). Upstream mirrors stay untouched:
  `develop`, `master`, `legacy/qt5`. `backup/pre-fork-main` holds the two original commits.
* `README.md` – Ubuntu landing page; `README.upstream.md` – upstream's, unchanged.
* `install.sh` – interim installer for Ubuntu's packaged Projecteur 0.10 (no zoom).

## Milestones (each with an automated acceptance check where possible)

| M | Deliverable | State |
|---|---|---|
| 0 | Headless-shell test harness `tests/headless/run.py` (whole process tree isolated: HOME, XDG dirs, runtime dir, GSettings keyfile, private D-Bus) | **done** |
| 1 | Spike: round live lens, highlight, laser, D-Bus control, enable/disable | **done** (headless) |
| 2 | Daemon in `daemon/` (Qt6, no KDE): USB + Bluetooth detection, D-Bus API, settings as JSON | **done**; verified on the real remote over Bluetooth and USB |
| 3 | Buttons: hold-Next/Back actions, double-click cycling, freeze, re-center, cursor control | **done** (held Next/Back measured on hardware; timing against Windows not compared) |
| 4 | Settings window for every setting, per-application profiles | **done**; real widgets tested, opened through the shell |
| 5 | Timer, vibration (short pulses), battery warning | **done**; vibration confirmed by hand ("connected" pulse), timer and battery tested with a simulated remote |
| 6 | `.deb`, `setup.sh`, udev rules, systemd user service, `projecteur-setup` | **done**; package builds, installs in simulation, service runs under its restrictions; clean-system install not tested |
| 7 | Hardening: multi-monitor, fractional scaling, 4K, GPUs, Teams/Zoom | **partly**: five display layouts pass in the headless shell (Intel only); AMD, NVIDIA, a real session and screen sharing are open |

## Open decisions

* Where settings live: **decided** – GSettings of the extension; the extension pushes the daemon's keys over D-Bus (`SetConfig`), under the same key names, and again whenever the daemon (re)starts. The daemon keeps nothing on disk.
* KF6 dependencies: **decided** – none. The daemon needs Qt6 Core and DBus only.
* Publishing: this fork is public on GitHub; the license (MIT, Jahn Fuchs) and attribution stay.

## Spike 1 results (2026-09-30)

Code: `gnome-shell/projecteur-overlay@mamrehn.github.io/` (extension) and `tests/headless/run.py` (harness).
Everything ran in a throwaway `gnome-shell --headless` 1280x720 on the Intel iGPU, GNOME Shell 50.1, with its own
D-Bus, GSettings and XDG directories. **29 of 29 checks pass:**

| Claim | How it was checked |
|---|---|
| The lens is a *live* magnified view of the screen | pixels at five lens positions equal the checkerboard tile at half the offset (zoom 2), and differ from the un-magnified tile; zoom 3 also verified |
| The lens is round | the corner of the lens square shows the plain screen, not magnified content |
| No self-capture / lens-in-lens | lens content equals the plain pattern, at every sampled point |
| Highlight dims by the contrast setting | outside the hole exactly 20 % of the original colour (contrast 80 %), inside unchanged |
| Laser dot | centre pixel is the configured red |
| Input passes through | `get_actor_at_pos(REACTIVE)` returns the background actor in all three modes |
| Clean lifecycle | 5x disable/enable: D-Bus name follows, stage child count unchanged (6 before, 6 after), lens still correct afterwards |
| Cost | shell CPU 0.7 % idle; 24.6 % of one core while moving the lens at about 96 updates/s (GPU time not included) |

**Not verified:** the real session; real Wayland windows and fullscreen slideshows underneath; more than one
monitor; fractional scaling; AMD and NVIDIA (only Intel was available); a 4K panel; the percentage-to-pixel
size mapping (a placeholder, not calibrated against the Windows app).

### Pitfalls found (each one cost a test run)

1. **`Shell.GLSLEffect` uniforms before the first paint segfault the shell** (NULL pipeline in
   `libmutter-cogl`). Call `get_uniform_location`/`set_uniform_float` only from `vfunc_paint_target`.
2. The snippet hook enum is `Cogl.SnippetHook`, not `Shell.SnippetHook`, in GNOME 50.
3. **Python's `Gio.bus_get_sync(SESSION)` ignores `DBUS_SESSION_BUS_ADDRESS` and connects to the real session
   bus** (`/run/user/UID/bus`); GJS and `gdbus` honour the variable. The harness connects to the address
   explicitly and refuses to run if the bus is the real one or not empty.
4. GNOME 50's Screenshot D-Bus API only answers `org.gnome.SettingsDaemon.MediaKeys` and
   `org.freedesktop.impl.portal.desktop.gnome`, unless `global.context.unsafe_mode` is set. Test builds set it
   (only when `PROJECTEUR_OVERLAY_TESTING=1`).
5. GNOME Shell writes `$XDG_RUNTIME_DIR/gnome-shell-disable-extensions` while extensions are being enabled; if
   the shell crashes then, the systemd unit disables extensions on restart. That is a useful safety net for
   the real session, and the reason test shells get a private `XDG_RUNTIME_DIR`.

## M2 sizing: from upstream `develop` to a Qt6-only daemon (2026-09-30)

* Installed for this: `cmake` 4.2.3, `ninja`, `pkg-config`, `qt6-base-dev`, `qt6-tools-dev`.
* Upstream `develop` **cannot be built on Ubuntu 26.04 as it is**, not even with KDE installed: it requires
  Plasma, KPipeWire and LayerShellQt **6.7**, Ubuntu 26.04 ships **6.6.x** (KF6 itself is 6.24, which is fine).
* Each device-layer source file was syntax-checked against plain Qt6 (Core, DBus, Gui, Widgets). The only
  blockers are of four kinds, none deep:
  1. ECM-generated logging headers (`projecteur_hid_debug.h`, `…_device_…`, `…_input_…`, `…_virtual_device_…`),
  2. `KLocalizedString` (`i18n()`),
  3. the KConfigXT-generated `projecteurconfig.h` used by `settings.cc`,
  4. the QtDBus-generated adaptor header used by `projecteurcontrol.cc`.

  `presentationtimer.cc` and `device-command-helper.cc` already compile unchanged.
* Consequence: the daemon target is the device layer plus small shims (Qt logging categories, `i18n` mapped to
  `tr`), a generated D-Bus adaptor and a QSettings-based configuration instead of KConfigXT. No Widgets, Quick,
  Wayland client, KF6, Plasma or KPipeWire.

## Decisions

| Decision | Why |
|---|---|
| **Vibration: short pulses** (length 1) for timer and battery alerts, told apart by the number of pulses; one longer pulse (length 3) only for "connection established"; the opt-in minute code adds a medium pulse (length 2) for "five" | a long vibration distracts the speaker during a presentation (maintainer, 2026-10-01); the minute code was the maintainer's own proposal |
| Vibration strength is monotonic in the intensity byte; the slider maps percent x 2.55 | maintainer guidance; the blind test only suggests about three perceptible steps |
| New Qt6 daemon in `daemon/` instead of porting Projecteur's application | the measured input model (HID++ diversion, exclusive capture, raw X/Y) differs from Projecteur's design, and its application is tied to KDE and Widgets; only protocol knowledge is reused |
