# Feature parity: Logi Options+ (Windows) → this fork (Ubuntu 26.04)

Reference: 12 screenshots of **Logi Options+ (German UI)** with a **Logitech Spotlight (v1)**,
device firmware 1.1.32, receiver firmware 41.1.7, taken 2026-09-30. The screenshots stay local
(`screenshots_*/` is git-ignored); this file records everything they show.

Goal: every feature of that app that makes sense on Linux, with a GUI wherever the Windows app has
a setting. The settings UI does **not** have to look like the Windows one.

## 1. The reference configuration (values from the screenshots)

| Area | Setting (German label) | Value |
|---|---|---|
| Pointer | Pointer speed (Zeigergeschwindigkeit) | 35 % |
| Highlight (Hervorheben) | enabled | yes |
| | Contrast (Kontrast) | 80 % |
| | Size (Größe) | 43 % |
| Magnify (Vergrößern) | enabled | yes |
| | Lens color (Farbe der Lupe) | hue slider ≈ 56 % → teal-green; preview shows a ring in that color with a black outer outline |
| | Size (Größe) | 80 % |
| Laser | enabled | yes |
| | Laser color (Laserfarbe) | hue slider ≈ 17 % → red |
| | Size (Größe) | 12 % |
| Behaviour | Cursor control (Cursor-Steuerung): "allows interaction with videos and links during presentations" | **off** |
| | Re-center pointer effects (Zeiger-Effekte neu zentrieren): "on slide change, move the pointer back to the screen centre" | **on** |
| | Freeze effects (Effekte einfrieren): "the effect stays on screen after releasing the button" | **on** |
| Buttons | Top button | "Multiple pointer effects"; double-click switches between enabled effects |
| | Hold **Next** (Weiter-Taste gedrückt halten) | Start presentation |
| | Hold **Back** (Zurück-Taste gedrückt halten) | Blank screen |
| Timer | Timer (currently unused by the user) | off |
| Vibration | Intensity (Vibrationsintensität) | 50 % |
| | Battery warning (Akkuwarnung): "device vibrates when charge reaches its minimum" | on |
| | Timer notification (Timer-Benachrichtigung) | on |

## 2. All features the app offers, and the plan for each

Legend – **Ext** = GNOME Shell extension (draws on screen), **Daemon** = Qt6 background service
(device, buttons, timers), **Prefs** = settings GUI. "Base" = what the Projecteur `develop` code already
contains, found by keyword search of `src/` and `qml/`; *present* means the code exists, not that it
works on this hardware.

| # | Feature | Options in Windows app | Where | Base (Projecteur develop) |
|---|---|---|---|---|
| 1 | Highlight effect | contrast, size | Ext + Prefs | spot/shade exists (KDE overlay code to be replaced) |
| 2 | **Magnify effect, live** | lens color, size | Ext + Prefs | snapshot/KWin-stream zoom only (KDE-specific) |
| 3 | Laser effect | color, size | Ext + Prefs | laser shader present (KDE overlay) |
| 4 | Effect selection | per-effect checkbox; **double-click top button** cycles enabled effects | Daemon + Ext | "double click" code present, purpose unverified |
| 5 | Pointer speed | 0–100 % | Daemon + Prefs | HID++ `PointerSpeed` feature referenced |
| 6 | Cursor control | on/off | Daemon + Ext | absent |
| 7 | Re-center on slide change | on/off | Daemon + Ext | absent |
| 8 | Freeze effects on release | on/off | Daemon + Ext | absent |
| 9 | Hold **Next** action | Start presentation · Blank screen · Fast forward · Volume control · Scroll · Keyboard shortcut · No action · Smart actions | Daemon + Prefs | Next/Back hold events, scroll and volume present; "start presentation" absent |
| 10 | Hold **Back** action | Blank screen · Fast rewind · Volume control · Scroll · Keyboard shortcut · No action · Smart actions | Daemon + Prefs | as above |
| 11 | Timer | on/off; duration 15 / 30 / 60 min / custom; per duration up to 3 vibration alerts ("5 min remaining" on by default, "1 min remaining", one free) | Daemon + Prefs | presentation timer present |
| 12 | Vibration | intensity; battery warning; timer notifications | Daemon + Prefs | vibrate present |
| 13 | Battery level | indicator (bottom left) + low-battery vibration | Daemon (+ Ext indicator) | battery reading present |
| 14 | Connection indicator | USB receiver / Bluetooth | Daemon | device scan present |
| 15 | Per-application profiles ("Add application") | separate settings per app | Ext (focused-window app id) + Daemon | absent |
| 16 | Device info | firmware versions; "check for update" | Daemon (read-only) | firmware info reading present |

## 3. Deliberately out of scope (unless you say otherwise)

| Windows feature | Why |
|---|---|
| Cloud backup of device settings (needs a Logitech account) | replaced by a plain, exportable config file |
| Firmware update | needs Logitech's update channel; nothing equivalent verified for Linux |
| Feature tour, support link | Windows-app onboarding |
| "Smart Actions" (automation chains in Options+) | undocumented in the screenshots; add only on request |
| Effects visible in a *single-window* screen share | an overlay drawn by the compositor is not part of one window's pixels; the same is true on Windows. Whole-screen shares include it. |

## 4. Behaviour the screenshots cannot answer (needs a Windows recording or a Windows test)

1. Magnify: zoom factor (not exposed as a setting) and what "80 %" of size refers to.
2. Highlight: what exactly "contrast 80 %" means (dim level of the rest of the screen?) and what "size 43 %" is relative to (screen height?).
3. Freeze: what ends a frozen effect (next button press, slide change, timeout)?
4. Double-click cycle: order, and does it skip unchecked effects; which effect appears first.
5. Re-center: which monitor is "the centre" on multi-monitor setups, and what triggers it exactly (Next/Back key presses?).
6. Cursor control: what the buttons do while it is on.
7. Fast forward / fast rewind: how many slides, how fast.
8. Pointer speed 35 %: mapping to the device's HID++ setting.

A short screen recording of each on Windows answers all eight.
