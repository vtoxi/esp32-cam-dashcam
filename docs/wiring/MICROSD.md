# microSD (Onboard ESP32-CAM Slot) — Wiring Reference

**Status: Reference (unverified).** The AI-Thinker ESP32-CAM has an onboard microSD slot
wired to fixed GPIOs on the module — not separately wired by the user, but its GPIO usage
is recorded here since it directly constrains what other peripherals can use.

## 1. Module Description

Onboard microSD card slot on the AI-Thinker ESP32-CAM board, using the ESP32's SDMMC
peripheral (not SPI mode, by default in most AI-Thinker example code).

## 2. Voltage Requirements

3.3V (onboard slot, no separate wiring — card is powered from the ESP32-CAM's own 3.3V
rail).

## 3. Pin Table (SDMMC 1-bit mode, reference)

| Function | GPIO |
|---|---|
| CLK | 14 |
| CMD | 15 |
| DATA0 | 2 |
| DATA1 (4-bit mode only) | 4 (conflicts with flash LED) |
| DATA2 (4-bit mode only) | 12 |
| DATA3 (4-bit mode only) | 13 |

Project plan: use **1-bit SDMMC mode** (CLK, CMD, DATA0 only) to avoid the GPIO4
flash-LED conflict and free GPIO 4, 12, 13 for other use — subject to confirmation in
Phase 3 that those freed pins are actually usable (GPIO 12 is boot-strapping-sensitive,
see `docs/wiring/ESP32_CAM.md` Section 3).

## 4. ESP32 GPIO Connection

Fixed by the board (see table above) — not user-configurable wiring, but the resulting
free/occupied GPIO set constrains RCWL/DHT placement.

## 5–6. Power / Ground

Internal to the board, no external wiring.

## 7. Communication Interface

SDMMC (1-bit mode planned).

## 8. Pull-ups / Pull-downs

SD lines typically need pull-ups (often present on the ESP32-CAM board itself for the SD
slot) — no external wiring expected.

## 9. Known Conflicts

- GPIO4 (DATA1, 4-bit mode) vs. onboard flash LED — avoided by using 1-bit mode.
- GPIO12 is a boot-strapping pin; using it (4-bit mode DATA2) risks boot failures if an
  external pull changes its reset-time level — another reason 1-bit mode is preferred.

## 10. Safety Warnings

None beyond standard SD card handling (format as FAT32, avoid hot-removal during writes).

## 11. Testing Procedure

1. Insert a FAT32-formatted microSD card.
2. Run a minimal SD mount example (1-bit mode) via the `SD_MMC` Arduino library, confirm
   mount success and a test file write/read.
3. Confirm camera capture still functions with SD mounted (verifies no unexpected pin
   contention).

## 12. Example Configuration

Deferred to Phase 4 firmware implementation.

## 13. Board-Specific Differences

Some ESP32-CAM clones wire the SD slot differently or omit it — confirmed hardware for
this project is standard AI-Thinker with onboard slot.
