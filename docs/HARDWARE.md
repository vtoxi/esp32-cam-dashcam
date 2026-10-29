# CarSentinel — Hardware Inventory (Phase 0)

This document records the exact hardware CarSentinel targets. It is the source of truth
for hardware profiles (`configs/hardware/*.json`) and wiring documentation
(`docs/wiring/*.md`). Nothing here is guessed — anything not yet verified against the
physical board is marked **TBD**.

## Confirmed Hardware

| Role | Module | Exact Variant | Interface | Notes |
|---|---|---|---|---|
| Camera node MCU | ESP32-CAM | AI-Thinker ESP32-CAM | — | No onboard USB-serial; requires FTDI/USB-TTL adapter to flash. Camera (OV2640) and microSD share GPIOs — see conflicts below. |
| Gateway MCU | ESP32-S3 | Generic ESP32-S3-WROOM dev board (exact vendor/model TBD — see Open Questions) | — | Has native USB, more GPIOs, no camera/SD pin conflicts inherent to the module itself. |
| Motion sensor | RCWL-0516 | Standard microwave radar breakout | Digital OUT (3.3V logic) | 4–28V input, onboard 3.3V regulator on most breakouts — verify against physical unit. |
| Temp/humidity | DHT11 | Standard 3-pin or 4-pin breakout | 1-Wire digital | Lower precision than DHT22; fine for cabin/ambient monitoring. |
| Display | SSD1306 OLED | 0.96" 128x64, I2C | I2C (SDA/SCL) | 4-pin (VCC/GND/SDA/SCL). Default I2C address typically `0x3C` (verify — some clones use `0x3D`). Two units planned — see I2C Bus Plan below. |
| GPS | NEO-6M | GY-NEO6MV2 (EEPROM + ceramic antenna variant) | UART (TX/RX), 3.3V logic | Module itself runs 3.3–5V depending on board regulator; UART pins are 3.3V logic on GY-NEO6MV2 — verify against physical unit before wiring directly to ESP32 UART. |
| IMU | MPU6050 | GY-521 breakout | I2C (SDA/SCL) | Default I2C address `0x68` (`0x69` if AD0 pulled high). Shares I2C bus with SSD1306 — no address conflict expected. |
| Storage | microSD | Onboard slot on AI-Thinker ESP32-CAM | SDMMC (shares GPIOs with camera on AI-Thinker) | AI-Thinker uses 1-bit SDMMC mode sharing pins with camera (Y2, GPIO4 flash LED conflicts with SD in some configs) — exact pin table in `docs/wiring/ESP32_CAM.md`. |

## Confirmed from Physical Photos (this update)

| Item | Confirmed detail |
|---|---|
| Gateway board | **ESP32-S3-N16R8** dev board (16MB flash / 8MB PSRAM), dual USB-C, RST/BOOT buttons, onboard WS2812 RGB LED, AMS1117 3.3V regulator. Silkscreen GPIO numbers transcribed in `docs/wiring/ESP32_S3_GATEWAY.md`. |
| RCWL-0516 pinout | Confirmed 5-pad header: `CDS, VIN, OUT, GND, 3V3` (CDS = optional daylight-disable photoresistor input, unused). |
| NEO-6M module | Confirmed GY-NEO6MV2, u-blox NEO-6M-0-001, 4-pin header `VCC, RX, TX, GND`, detached ceramic antenna on U.FL. |
| MPU6050 breakout | Confirmed `HW-123` silkscreen (functionally identical to GY-521), 8-pin header `VCC, GND, SCL, SDA, XDA, XCL, AD0, INT`. |
| SSD1306 ×2 address | **Both confirmed at default address `0x3C`, no address-select jumper found on either unit.** Resolved via a second I2C bus on the gateway rather than address sharing — see `docs/wiring/SSD1306.md`. |

Gateway-side GPIO assignments (I2C SDA/SCL, second I2C bus, GPS UART) are **proposed**
in `docs/wiring/ESP32_S3_GATEWAY.md` from the silkscreen-visible free pins, but not yet
bench/continuity-tested — treat as "reference, pending verification," not final.

## Confirmed Quantities

| Item | Quantity | Notes |
|---|---|---|
| RCWL-0516 | Dozens on hand | Not all need to be deployed — one per camera node (front/rear/left/right) is the expected initial use; spares available for replacement/testing without a reorder. |
| NEO-6M (GY-NEO6MV2) | 1 | Single GPS unit, gateway/vehicle-level per architecture (Section 3) — one fix serves the whole vehicle, not per-camera. |
| MPU6050 (HW-123) | 1 | Single IMU, gateway/vehicle-level per architecture — mounted centrally, not per-camera. |
| DHT11 | 2 | Two temperature/humidity sensors. Node assignment **TBD** — likely one cabin-facing (e.g. interior/gateway-adjacent) and one exterior-facing camera node, but which physical nodes get them is an open question (see below). |

