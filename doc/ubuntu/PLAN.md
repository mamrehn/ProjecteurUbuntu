# Plan: Logitech Spotlight for Ubuntu 26.04 (GNOME 50, Wayland, Qt6)

Status: **plan, nothing of it implemented yet** apart from the interim `install.sh` at the repository
root. Feature list: [FEATURE-PARITY.md](FEATURE-PARITY.md).

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

| M | Deliverable | Acceptance |
|---|---|---|
| 0 | Build tooling; headless-shell test harness (boot shell, load extension, drive a virtual pointer, record the virtual monitor, assert pixels) | harness runs one trivial extension and reads back a frame |
| 1 | Extension spike: circular live lens following the pointer; highlight; laser | pixel checks: lens shows magnified content, no recursion, click-through; CPU/fps measured |
| 2 | Daemon: KDE code removed, builds on Ubuntu 26.04 with Qt6, systemd user service, USB + Bluetooth detection, D-Bus API to the extension | device detected; extension receives activate/move/deactivate |
| 3 | Buttons: hold-Next/Back actions, double-click cycling, freeze, re-center, cursor control | scripted device events → expected key events / effect state |
| 4 | Prefs GUI for every applicable setting, per-app profiles | settings round-trip; profile switches with the focused window |
| 5 | Timer, vibration, battery warning | timers fire vibration commands (mocked device) |
| 6 | `.deb` (daemon, extension, udev rules, user service), updated `install.sh`, uninstall | clean install/uninstall in a fresh Ubuntu 26.04 VM |
| 7 | Hardening on real hardware: multi-monitor, fractional scaling, Intel/AMD/NVIDIA, Teams/Zoom whole-screen share | checklist run on colleagues' machines |

## Open decisions

* Where settings live (GSettings vs a config file owned by the daemon) – decide in M2.
* Whether to keep any KF6 dependency during the port or remove all of them at once – decide by a
  daemon-only build spike in M2.
* Publishing: this fork is public on GitHub; the license (MIT, Jahn Fuchs) and attribution stay.
