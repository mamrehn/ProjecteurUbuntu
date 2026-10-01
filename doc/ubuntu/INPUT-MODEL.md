# What the Logitech Spotlight really sends (measured)

Measured on 2026-10-01 with a Spotlight v1 behind its USB receiver (`046d:c53e`) on Ubuntu 26.04, using
[tools/spotlight_events.py](../../tools/spotlight_events.py) (raw evdev, exclusive) and
[tools/spotlight_hidpp.py](../../tools/spotlight_hidpp.py) (HID++ diversion). Projecteur was not running.
Next/Back were also checked over Bluetooth. This is the input side of the plan in [PLAN.md](PLAN.md).

## Events per physical action

| You do | The hardware sends | Notes |
|---|---|---|
| **Next** | `KEY_RIGHT` down/up (HID usage `0x7004f`), about 60 ms apart | the same over Bluetooth. Not Page Down. |
| **Back** | `KEY_LEFT` down/up (`0x70050`) | the same over Bluetooth |
| Next / Back **while the action button is held** | arrive normally, in the middle of the movement | |
| Action button, **short press** | `BTN_MOUSE` down/up (HID usage `0x90001`) on the receiver's *mouse* node, about 60 ms apart; **no HID++** | a plain left click: if nothing captures the node it clicks in the app under the pointer. Never buzzes, never HID++. |
| Action button, **double-click** | HID++ notification, ReprogramControlsV4 control `0x00df`: `11 01 07 00 00 df 00 …` (down) then `11 01 07 00 00 00 00 …` (up) | needs the diversion `0xdf` with flags `0x03`. Sent at the second press. |
| Action button, **hold** | with `0xd8` diverted using flags `0x33`: `11 01 07 00 00 d8 …` (down), then **raw X/Y** messages, then `11 01 07 00 00 00 …` (up) | see below |

### Hold and movement

* Diverted with flags `0x33` (`0x01` divert, `0x02` valid, `0x10` also divert raw X/Y, `0x20` valid), the
  movement is sent only as HID++ messages `11 01 07 10 <dx:int16> <dy:int16> …` (big endian), about 100 per
  second. **No pointer motion reaches the system**, which is how the Windows software keeps the mouse cursor
  still ("cursor control off"). Example: sweeping upward for 1.5 s gave 152 messages with dx=15, dy=-892.
* Without the raw X/Y bit (flags `0x03`) the hold is still reported (`0xd8` down/up) but the movement arrives as
  ordinary `REL_X/REL_Y` on the mouse node.
* Diversions are temporary: they are lost when the remote power-cycles, and the tools release them on exit
  (flags `0x22`). Nothing is stored in the device.

## Device behaviour that the daemon must handle

* **The remote sleeps.** After a short idle period the first HID++ request gets no answer; a button press, or
  repeating the request, wakes it. Diversions may have to be applied again after a wake-up or reconnect.
* Feature indices are per device and discovered at run time (IRoot.GetFeature): on this unit
  ReprogramControlsV4 (`0x1b04`) is index `0x07`, PresenterControl (`0x1a00`, vibration) is index `0x09`.
* **Vibration** (original Spotlight): long message `11 01 <idx> 1d <length> e8 <intensity> …`.
  Blind test, one rater, one trial per cell, random order, 2026-10-01
  ([tools/spotlight_vibration_test.py](../../tools/spotlight_vibration_test.py)); rating 0 none, 1 faint,
  2 clear, 3 strong, 4 too strong:

  | length \ intensity | `0x40` | `0x80` | `0xc0` | `0xff` |
  |---|---|---|---|---|
  | 1 | 1 | 2 (short) | 3 | 4 (short) |
  | 2 | 1 | 3 | 3 | 4 |
  | 3 | 1 | 2 | 4 | 4 |

  **How to read this.** One rater, one trial per cell, a single random order: individual ratings are noisy, and
  the few cells that look inverted (for example `0x80` rated above `0xc0` at length 2 versus length 3) are noise,
  not findings. The model used is the obvious one: **a higher intensity byte is felt stronger, a longer length
  lasts longer**. What the data do add: only about **three** strength steps were told apart over the range
  `0x40`..`0xff`, so do not promise finely graded levels, and **length 0 is not felt at all** (Projecteur's timer
  passes length 0; here it would be silent).

  **Policy (decided by the maintainer): short pulses.** A long vibration distracts a speaker during a
  presentation. Use length 1 for every alert (timer, battery, confirmations) and tell alerts apart by the
  *number* of pulses, not their length. A longer pulse (length 3) is allowed only once, for "connection
  established". The slider maps to the intensity byte as percent x 2.55 (Windows' 50 % gives `0x80`).