## Unconfirmed / Not Yet Selected

| Item | Status |
|---|---|
| Which two nodes/locations get the 2 DHT11 units | **TBD** — decide during Phase 6 node naming/assignment; does not block Phase 0–5. |
| Which camera nodes get an RCWL-0516 (all 4, or a subset) | **TBD** — plenty of spare units on hand, so this is a deployment choice, not a supply constraint. |
| microSD card capacity/class per camera node | **TBD** |
| Power supply design (regulator, source voltage from vehicle) | **TBD** — Section 42 of the spec explicitly defers vehicle wiring; use a bench/independent regulated 5V supply for prototype phase. |
| Which of the gateway's two USB-C ports is USB-serial vs. native USB | **TBD** — to be determined by flashing a test sketch via each port (see `docs/wiring/ESP32_S3_GATEWAY.md` Section 11). |

## GPIO Conflicts to Resolve in Phase 3 (documented now, not solved now)

These are known structural conflicts on **AI-Thinker ESP32-CAM** specifically — they do not
block Phase 0, but must be resolved before Phase 3 (Hardware Capability Layer):

1. **Camera + microSD**: AI-Thinker ESP32-CAM has very few free GPIOs once camera and SD are
   both active. Historically only GPIO 0, 2, 4, 12–16 have any availability, and several of
   those are boot-strapping pins (GPIO 0, 2, 12) that must be handled carefully at reset.
2. **GPIO 4**: Shared between the onboard flash LED and SD_DATA1 in 4-bit SD mode. Most
   AI-Thinker projects use 1-bit SD mode to avoid this, at the cost of slower SD writes.
3. **UART0 (GPIO 1/3)**: Used for programming/serial console. If GPS UART is needed on a
   camera node (not currently planned — GPS is gateway/sensor-node scoped), it would need a
   second UART via software serial, competing with the already-scarce GPIO budget.
4. **I2C for OLED/IMU**: AI-Thinker ESP32-CAM has no dedicated I2C-safe pins free when camera
   is active; if a camera node ever needs OLED/IMU directly (not currently planned — those are
   gateway-side in Section 3), pin selection will need careful verification against the exact
   board, not assumption.

Because GPS and IMU are architected as gateway/vehicle-level sensors (Section 3) rather than
per-camera sensors, the above conflicts mainly affect RCWL + DHT + SD + Camera coexistence on
each camera node. That combination is the one this project must verify first in Phase 3.

## I2C Bus Plan (Gateway) — resolved

SSD1306 (×2) and MPU6050 (×1) are all I2C devices, living on the **ESP32-S3 gateway**,
not on camera nodes.

- SSD1306 #1: `0x3C` — confirmed, shares primary bus (GPIO 8/9) with MPU6050.
- SSD1306 #2: `0x3C` — confirmed same address, no jumper found. Placed on a **second I2C
  bus** (GPIO 10/11) rather than address sharing.
- MPU6050 (HW-123): `0x68` default (AD0 low), on the primary bus (GPIO 8/9) with SSD1306 #1.

Pin assignments are proposed from the gateway's silkscreen but not yet bench-verified —
see `docs/wiring/ESP32_S3_GATEWAY.md`.

## Open Questions Before Finalizing Wiring

**Resolved this update:** exact ESP32-S3 board (ESP32-S3-N16R8), RCWL-0516 pinout,
NEO-6M module identity, MPU6050 breakout identity, SSD1306 dual-address conflict.

**Also resolved:** unit counts — dozens of RCWL-0516 (not all deployed at once), 1×
NEO-6M, 1× MPU6050, 2× DHT11. Bench power source — USB powerbank (see
`docs/wiring/POWER.md`).

**No strong preference (decide ad hoc, non-blocking):**

- Which two nodes get the 2 DHT11 units, and which camera nodes get an RCWL-0516 — any
  assignment works; decide at Phase 6 node naming, or even physically on the bench as
  units are wired up.
- Which of the gateway's two USB-C ports is used for programming — either works; whichever
  is confirmed as the USB-serial bridge during the Section 11 bench test in
  `docs/wiring/ESP32_S3_GATEWAY.md` becomes the one used going forward.

**Still open (needs a physical bench test, not a preference call):**

1. Which free ESP32-CAM GPIOs will host RCWL OUT and DHT DATA (Phase 3, requires
   physical continuity testing against the specific AI-Thinker unit, not assumption from
   the public reference pinout).

Wiring docs now contain confirmed pinouts for RCWL-0516, NEO-6M, MPU6050, and the
gateway board, plus proposed (untested) gateway-side GPIO assignments. The AI-Thinker
ESP32-CAM pin table remains a well-established public reference, still to be
cross-checked against the physical unit before soldering.
