# CarSentinel Wiring Documentation

This directory contains one wiring document per hardware module used in CarSentinel.
Every document follows the same structure (module description, voltage requirements, pin
table, ESP32 GPIO connection, power/ground, communication interface, pull-ups, known
conflicts, safety warnings, testing procedure, example configuration, board-specific
differences).

## Status Legend

- **Verified**: pin table matches the physically inspected board.
- **Reference (unverified)**: a well-established public pinout for this exact board
  variant, included as a starting point, but not yet cross-checked against the physical
  unit in this project. Do not wire from this without a continuity/multimeter check first.
- **TBD**: exact board/pins genuinely unknown — do not wire anything until resolved.

## Documents

| File | Module | Status |
|---|---|---|
| [ESP32_CAM.md](ESP32_CAM.md) | AI-Thinker ESP32-CAM | Reference (unverified) |
| [ESP32_S3_GATEWAY.md](ESP32_S3_GATEWAY.md) | ESP32-S3-N16R8 gateway | Board confirmed from photo; proposed GPIOs pending bench test |
| [RCWL_0516.md](RCWL_0516.md) | RCWL-0516 motion sensor | Sensor pinout confirmed; ESP32-CAM-side GPIO TBD (Phase 3) |
| [DHT11.md](DHT11.md) | DHT11 temp/humidity | TBD — GPIO depends on ESP32-CAM conflict resolution |
| [NEO_6M.md](NEO_6M.md) | NEO-6M GPS (GY-NEO6MV2) | Sensor pinout confirmed; gateway GPIO proposed pending bench test |
| [MPU6050_HW123.md](MPU6050_HW123.md) | MPU6050 (HW-123) | Sensor pinout confirmed; gateway GPIO proposed pending bench test |
| [SSD1306.md](SSD1306.md) | SSD1306 OLED ×2 | Address conflict confirmed (both `0x3C`); resolved via second I2C bus, pending bench test |
| [MICROSD.md](MICROSD.md) | microSD (onboard ESP32-CAM slot) | Reference (unverified) — shares pins with camera |
| [POWER.md](POWER.md) | Power architecture | Documented (prototype: independent bench supply, no vehicle wiring yet) |

See `docs/HARDWARE.md` for the full hardware inventory and open questions driving the TBD
items above.