## Over Bluetooth (measured 2026-10-01 with `projecteurd --verbose`, 5 minutes, one remote)

The daemon ran on the real remote, connected directly (no receiver). The same events as on USB arrive:

| Action | What the daemon saw |
|---|---|
| Next / Back | `KEY_RIGHT` / `KEY_LEFT` on the keyboard node; 54 presses (32 / 22) all forwarded 1:1 on the virtual keyboard, same millisecond |
| Single click | `BTN_LEFT` (272) on the mouse node, no HID++ message |
| Double click | HID++ `0xdf` (6 of 6 recognised); no mouse button event with it |
| Hold | HID++ `0xd8` down / up with raw X/Y (9 of 9 holds, 15 to 334 movement reports each) |

* The handshake works with device index `0xff` and 20 byte messages only; the remote is ready 0.35 s after
  start, and the "connected" pulse is acknowledged (`11 ff 09 1d 00 ...`).
* Raw X/Y reports arrive at **about 100 per second** (86 to 100 Hz over nine holds).
* The raw counts vary a lot between holds (|sum| from about 140 to about 12,000 in one deliberate wide sweep),
  so the pixels-per-count gain is still a guess and needs a calibration run with a known swing.
* The Bluetooth hidraw node also delivers the remote's *ordinary* input as report 1 (keyboard, 8 bytes,
  `01 00 4f ...` = Right arrow) and report 2 (mouse, 8 bytes, `02 01 ...` = button, `02 00 00 14 00 fd ...` =
  movement). `HidppLink` ignores everything that is not report `0x10` / `0x11`.
* In 3 of 9 holds a plain mouse movement report (report 2) came in just before the release. The daemon holds
  the exclusive grab, so it never reaches the compositor.

## Not measured yet

* The double-click time window. With slow double-clicks (0.9 to 1.4 s) the first click was reported as
  `BTN_MOUSE` and the second as `0xdf`; with fast ones no `BTN_MOUSE` preceded the `0xdf` in several cases.
  Whether the device delays the single-click report is unknown (needs a test with known press times).
* Latency from pressing the action button to `0xd8` down (the hold threshold).
* Whether Bluetooth differs in *timing* from USB (see the Bluetooth section for what was verified).
* How the pointer-speed setting maps to the device (`PointerSpeed`, feature `0x2205`) and to a gain for the
  raw X/Y counts.
* Why Back and the held-button keys seemed dead through Projecteur on the dongle on 2026-09-29. The raw
  hardware is fine (above), so the fault was after the device and does not matter once the daemon replaces
  Projecteur's forwarding.

## What this means for the daemon

1. At connect (and after every wake-up) divert `0xd8` with `0x33` and `0xdf` with `0x03`. With *cursor control*
   on, divert `0xd8` with `0x03` instead so the movement moves the real pointer.
2. Capture the receiver's mouse node exclusively: otherwise a short press clicks in the app under the pointer.
   Forward it only when cursor control is on.
3. Inputs of the effect state machine: hold down, raw X/Y deltas, hold up, short press, double-click, Next, Back.
   Mapping to the overlay (D-Bus `org.projecteur.Overlay1`): hold down shows the effect at the pointer
   (`ShowAtPointer`) unless a frozen one is visible, raw X/Y moves it (`MoveBy`), hold up freezes or hides it,
   short press hides it, double-click selects the next mode (Highlight, Magnify, Laser, wrap).
4. Next/Back are forwarded as the key events the remote sends (or run their mapped action).
