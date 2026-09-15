# DHT11 Temperature/Humidity Sensor — Wiring (TBD)

**Status: TBD.** 2 units confirmed on hand. Which two nodes host them is undecided
(Phase 6 node assignment); GPIO assignment on whichever camera node(s) get one depends on
which free pins are confirmed on the AI-Thinker ESP32-CAM in Phase 3 (see
`docs/wiring/ESP32_CAM.md` Section 3). Wiring below applies per-unit, identically for
both.

## 1. Module Description

DHT11 digital temperature/humidity sensor, single-wire digital protocol (not true
1-Wire/OneWire, but a similar timed-pulse protocol specific to the DHTxx family).

## 2. Voltage Requirements

- VCC: 3.3V–5.5V (DHT11 typically rated 3–5.5V; confirm against the specific breakout —
  running at 3.3V from the ESP32-CAM rail is expected to work but reduces range/response
  time slightly per datasheet).
- Logic level: matches VCC — at 3.3V supply the data line is 3.3V-safe for direct ESP32
  connection.

## 3. Pin Table

| Pin | Connects to |
|---|---|
| VCC | 3.3V |
| GND | Common ground |
| DATA | ESP32-CAM free GPIO — **TBD**, see Phase 3 |

Some breakout boards have a 4th NC pin — ignore it.

## 4. ESP32 GPIO Connection

TBD — pending Phase 3 free-GPIO determination on AI-Thinker ESP32-CAM.

## 5–6. Power / Ground

VCC to 3.3V, GND common with ESP32-CAM.

## 7. Communication Interface

Single-wire digital, timed pulse protocol (use a DHT library, e.g. Adafruit DHT or
`DHT sensor library` — handles timing).

## 8. Pull-ups / Pull-downs

4.7kΩ–10kΩ pull-up resistor on DATA line to VCC. Most 3-pin breakout modules include
this onboard — verify before adding an external one (double pull-up is usually harmless
but check the specific board).

## 9. Known Conflicts

Whichever GPIO is chosen must not collide with camera/SD/RCWL pins — see
`docs/wiring/ESP32_CAM.md`.

## 10. Safety Warnings

None specific beyond standard 3.3V logic precautions.

## 11. Testing Procedure

1. Wire per pin table, run a minimal DHT11 read example sketch.
2. Confirm plausible temperature/humidity values (not NaN — a common DHT11 failure mode
   from wiring/timing issues).
3. Verify read interval respects DHT11's minimum ~1s sampling interval.

## 12. Example Configuration

Deferred to Phase 3/4 firmware implementation.

## 13. Board-Specific Differences

3-pin vs 4-pin breakout only affects whether an NC pin exists; wiring is otherwise
consistent across DHT11 breakout variants.
