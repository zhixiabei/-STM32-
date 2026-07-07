# STM32F103 USART Bootloader Flash Script
# BOOT0=3.3V, BOOT1=GND, power on before running

param(
    [string]$com = "",
    [string]$hex = ".\Objects\2.hex",
    [int]$baud = 115200
)

if ($com -eq "") {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count -eq 0) {
        Write-Host "No COM port found! Check USB-TTL connection." -ForegroundColor Red
        exit 1
    }
    Write-Host "Available COM ports:" -ForegroundColor Yellow
    $ports | ForEach-Object { Write-Host "  $_" }
    if ($ports.Count -eq 1) {
        $com = $ports[0]
        Write-Host "Auto select: $com" -ForegroundColor Green
    } else {
        $com = Read-Host "Enter COM port (e.g. COM3)"
    }
}

if (-not (Test-Path $hex)) {
    $alt = Join-Path (Split-Path $hex) "2.1.hex"
    if (Test-Path $alt) { $hex = $alt }
    else {
        Write-Host "HEX file not found: $hex" -ForegroundColor Red
        exit 1
    }
}

Write-Host "COM  : $com" -ForegroundColor Green
Write-Host "HEX  : $hex" -ForegroundColor Green

# Parse Intel HEX
$pages = @{}
$extAddr = 0

foreach ($line in Get-Content $hex) {
    if ($line[0] -ne ":") { continue }
    $len  = [Convert]::ToInt32($line.Substring(1, 2), 16)
    $addr = [Convert]::ToInt32($line.Substring(3, 4), 16)
    $type = [Convert]::ToInt32($line.Substring(7, 2), 16)

    if ($type -eq 0) {
        $physAddr = $extAddr + $addr
        $pageAddr = $physAddr -band 0xFFFFF800
        if (-not $pages.ContainsKey($pageAddr)) {
            $pages[$pageAddr] = (0..2047 | ForEach-Object { 0xFF })
        }
        $offset = $physAddr - $pageAddr
        for ($i = 0; $i -lt $len; $i++) {
            $pages[$pageAddr][$offset + $i] = [Convert]::ToInt32($line.Substring(9 + $i * 2, 2), 16)
        }
    }
    elseif ($type -eq 4) {
        $extAddr = [Convert]::ToInt32($line.Substring(9, 4), 16) * 0x10000
    }
    elseif ($type -eq 1) { break }
}

Write-Host "Pages to flash: $($pages.Count)" -ForegroundColor Yellow

# Open serial port (8E1 for STM32 bootloader)
$port = New-Object System.IO.Ports.SerialPort $com, $baud, [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One
$port.ReadTimeout = 3000
$port.WriteTimeout = 2000

try {
    $port.Open()
    Write-Host "Waiting for STM32 bootloader..." -ForegroundColor Yellow

    # Sync
    $port.DiscardInBuffer()
    $port.Write([byte]0x7F)
    try { $ack = $port.ReadByte() } catch { $ack = 0 }
    if ($ack -ne 0x79) {
        Write-Host "No response! Check:" -ForegroundColor Red
        Write-Host "  1. BOOT0 -> 3.3V" -ForegroundColor Red
        Write-Host "  2. BOOT1 -> GND" -ForegroundColor Red
        Write-Host "  3. TX->PA10, RX->PA9" -ForegroundColor Red
        Write-Host "  4. MCU powered on" -ForegroundColor Red
        $port.Close()
        exit 1
    }
    Write-Host "Sync OK" -ForegroundColor Green

    # Get version
    $port.Write([byte[]]@(0x00, 0xFF))
    Start-Sleep -Milliseconds 50
    $ack  = $port.ReadByte()
    $ver  = $port.ReadByte()
    $cmd1 = $port.ReadByte()
    $cmd2 = $port.ReadByte()
    $port.ReadByte() | Out-Null
    Write-Host "Bootloader v$($ver.ToString('X2'))" -ForegroundColor Green

    # Erase all
    Write-Host "Erasing..." -ForegroundColor Yellow
    $port.Write([byte[]]@(0x43, 0xBC))
    Start-Sleep -Milliseconds 50
    $ack = $port.ReadByte()
    if ($ack -ne 0x79) {
        Write-Host "Erase command failed" -ForegroundColor Red
        $port.Close()
        exit 1
    }
    $xor = 0xFF -bxor 0xFF
    $port.Write([byte[]]@(0xFF, 0xFF, $xor))
    Start-Sleep -Milliseconds 500
    $ack = $port.ReadByte()
    if ($ack -ne 0x79) {
        Write-Host "Global erase failed" -ForegroundColor Red
        $port.Close()
        exit 1
    }
    Write-Host "Erase OK" -ForegroundColor Green

    # Flash pages
    $count = 0
    foreach ($pageAddr in ($pages.Keys | Sort-Object)) {
        $data = $pages[$pageAddr]
        $base = $pageAddr
        $n = $data.Count

        # Write memory command
        $port.Write([byte[]]@(0x31, 0xCE))
        Start-Sleep -Milliseconds 10
        $ack = $port.ReadByte()
        if ($ack -ne 0x79) {
            Write-Host "Write cmd failed @ 0x$($base.ToString('X8'))" -ForegroundColor Red
            $port.Close()
            exit 1
        }

        # Address (4 bytes, MSB first) + xor checksum
        $a = @(
            ($base -shr 24) -band 0xFF,
            ($base -shr 16) -band 0xFF,
            ($base -shr 8) -band 0xFF,
            $base -band 0xFF
        )
        $port.Write([byte[]]($a + @($a[0] -bxor $a[1] -bxor $a[2] -bxor $a[3])))
        Start-Sleep -Milliseconds 10
        $ack = $port.ReadByte()
        if ($ack -ne 0x79) {
            Write-Host "Addr error @ 0x$($base.ToString('X8'))" -ForegroundColor Red
            $port.Close()
            exit 1
        }

        # N-1 + data + checksum
        $lenByte = $n - 1
        $chk = $lenByte
        $port.Write([byte]$lenByte)
        for ($i = 0; $i -lt $n; $i++) {
            $port.Write([byte]$data[$i])
            $chk = $chk -bxor $data[$i]
        }
        $port.Write([byte]$chk)
        Start-Sleep -Milliseconds 20
        $ack = $port.ReadByte()
        if ($ack -ne 0x79) {
            Write-Host "Data write failed @ 0x$($base.ToString('X8'))" -ForegroundColor Red
            $port.Close()
            exit 1
        }

        $count++
        Write-Host "`rFlash: $count / $($pages.Count)" -NoNewline
    }
    Write-Host ""
    Write-Host "Flash done!" -ForegroundColor Green

    # Go
    Write-Host "Starting app..." -ForegroundColor Yellow
    $port.Write([byte[]]@(0x21, 0xDE))
    Start-Sleep -Milliseconds 50
    $ack = $port.ReadByte()
    if ($ack -eq 0x79) {
        Write-Host "App started!" -ForegroundColor Green
    }

}
finally {
    $port.Close()
    Write-Host "Set BOOT0 to GND, reset to run." -ForegroundColor Yellow
}
