<#
.SYNOPSIS
    Development run: starts iotwin-bus and can_sim from the build folder and,
    optionally, the visualizer from the Unreal editor binaries (-game).

.EXAMPLE
    ./tools/run-dev.ps1                       # core only, open the .uproject and press Play
    ./tools/run-dev.ps1 -SerialPort COM5      # plus the NUCLEO-G0B1RE gateway
    ./tools/run-dev.ps1 -Visualizer -EngineDir "F:\Unreal\UE_5.6\Engine"
#>
[CmdletBinding()]
param(
    [string]$Preset = "gcc-debug",
    [int]$Port = 8080,
    [switch]$Visualizer,
    [string]$EngineDir = $env:UE_ENGINE_DIR,
    # Serial port of a NUCLEO-G0B1RE with the gateway firmware, e.g. COM5.
    [string]$SerialPort,
    [ValidateSet("loopback", "normal", "listen")]
    [string]$CanMode = "loopback"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $RepoRoot "build\$Preset\bin"
if (-not (Test-Path (Join-Path $Bin "iotwin-bus.exe"))) {
    throw "Core not built. Run: cmake --preset $Preset; cmake --build --preset $Preset"
}

Start-Process -FilePath (Join-Path $Bin "iotwin-bus.exe") -ArgumentList "--port", $Port, "--log-frames"
Start-Sleep -Milliseconds 500
Start-Process -FilePath (Join-Path $Bin "can_sim.exe") -ArgumentList "--port", $Port
if ($SerialPort) {
    Start-Process -FilePath (Join-Path $Bin "serial_bridge.exe") -ArgumentList "--serial", $SerialPort, "--port", $Port, "--mode", $CanMode, "--stats", "30"
}

if ($Visualizer) {
    $Editor = Join-Path $EngineDir "Binaries\Win64\UnrealEditor.exe"
    if (-not (Test-Path $Editor)) { throw "UnrealEditor.exe not found, pass -EngineDir <UE_5.6>\Engine" }
    $Project = Join-Path $RepoRoot "src\ue\IoTwin\IoTwin.uproject"
    Start-Process -FilePath $Editor -ArgumentList "`"$Project`"", "-game", "-windowed", "-ResX=1600", "-ResY=900", "-IoTwinBusPort=$Port"
} else {
    Write-Host "Bus and simulator started on port $Port. Open src/ue/IoTwin/IoTwin.uproject and press Play."
}
