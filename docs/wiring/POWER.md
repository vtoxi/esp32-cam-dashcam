# Power Architecture

**Status: Prototype design documented; vehicle integration explicitly deferred.**

## 1. Module Description

Power distribution for CarSentinel nodes during bench/prototype development. Per
Section 42 of the project spec, the initial prototype uses an independent regulated
power supply — **no direct connection to Peugeot 2008 ECU/CAN wiring** at this stage.

**Confirmed bench power source: a USB powerbank**, feeding nodes via:
- USB-A → micro-USB cables (for boards with a micro-USB power input)
- USB-A → USB-C cables (for the ESP32-S3-N16R8 gateway's USB-C ports)
- Direct soldered wire connections (for sensor breakouts with no USB connector — RCWL,
  DHT11, NEO-6M, MPU6050 — powered from a node's `5V`/`3V3` and `GND` pins per their
  individual wiring docs, not from the powerbank directly)

The AI-Thinker ESP32-CAM itself has no onboard USB connector at all — it is powered via
its 5V/GND header pins, typically from the FTDI programmer's 5V rail during bench testing
or from a dedicated 5V feed from the powerbank via a micro-USB breakout/adapter cable
into those header pins (not a native micro-USB port on the ESP32-CAM board itself).

## 2. Voltage Requirements

- ESP32-CAM: 5V input (board regulator steps down to 3.3V) — confirm the specific
  board's power pin labeling before connecting (see `docs/wiring/ESP32_CAM.md` Section
  2).
- ESP32-S3 gateway: typically 5V via USB-C or VIN — confirm against the actual board once
  identified.
- Sensors (RCWL, DHT, GPS, IMU, OLED): 3.3V–5V per their individual onboard regulators —
  see each module's wiring doc.

## 3. Pin Table

Not applicable at the system level — see per-module wiring docs.

## 4. ESP32 GPIO Connection

N/A.

## 5. Power Connection (Prototype Phase)

- **Confirmed source: USB powerbank**, one 5V rail split across multiple output
  cables/ports (most powerbanks provide independent regulated 5V per port, but verify the
  specific unit's per-port current rating — see Section 13).
- ESP32-S3 gateway: powerbank → USB-A-to-USB-C cable, into either USB-C port (confirm
  which is the programming port per `docs/wiring/ESP32_S3_GATEWAY.md` Section 11 before
  relying on that port for flashing while also powered here).
- ESP32-CAM node(s): powerbank → USB-A-to-micro-USB cable → micro-USB breakout/adapter →
  soldered/header connection to the board's `5V`/`GND` pins (no native USB port on
  AI-Thinker ESP32-CAM).
- Sensor breakouts (RCWL, DHT11, NEO-6M, MPU6050): soldered wire connections from the
  host board's `5V`/`3V3` and `GND` pins, not directly from the powerbank — see each
  module's own wiring doc for exact pins.
- Do not power a node from both USB and an external 5V supply simultaneously without a
  diode-OR or similar isolation circuit — can create a damaging voltage conflict. When
  flashing a node via FTDI/USB while it's also connected to the powerbank, disconnect one
  source first.

## 6. Ground Connection

All nodes sharing a bench supply must share a common ground; nodes on separate supplies
communicating over ESP-NOW/Wi-Fi do not need a shared ground (RF-only link).

## 7. Communication Interface

N/A — this document covers power only.

## 8. Pull-ups / Pull-downs

N/A.

## 9. Known Conflicts

None at the prototype-power level; future vehicle-power integration (ignition sensing,
battery voltage monitoring) is designed for in software (`IgnitionState`,
`BatteryVoltage`, `PowerState` interfaces — Section 42) but not implemented until Phase
18, after bench testing, and only with a properly engineered electrical isolation design.

## 10. Safety Warnings

- Do not connect any node directly to vehicle 12V systems, ECU, or CAN bus without a
  purpose-designed, tested isolation/regulation circuit. This is explicitly out of scope
  until Phase 18.
- Verify polarity before connecting any bench supply.

## 11. Testing Procedure

1. Power each node independently from the powerbank, confirm stable boot (no brownout
   resets — ESP32-CAM camera init in particular can brown out on marginal supplies; check
   the powerbank's per-port current rating supports at least 500mA per camera node,
   more if RCWL/DHT are also drawing from the same node).
2. Confirm no node requires the gateway or another node to be powered in order to
   function (validates the node-independence requirement, Section 5).
3. If running multiple nodes off the same powerbank simultaneously, confirm the
   powerbank's total output capacity isn't exceeded (sum of all connected nodes' peak
   draw, camera capture being the highest draw event per node).

## 12. Example Configuration

N/A — hardware-only document.

## 13. Board-Specific Differences

ESP32-CAM camera modules are known to be sensitive to under-powered/high-impedance USB
sources causing brownout during camera capture; a dedicated bench supply or
high-quality USB power source is recommended over a weak USB hub port. Some powerbanks
implement aggressive low-current auto-shutoff (designed for phone charging, not a
steady low-current load like an idle ESP32 node) — if a node unexpectedly loses power
during idle/low-draw periods, this is the likely cause; verify the specific powerbank
model's idle-load behavior.
