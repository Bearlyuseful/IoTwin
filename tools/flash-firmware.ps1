<#
.SYNOPSIS
    Flashes the IoTwin gateway firmware onto a NUCLEO-G0B1RE.

.DESCRIPTION
    1. OpenOCD via the on-board ST-LINK (program, verify, reset).
    2. Fallback: copy the .bin to the ST-LINK mass storage drive (NOD_G0B1RE),
       which needs no debugger driver.

.EXAMPLE
    ./tools/flash-firmware.ps1
    ./tools/flash-firmware.ps1 -MassStorage
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "firmware\build\nucleo-g0b1re"),
    [switch]$MassStorage
)

$ErrorActionPreference = "Stop"
$Elf = Join-Path $BuildDir "iotwin_gateway.elf"
$Bin = Join-Path $BuildDir "iotwin_gateway.bin"
if (-not (Test-Path $Elf)) {
    throw "Firmware not built. Run: cd firmware; cmake --preset nucleo-g0b1re; cmake --build --preset nucleo-g0b1re"
}

if (-not $MassStorage -and (Get-Command openocd -ErrorAction SilentlyContinue)) {
    Write-Host "Flashing with OpenOCD (ST-LINK)..." -ForegroundColor Cyan
    $ElfForOpenOcd = $Elf -replace '\\', '/'
    & openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c "program $ElfForOpenOcd verify reset exit"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Done." -ForegroundColor Green
        exit 0
    }
    Write-Warning "OpenOCD failed (ST-LINK driver missing?), trying the mass storage drive."
}

$Drive = Get-Volume | Where-Object { $_.FileSystemLabel -like "NOD_*" } | Select-Object -First 1
if (-not $Drive) {
    throw "No ST-LINK mass storage drive (NOD_...) found. Is the board connected?"
}
$Target = "$($Drive.DriveLetter):\"
Write-Host "Copying $Bin to $Target ($($Drive.FileSystemLabel))..." -ForegroundColor Cyan
Copy-Item $Bin $Target
Write-Host "Done. The ST-LINK programs the board and resets it (LED blinks while waiting for the host)." -ForegroundColor Green
