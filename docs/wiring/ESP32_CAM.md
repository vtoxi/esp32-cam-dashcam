# AI-Thinker ESP32-CAM — Wiring Reference

**Status: Reference (unverified).** This is the widely-published AI-Thinker ESP32-CAM
pinout. It has not yet been cross-checked against the physical board in this project.
Verify with a multimeter/continuity test and the board's silkscreen before wiring
anything, per project rule "never assume all ESP32-CAM boards are identical."

## 1. Module Description

AI-Thinker ESP32-CAM: ESP32-S module + OV2640 camera + microSD slot on one board. No
onboard USB-to-serial — flashing requires an external FTDI/USB-TTL adapter wired to
UART0, plus grounding GPIO0 during flash mode.

## 2. Voltage Requirements

- Board logic: 3.3V
- Power input pin (5V pin on most boards): 5V, board regulates down to 3.3V on-board.
  Some boards accept 3.3V directly on the 3V3 pin instead — **verify silkscreen labeling
  before connecting**, applying 5V to a 3.3V-only pin can destroy the module.

## 3. Pin Table (reference, AI-Thinker layout)

| Function | GPIO | Notes |
|---|---|---|
| Camera (OV2640), all pins | GPIO 0, 5, 18, 19, 21, 22, 23, 25, 26, 27, 32, 34, 35, 36, 39 | Reserved by camera interface, not available for other peripherals. Per-signal mapping below (the standard, widely-published `CAMERA_MODEL_AI_THINKER` pin set used by Espressif's own esp32-camera examples — same status as the rest of this document: reference, not yet bench-verified against this specific unit). |

**Per-signal camera pin mapping (used by `firmware/lib/CarSentinelCommon/CameraManager.cpp`):**

| Signal | GPIO | Signal | GPIO |
|---|---|---|---|
| PWDN | 32 | Y5 | 21 |
| RESET | -1 (not connected) | Y4 | 19 |
| XCLK | 0 | Y3 | 18 |
| SIOD (I2C SDA to OV2640) | 26 | Y2 | 5 |
| SIOC (I2C SCL to OV2640) | 27 | VSYNC | 25 |
| Y9 | 35 | HREF | 23 |
| Y8 | 34 | PCLK | 22 |
| Y7 | 39 | | |
| Y6 | 36 | | |
| microSD (1-bit SDMMC mode) | GPIO 2, 4, 12, 13, 14, 15 | Shared bus with camera flash LED on GPIO 4. |
| Onboard flash LED | GPIO 4 | Conflicts with SD_DATA1 in 4-bit SD mode — project will use 1-bit SD mode. |
| Onboard status LED (red) | GPIO 33 | Free for use if not needed as indicator. |
| UART0 (flashing/console) | GPIO 1 (TX), GPIO 3 (RX) | Needed for programming; avoid using for peripherals. |
| Boot-strapping pins | GPIO 0, 2, 12 | GPIO0 must be LOW to enter flash mode, HIGH/floating for normal boot. GPIO12 affects flash voltage mode at boot — avoid pulling high externally. |

**Remaining GPIOs realistically free for RCWL/DHT on a camera node with SD + camera
both active: TBD — must be determined empirically per board**, since AI-Thinker clones
vary slightly in which pins are actually broken out to headers. Likely candidates based
on the public reference pinout: GPIO 13, GPIO 14, GPIO 15, GPIO 16 (if present) — subject
to conflicting with SD lines depending on SD bus mode chosen. This will be finalized in
Phase 3 against the physical board.

## 4. ESP32 GPIO Connection

Not applicable beyond the table above — this module IS the ESP32 node.

## 5. Power Connection

- 5V and GND from external regulated supply (bench supply during prototyping — see
  [POWER.md](POWER.md)).
- Do not power from USB and external supply simultaneously without a diode/isolation —
  can create a voltage conflict.

## 6. Ground Connection

Common ground required between ESP32-CAM, FTDI programmer, and any attached sensors
(RCWL, DHT).

## 7. Communication Interface

- Camera: parallel DVP interface (fixed pins above).
- microSD: SDMMC.
- Sensors (RCWL digital out, DHT 1-wire): whichever free GPIOs are confirmed in Phase 3.

## 8. Pull-ups / Pull-downs

- DHT11 data line typically needs a 4.7kΩ–10kΩ pull-up to 3.3V (many breakout boards
  include this onboard — verify before adding an external one).
- GPIO0: needs to be pulled LOW only during flashing (button or jumper to GND), left
  floating/HIGH for normal boot.

## 9. Known Conflicts

- Camera + SD together leave very few GPIOs — see Section 3 of `docs/HARDWARE.md`.
- GPIO 4 (flash LED) vs SD 4-bit mode.
- GPIO 1/3 reserved for UART0 (flashing/serial console) — cannot be reused while
  retaining USB-serial debug/flash capability.

## 10. Safety Warnings

- Confirm 5V vs 3.3V pin before connecting power — wrong voltage can destroy the module.
- Do not hot-plug the FTDI adapter while the board is powered from another source.

## 11. Testing Procedure

1. Flash a minimal "hello world" sketch via FTDI (GPIO0 to GND during upload, remove
   after).
2. Verify camera init succeeds (`esp32-camera` example sketch) before adding SD/sensors.
3. Add SD, verify mount, before wiring RCWL/DHT.
4. Add RCWL/DHT one at a time, verifying no camera/SD regression after each.

## 12. Example Configuration

Deferred — hardware profile JSON (`configs/hardware/ESP32_CAM_AI_THINKER.json`) will be
created once Phase 3 confirms the actual free-GPIO set against the physical board.

## 13. Board-Specific Differences

AI-Thinker clones (Freenove, generic) may differ in which header pins are physically
broken out, and in flash LED wiring. This document targets the standard AI-Thinker
reference design; if a Freenove or other clone is used, re-verify against its own
datasheet/silkscreen.
