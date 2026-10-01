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
* **Vibration** (original Spotlight): long message `11 01 <idx> 1d <length> e8 <intensity> …`. Length `0` is
  **not felt**, `2` is clearly felt, `3` with intensity `0xc0` is strong and long, `1` with `0x80` is the soft
  feedback pulse used by the tools. (Projecteur's timer passes length 0; that would be silent on this unit.)

## Not measured yet

* The double-click time window. With slow double-clicks (0.9 to 1.4 s) the first click was reported as
  `BTN_MOUSE` and the second as `0xdf`; with fast ones no `BTN_MOUSE` preceded the `0xdf` in several cases.
  Whether the device delays the single-click report is unknown (needs a test with known press times).
* Latency from pressing the action button to `0xd8` down (the hold threshold).
* The same events over **Bluetooth** (HID++ over BLE). Next/Back are identical; the rest is unverified.
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
