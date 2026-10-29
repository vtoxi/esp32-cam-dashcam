#!/usr/bin/env python3
"""Interactive PlatformIO deploy helper for the CarSentinel firmware.

This script lists USB serial ports, asks which board to deploy (gateway or node),
then asks for the target COM port and uploads the selected firmware.

Examples:
    py scripts\deploy_device.py
    .\scripts\deploy_device.bat
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

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


def list_usb_ports() -> list:
    return [p for p in list_ports.comports() if is_usb_serial(p)]


def normalize_env(value: str) -> str | None:
    v = value.strip().lower()
    if v in {"gateway", "g"}:
        return "gateway"
    if v in {"node", "n"}:
        return "node"
    return None


def prompt_choice(label: str, valid: list[str]) -> str:
    while True:
        value = input(f"{label}: ").strip()
        if value.lower() in valid:
            return value.lower()
        if value.isdigit() and value in {str(i + 1) for i in range(len(valid))}:
            return valid[int(value) - 1]
        print(f"Invalid choice. Pick one of: {', '.join(valid)}")


def prompt_port(ports: list) -> str:
    if not ports:
        raise RuntimeError("No USB serial devices detected.")

    print("\nAvailable USB serial ports:")
    for idx, port in enumerate(ports, start=1):
        label = port.description or port.name or "USB serial"
        print(f"  {idx}. {port.device}  -  {label}")

    while True:
        choice = input("Select a port by number or type a COM port manually (example: COM6): ").strip()
        if not choice:
            print("Port cannot be empty.")
            continue

        try:
            idx = int(choice)
            if 1 <= idx <= len(ports):
                return ports[idx - 1].device
        except ValueError:
            pass

        if choice.upper().startswith("COM"):
            return choice.upper()

        print("Please enter a valid port number or a COM port like COM6.")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    firmware_dir = repo_root / "firmware"

    if not firmware_dir.exists():
        print(f"Firmware directory not found: {firmware_dir}")
        return 1

    ports = list_usb_ports()
    if not ports:
        print("No USB serial devices detected. Connect the ESP32 device and try again.")
        return 1

    print("CarSentinel deployment helper")
    print("1 = gateway")
    print("2 = node")

    more = True
    while more:
        target = prompt_choice("Select device type", ["1", "2"])
        env = "gateway" if target == "1" else "node"
        port = prompt_port(ports)

        command = [
            sys.executable,
            "-m",
            "platformio",
            "run",
            "-e",
            env,
            "-t",
            "upload",
            "--upload-port",
            port,
        ]

        print(f"\nCompiling and uploading {env} firmware to {port}...")
        print("Command:", " ".join(command))

        result = subprocess.run(command, cwd=str(firmware_dir))
        if result.returncode == 0:
            print(f"\nUpload succeeded for {env} on {port}.")
        else:
            print(f"\nUpload failed for {env} on {port}.")
            print("The script will continue to the next job unless you exit.")

        while True:
            again = input("Deploy more devices? [y/n]: ").strip().lower()
            if again in {"y", "yes"}:
                break
            if again in {"n", "no"}:
                more = False
                break
            print("Please enter y or n.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
