# Cross-Pad Gesture: Design Overview

> **⚠️ Proof of Concept (PoC)**
> This feature is an independent extension not included in the upstream driver.
> It comes with no warranty and may be changed or removed without notice.

## What is Cross-Pad Gesture?

Cross-pad gesture enables **cooperative gestures between two trackpads** on a
split keyboard. When both trackpads are touched simultaneously, special gestures
are triggered instead of normal cursor/scroll behavior.

This is analogous to multi-finger gestures on a single laptop trackpad, but
distributed across two physically separate devices connected via BLE.

### Supported Gestures

| Left | Right | Gesture |
|:----:|:-----:|---------|
| 1 finger | 1 finger | **Pinch zoom** — move fingers apart/together horizontally to zoom in/out |
| 1 finger | 2 fingers | **Press & hold** — left-click held + cursor movement (drag & drop) |
| 2 fingers | 1 finger | **Press & hold** — same as above |
| 2 fingers | 2 fingers | **Press & hold** — same as above |
| 3 fingers | any | None (reserved for future use) |

Gesture assignment is controlled by a simple dispatch table (`iqs9151_cross_pad_resolve()`).
Changing which gesture maps to which finger combination requires editing only a single
`switch` statement.

## Architecture

### Hardware Layout

```
┌─────────────────┐          BLE           ┌─────────────────┐
│   Left MCU       │  ◄──────────────────►  │   Right MCU      │
│  (peripheral)    │    ZMK split transport  │  (central)       │
│  iqs9151 × 1    │                         │  iqs9151 × 1    │
└─────────────────┘                         └─────────────────┘
```

### Bidirectional Touch State Notification

Each side must know the other's finger count to resolve gestures:

| Direction | Mechanism | Data |
|-----------|-----------|------|
| peripheral → central | `EV_MSC` input event via split transport | finger count, rel_x, rel_y |
| central → peripheral | `INVOKE_BEHAVIOR("pdt")` via BLE | finger count |

No modifications to ZMK core are required — both `EV_MSC` forwarding and
`INVOKE_BEHAVIOR` work with the existing split transport infrastructure.

### Dispatch Table

```c
enum cross_pad_gesture {
    CROSS_PAD_GESTURE_NONE = 0,
    CROSS_PAD_GESTURE_PINCH,
    CROSS_PAD_GESTURE_PRESS_HOLD,
};

static enum cross_pad_gesture iqs9151_cross_pad_resolve(uint8_t local_fc,
                                                         uint8_t peer_fc) {
    if (local_fc == 0U || peer_fc == 0U) {
        return CROSS_PAD_GESTURE_NONE;
    }
    switch (MAX(local_fc, peer_fc)) {
    case 1:  return CROSS_PAD_GESTURE_PINCH;       /* both 1F → pinch */
    case 2:  return CROSS_PAD_GESTURE_PRESS_HOLD;  /* either 2F → press & hold */
    case 3:  return CROSS_PAD_GESTURE_NONE;         /* either 3F → reserved */
    default: return CROSS_PAD_GESTURE_NONE;
    }
}
```

## Key Design Decisions

### Gesture Start Stabilization (Hold-Tap Inspired)

When both trackpads are touched and a gesture would start, the system does **not**
commit immediately. Instead, it enters an "undecided" state and waits for
`CROSS_PAD_STABILIZE_MS` (default: 50ms) for `finger_count` to stabilize.

**Problem:** Placing two fingers sequentially (e.g., for press & hold) creates a
transient state where `finger_count` briefly reads as 1 before settling to 2.
Without stabilization, this would trigger a momentary pinch gesture.

**Solution:** Inspired by ZMK's hold-tap behavior, which defers the tap/hold
decision until enough information is available:

- On first detection of both-side touch, record timestamp and `fc` snapshot
- Swallow incoming frames (`return true`) during the wait
- If `fc` changes, restart the timer
- Once stable for the required duration, commit to the gesture
- If fingers are released during the wait, cancel silently

Single-side operations (`peer_fc == 0`) never enter the undecided state, so
normal cursor movement and scrolling are completely unaffected.

