@echo off
py -3 "%~dp0deploy_device.py" %*
if errorlevel 1 (
    echo.
    echo pyserial is missing. Install it with:
    echo   py -m pip install pyserial
)
