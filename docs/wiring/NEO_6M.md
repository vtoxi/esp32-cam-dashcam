# NEO-6M GPS (GY-NEO6MV2) — Wiring

**Status: GPS-side pinout confirmed from photo; gateway-side GPIO proposed (untested).**
Physical unit confirmed silkscreened `VCC RX TX GND`, u-blox NEO-6M-0-001 module, marked
`GY-NEO6MV2` with detached ceramic patch antenna on a U.FL connector. Gateway-side pins
proposed in `docs/wiring/ESP32_S3_GATEWAY.md` (GPIO 17/18) pending bench verification.

## 1. Module Description

GY-NEO6MV2 breakout: u-blox NEO-6M GPS receiver with onboard EEPROM (stores
configuration across power cycles) and ceramic patch antenna. Outputs NMEA sentences
over UART.

## 2. Voltage Requirements

- VCC: 3.3V–5V (GY-NEO6MV2 typically has an onboard regulator accepting up to 5V; verify
  against the specific unit's silkscreen/datasheet — some variants are 3.3V-only).
- TX/RX logic level: **verify against the physical unit before connecting.** Many
  GY-NEO6MV2 boards output 3.3V logic on TX regardless of VCC voltage, but this must not
  be assumed — feeding a 5V logic signal into an ESP32 RX pin without a level shifter can
  damage the GPIO.

## 3. Pin Table

| Pin | Connects to |
|---|---|
| VCC | 3.3V or 5V (per board regulator — verify) |
| RX (GPS receives) | ESP32-S3 gateway GPIO 18 (proposed TX) — see `docs/wiring/ESP32_S3_GATEWAY.md` |
| TX (GPS transmits) | ESP32-S3 gateway GPIO 17 (proposed RX) — see `docs/wiring/ESP32_S3_GATEWAY.md` |
| GND | Common ground |

## 4. ESP32 GPIO Connection

Proposed: gateway GPIO 17 = UART RX (receives GPS TX), GPIO 18 = UART TX (drives GPS RX).
Not yet bench-verified — see `docs/wiring/ESP32_S3_GATEWAY.md` Section 11 testing steps.

## 5–6. Power / Ground

Per Pin Table above; common ground with gateway required.

## 7. Communication Interface

UART, default NEO-6M baud rate 9600 (configurable via u-blox U-Center tool if needed).
Use a library such as TinyGPS++ to parse NMEA sentences.

## 8. Pull-ups / Pull-downs

None required.

## 9. Known Conflicts

Must avoid the gateway's UART0 (used for USB/flashing/console) — use a secondary
hardware UART on the ESP32-S3 (which has multiple UART peripherals available) once the
board's pin map is known.

## 10. Safety Warnings

Verify TX logic voltage against ESP32-S3 RX pin tolerance (3.3V) before connecting — do
not assume 3.3V without checking the specific board.

## 11. Testing Procedure

1. Power module, confirm onboard LED blinks once GPS fix is acquired (behavior varies by
   board — check silkscreen/datasheet).
2. Read raw NMEA sentences over UART at 9600 baud to confirm communication before adding
   a parsing library.
3. Test outdoors or near a window — indoor GPS fix can be slow/unreliable.

## 12. Example Configuration

Deferred to Phase 8 firmware implementation.

## 13. Board-Specific Differences

Some NEO-6M breakouts lack the EEPROM (settings reset on power loss) — the GY-NEO6MV2
variant confirmed for this project does include it.
