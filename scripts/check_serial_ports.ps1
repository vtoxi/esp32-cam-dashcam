$ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Description -notmatch 'Bluetooth'
    } |
    Where-Object {
        $_.Description -match 'USB Serial|CH340|CP210|FTDI|Silicon Labs|ESP32'
    }

if (-not $ports) {
    Write-Host "COUNT 0"
    Write-Host "No USB serial devices detected."
    exit 0
}

Write-Host ("COUNT {0}" -f $ports.Count)
Write-Host "PORT   | DESCRIPTION"
Write-Host "-------|-----------------------------------------------------------"

foreach ($p in $ports) {
    Write-Host ("{0,-6} | {1}" -f $p.DeviceID, $p.Description)
    if ($p.PNPDeviceID) {
        Write-Host ("       | PNP: {0}" -f $p.PNPDeviceID)
    }
}
