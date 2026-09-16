$ErrorActionPreference = 'Stop'

$ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Description -notmatch 'Bluetooth'
    } |
    Where-Object {
        $_.Description -match 'USB Serial|CH340|CP210|FTDI|Silicon Labs|ESP32'
    }

if (-not $ports) {
    Write-Host "No USB serial devices found. Nothing to free."
    exit 0
}

Write-Host "Detected USB serial ports:"
foreach ($p in $ports) {
    Write-Host ("  {0} : {1}" -f $p.DeviceID, $p.Description)
}

$processes = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -match 'python|py|powershell|cmd|putty|screen|minicom|picocom|esptool|platformio|arduino'
}

$killed = @()
foreach ($p in $ports) {
    $device = $p.DeviceID
    foreach ($proc in $processes) {
        $cmd = $proc.CommandLine
        if (-not $cmd) { continue }
        if ($cmd -match [regex]::Escape($device)) {
            Write-Host ("Stopping process holding {0}: {1} (PID {2})" -f $device, $proc.Name, $proc.ProcessId)
            Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
            $killed += [PSCustomObject]@{ Device = $device; PID = $proc.ProcessId; Name = $proc.Name }
        }
    }
}

if (-not $killed) {
    Write-Host "No processes were found holding these USB serial ports."
    exit 0
}

Write-Host "Killed processes:"
$killed | Format-Table -AutoSize
