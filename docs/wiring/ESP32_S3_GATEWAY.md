# ESP32-S3-N16R8 Gateway — Wiring

**Status: Reference (from board photo — silkscreen read, not yet continuity-tested).**
Board identified from a physical photo: generic **ESP32-S3-N16R8** dev board (16MB
flash / 8MB PSRAM module, marked "ESP32-S3-N16R8 WiFi BT MODEL"). Dual USB-C ports, RST
and BOOT buttons, onboard WS2812 RGB LED, AMS1117 3.3V regulator. This is a common
"ESP32-S3 DevKitC-1 style" clone layout. GPIO numbers below are transcribed from the
silkscreen in the photo; still recommend a continuity check before soldering, since clone
boards occasionally mislabel silkscreen.

## 1. Module Description

Generic ESP32-S3-N16R8 development board, role: CarSentinel Gateway (Wi-Fi, BLE, ESP-NOW
coordinator, hosts GPS/IMU/dual-OLED I2C bus).

## 2. Voltage Requirements

- Input: 5V via either USB-C port, or `5V`/`VIN` header pin.
- Onboard AMS1117 regulates to 3.3V for the module.
- Logic: 3.3V on all GPIOs.

## 3. Pin Table (as silkscreened)

**Left edge (top → bottom):** `3V3, 3V3, RST, 4, 5, 6, 7, 15, 16, 17, 18, 8, 9, 10, 11,
12, 13, 14, 5V, GND`

**Right edge (top → bottom):** `1, RX, TX, GND, 42, 41, 40, 39, 38, 37, 36, 0, 45, 48,
47, 20, 21, 19`

**Reserved / avoid:**

| GPIO | Reason |
|---|---|
| 0 | BOOT strapping pin (must be LOW at reset to enter download mode) — avoid driving externally. |
| 3, 45, 46 | Other ESP32-S3 strapping pins (not all broken out on this board's header, but avoid if present). |
| 19, 20 | Native USB D-/D+ — committed to the native USB-C port; do not reuse if that port is used for programming/serial. |
| 43, 44 (U0TXD/U0RXD) | Not seen labeled on this board's header directly, but the board exposes a separate silkscreened `RX`/`TX` pair (see below) — likely the secondary USB-C's UART bridge or a dedicated UART0 breakout. Confirm with a multimeter/continuity check against the USB-serial chip before assuming general-purpose use. |
| 48 | Drives the onboard WS2812 RGB LED — avoid unless the LED is being intentionally used as a status indicator. |

## 4. ESP32 GPIO Connection — Proposed Assignments

Proposed (not yet physically wired/tested) assignments, chosen from the free GPIO list
above, avoiding strapping/USB/LED pins:

| Function | GPIO | Notes |
|---|---|---|
| I2C SDA (SSD1306 ×2 + MPU6050) | GPIO 8 | Free per silkscreen, commonly used as default SDA on ESP32-S3 boards. |
| I2C SCL | GPIO 9 | Free per silkscreen, commonly used as default SCL on ESP32-S3 boards. |
| GPS UART RX (ESP32 receives GPS TX) | GPIO 17 | Free, not a strapping/USB pin. |
| GPS UART TX (ESP32 transmits to GPS RX) | GPIO 18 | Free, not a strapping/USB pin. |

Both SSD1306 units report the default address `0x3C` (confirmed — see
`docs/wiring/SSD1306.md`), so they **cannot share GPIO 8/9 as a single I2C bus** — see
Section 9 below.

## 5. Power Connection

5V via USB-C or `5V` pin; sensors draw from `3V3` pin or a shared 3.3V/5V rail per their
own wiring docs.

## 6. Ground Connection

Common `GND` across gateway and all attached peripherals.

## 7. Communication Interface

I2C (SSD1306, MPU6050 — with the dual-bus caveat below), UART (NEO-6M GPS), USB-C
(programming/flashing via one of the two ports — confirm at bench time which port
carries the USB-serial bridge vs. native USB).

## 8. Pull-ups / Pull-downs

I2C requires pull-ups on SDA/SCL — rely on onboard pull-ups on the MPU6050/SSD1306
breakouts (see their wiring docs) rather than adding board-side pull-ups, unless bus
testing shows they're needed.

## 9. Known Conflicts

**Confirmed dual-OLED address conflict:** both SSD1306 units read `0x3C` with no visible
address-select jumper populated. They cannot coexist on one I2C bus at the same address.
Two options, to be decided in Phase 13:

1. **Second I2C bus**: ESP32-S3 supports a second hardware I2C peripheral. Assign a
   second SDA/SCL pair (e.g. GPIO 10/11, both free per the silkscreen list) to the second
   display.
2. **Software-selected addressing**: some SSD1306 breakouts allow bridging a solder pad
   to change the address to `0x3D` — inspect both physical units under magnification for
   an unbridged pad before committing to the two-bus approach, since it's simpler if
   available.

Recommendation: **default to a second I2C bus (GPIO 10 = SDA2, GPIO 11 = SCL2)** unless
a spare address pad is found, since it needs no rework of the OLED boards.

## 10. Safety Warnings

- Confirm which USB-C port is the USB-serial programming port before connecting a
  programmer to both simultaneously.
- GPIO 0 must not be pulled by external wiring at boot, or the board will enter/fail to
  exit download mode unexpectedly.

## 11. Testing Procedure

1. Flash a minimal blink/serial sketch via each USB-C port to identify which is
   USB-serial (for programming) vs. native USB.
2. Run an I2C scanner on GPIO 8/9 with one SSD1306 + MPU6050 attached, confirm addresses
   `0x3C` and `0x68`.
3. Attach the second SSD1306 on GPIO 10/11 (second I2C bus) or the same bus if an address
   jumper is found, confirm both displays render independently.
4. Wire GY-NEO6MV2 to GPIO 17/18, confirm NMEA sentences are received.

## 12. Example Configuration

Deferred to Phase 3/8/9/13 firmware implementation — hardware profile
`configs/hardware/ESP32_S3_GATEWAY.json` will record these pin assignments once bench
step 11 above confirms them electrically.

## 13. Board-Specific Differences

This is a generic/clone board, not an official Espressif DevKitC-1 — silkscreen pin
numbering was read directly from the physical unit's edges rather than assumed from a
datasheet. If a different gateway unit is substituted later, re-photograph and
re-transcribe rather than reusing this table.
