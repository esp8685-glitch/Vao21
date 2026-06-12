<#
PowerShell helper for flashing the ESP32-S3 firmware and LittleFS filesystem.
Usage examples:
  .\flash_windows.ps1 -Port COM3 -Firmware firmware.bin -Bootloader bootloader.bin -Partitions partitions.bin
  .\flash_windows.ps1 -Port COM3 -LittleFS littlefs.bin -FlashFs
  .\flash_windows.ps1 -Port COM3 -Firmware firmware.bin -Bootloader bootloader.bin -Partitions partitions.bin -LittleFS littlefs.bin -FlashFs
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$Bootloader = "bootloader.bin",
    [string]$Partitions = "partitions.bin",
    [string]$Firmware = "firmware.bin",
    [string]$LittleFS = "littlefs.bin",

    [switch]$FlashFs,
    [switch]$FlashAll
)

function Ensure-Esptool {
    Write-Host "Checking esptool availability..."
    $command = "python -m esptool --help"
    try {
        Invoke-Expression $command | Out-Null
    } catch {
        Write-Host "esptool is not installed or not available in PATH. Install it with:`n  python -m pip install --user esptool" -ForegroundColor Yellow
        exit 1
    }
}

function Flash-Image($offset, $imagePath) {
    if (-Not (Test-Path $imagePath)) {
        Write-Host "Missing file: $imagePath" -ForegroundColor Red
        exit 1
    }
    $trimmedPort = $Port.Trim()
    Write-Host "Flashing $imagePath at offset $offset to $trimmedPort..."
    $command = "python -m esptool --chip esp32s3 --port $trimmedPort --baud 921600 write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect $offset $imagePath"
    Write-Host "Command: $command"
    Invoke-Expression $command
}

Ensure-Esptool

if ($FlashAll -or (-not $FlashFs)) {
    Write-Host "Flashing firmware bundle..."
    Flash-Image 0x1000 $Bootloader
    Flash-Image 0x8000 $Partitions
    Flash-Image 0x10000 $Firmware
}

if ($FlashFs -or $FlashAll) {
    Write-Host "Flashing LittleFS filesystem..."
    Flash-Image 0x310000 $LittleFS
}

Write-Host "Done."
