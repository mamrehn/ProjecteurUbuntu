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
| 12 | Vibration | intensity; battery warning; timer notifications | Daemon + Prefs | vibrate present. Our policy: short pulses only (alerts differ by pulse count), one longer pulse for "connected"; see [INPUT-MODEL.md](INPUT-MODEL.md) |
| 13 | Battery level | indicator (bottom left) + low-battery vibration | Daemon (+ Ext indicator) | battery reading present |
| 14 | Connection indicator | USB receiver / Bluetooth | Daemon | device scan present |
| 15 | Per-application profiles ("Add application") | separate settings per app | Ext (focused-window app id) + Daemon | absent |
| 16 | Device info | firmware versions; "check for update" | Daemon (read-only) | firmware info reading present |

## 3. Out of scope (confirmed by the user, 2026-09-30)

| Windows feature | Why |
|---|---|
| Cloud backup of device settings (needs a Logitech account) | out of scope; a plain, exportable config file is enough |
| Firmware update | out of scope (firmware versions are still shown read-only) |
| Feature tour, support link | Windows-app onboarding |
| Effects visible in a *single-window* screen share | an overlay drawn by the compositor is not part of one window's pixels; the same is true on Windows. Whole-screen shares include it. |

**"Smart Actions"** need no separate work: per the user they are the action-button (pointer effect) settings
that are already listed above.

## 4. Confirmed behaviour (stated by the user from the Windows app)

These define the on-screen state machine (Highlight, Magnify and Laser all behave the same way):

| Input | Result |
|---|---|
| **Hold** the action (top) button | the effect is shown and follows the *device's* movement |
| **Release** the button (Freeze on) | the effect stays exactly where it was, as with an extremely steady hand |
| Press and hold **again** | device movement moves the frozen effect again, from where it was |
| **Short click** on the action button | the effect is hidden |
| **Double-click** the action button | cycles the mode in the order of the settings page: **Highlight → Magnify → Laser → Highlight …** |

Consequence for the design: the effect position is *state of the overlay*, moved by device deltas,
not by the system mouse pointer. After a release the effect must stay put even if the mouse moves.

Assumptions to confirm: with Freeze **off** the effect hides on release; the double-click cycle skips
modes whose checkbox is off.

## 4a. Measured from the Windows screencast (2026-10-01, 1920x1080, 60 fps, exactly the reference settings)

The recording (kept locally, git-ignored) was analysed frame by frame. All numbers are for the settings in
section 1.

| What | Measured |
|---|---|
| Highlight hole, size 43 % | **429 px** across = 39.7 % of the screen height; hard edge (2-3 px anti-aliasing) |
| Highlight dimming, contrast 80 % | everything outside is drawn at **29 %** brightness (overlay alpha about 0.71) |
| Highlight does not dim the Windows taskbar | the taskbar stays at full brightness |
| Magnify lens, size 80 % | teal ring **703 px** across = 65.1 % of the screen height |
| Magnify zoom factor | **2.0** (scale search against the unmagnified page: 2.005); not configurable |
| Lens ring | teal band about 4 px (#00F8BE) with a black outline about 4 px outside it |
| Laser, size 12 % | soft radial dot: saturated red core about **22 px**, glow about **40 px** (core colour 255,0,6) |
| Appearance / disappearance | **instant**: no fade or animation (full brightness change within one frame at 30 fps) |
| Where an effect appears | **at the mouse cursor** (the Windows cursor stays where it is for the whole demo, the effects start within about 150 px of it and then follow the device); not at the last position, not at the screen centre |
| After a short click (hide) and a new hold | starts again at the mouse cursor, not where it was hidden |
| Order observed when switching | Magnify -> Laser -> Highlight -> Magnify, consistent with the Highlight -> Magnify -> Laser cycle |
| Mouse cursor while an effect is shown | does not move (cursor control is off) |

How the mapping from settings to pixels is implemented: `CAL` in
`gnome-shell/projecteur-overlay@mamrehn.github.io/overlay.js`. With one data point per effect these curves are
**hypotheses**: both lens and highlight fit `diameter / screen height = 0.102 + 0.686 * size`, the dimming is
assumed linear in contrast, and the laser proportional to its size. **To confirm them, record each effect at two
more settings** (for example sizes 20 % and 100 %, contrast 30 % and 100 %).

## 5. Behaviour the screenshots cannot answer (needs a Windows recording or a Windows test)

1. Size and contrast curves for settings other than the reference ones (see 4a).
3. Re-center: which monitor is "the centre" on multi-monitor setups, and what triggers it exactly (Next/Back key presses?).
4. Cursor control: what the buttons do while it is on.
5. Fast forward / fast rewind: how many slides, how fast.
6. Pointer speed 35 %: base gain measured (49.5 counts per degree, 1.0 pixel per count matches the flashlight feel at 35 %); how 0 to 100 % scales it, and whether the remote stores its own speed, is still open.
7. What "re-center on slide change" does exactly on several monitors (not visible in the recording, which has no slide changes).
