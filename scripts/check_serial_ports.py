#!/usr/bin/env python3
"""Quick serial-port inventory for the CarSentinel ESP32 setup.

Usage:
    py check_serial_ports.py
    python check_serial_ports.py
"""

from __future__ import annotations

import sys

try:
    import serial.tools.list_ports as list_ports
except Exception:
    print("pyserial is not installed.")
    print("Install it with:")
    print("  py -m pip install pyserial")
    sys.exit(1)


def is_usb_serial(port) -> bool:
    description = (port.description or "").lower()
    hwid = (port.hwid or "").lower()
    name = (port.name or "").lower()
    combined = f"{description} {hwid} {name}"
    if "bluetooth" in combined:
        return False
    return any(token in combined for token in ["esp32", "usb serial", "ch340", "cp210", "ftdi", "silabs"])


def main() -> int:
    ports = [p for p in list_ports.comports() if is_usb_serial(p)]
    print(f"COUNT {len(ports)}")
    if not ports:
        print("No USB serial devices detected.")
        return 0

    print("PORT    | DEVICE       | DESCRIPTION")
    print("--------|--------------|-----------------------------------------------")
    for port in ports:
        print(f"{port.device:<6} | {port.name or '-':<12} | {port.description or '-'}")
        if port.hwid:
            print(f"        |              | HWID: {port.hwid}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