### Stateless vs. Stateful Gesture Management

Different gestures require different state management:

**Pinch (stateless):** Evaluated every frame via `resolve()`. Ends immediately
when either side lifts all fingers.

**Press & hold (stateful):** Once started, remains active as long as
`MAX(local_fc, peer_fc) == 2`. The 1-finger side can be lifted and re-placed
(to reposition for continued dragging) without ending the hold.

### Direct HID Report Manipulation

Modifier keys and mouse buttons are sent directly via ZMK's HID APIs
(`zmk_hid_register_mod()`, `zmk_hid_mouse_button_press()`), bypassing the
keymap entirely. This eliminates an implicit dependency on keymap configuration
that existed in the initial design (which routed through `INPUT_BTN_7` →
input processor → keymap).

The modifier for pinch zoom is configurable via
`CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER`:
- `0` = None (REL_WHEEL only)
- `1` = Left Ctrl (default; Ctrl+Wheel zoom on most OSes)
- `2` = Mouse Button 4 (for macOS utilities like BetterTouchTool)

### Centroid Delta for Movement

Both pinch and press & hold compute movement from **centroid deltas** (difference
in finger position centroid between frames) rather than the hardware-reported
`frame->rel_x`. This provides a unified approach regardless of finger count
and matches the method already used by the driver's normal 2-finger scroll.

### Central-Side Aggregation

All gesture output (HID reports) is produced on the central side only.
The peripheral sends its movement data via `EV_MSC` events, and the central
aggregates both sides' data into a single output:

- **Pinch:** Both sides' X deltas → single `REL_WHEEL` output
- **Press & hold:** Both sides' X/Y deltas → single `REL_X` + `REL_Y` output

### Peer State Release

When the peer's `finger_count` is updated via BLE (`iqs9151_set_peer_state()`),
the gesture end conditions are re-evaluated immediately. This handles the case
where the local side has already lifted all fingers (no more frames being
generated) and the peer update is the only trigger for releasing the held
button or modifier.

## Configuration

### Kconfig

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_INPUT_IQS9151_CROSS_PAD` | bool | `n` | Enable cross-pad gesture |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT` | choice | — | This trackpad is on the left |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_RIGHT` | choice | — | This trackpad is on the right |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_GAIN_X10` | int | `40` | Pinch wheel output gain (10=1.0x, 40=4.0x) |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER` | int | `1` | Pinch modifier (0=none, 1=LCtrl, 2=MB4) |

### DTS

A `pdt` behavior node and `cross-pad-peer-input` property are required.
See [README](../README.md) for setup instructions.

## Files

| File | Purpose |
|------|---------|
| `drivers/input/iqs9151.c` | Core cross-pad logic, frame handling, proxy callback |
| `drivers/input/CMakeLists.txt` | ZMK app include path |
| `drivers/input/Kconfig` | Cross-pad Kconfig options |
| `dts/bindings/input/azoteq,iqs9151.yaml` | `cross-pad-peer-input` phandle property |
| `behaviors/behavior_pad_touch.c` | `pdt` behavior (receives central → peripheral notifications) |
| `dts/bindings/behaviors/zmk,behavior-pad-touch.yaml` | DT binding (`#binding-cells = 2`) |
| `include/iqs9151_cross_pad.h` | Public API header |

## Known Issues

### Touch Detection Instability at Trackpad Edges

The IQS9151 touch sensor may produce unreliable readings near the trackpad
edges, where finger contact area is reduced:

- Touch may flicker on/off near detection thresholds
- `finger_count` may oscillate between values (e.g., 1 ↔ 2)

This is a hardware characteristic. A potential software mitigation would be
to apply special handling for finger positions near the edge, but this would
affect the entire driver and is considered a future improvement.

## Related Documents (Japanese)

- [設計ドキュメント](cross_pad_gesture_design.md) — Full design document
- [実装設計](cross_pad_gesture_implementation.md) — Implementation details
- [通信設計](cross_pad_communication.md) — Communication design
- [リファレンス](cross_pad_reference.md) — Data structures, Kconfig, design decisions
