@echo off
py -3 "%~dp0check_serial_ports.py" %*
if errorlevel 1 (
    echo.
    echo pyserial is missing. Install it with:
    echo   py -m pip install pyserial
)
