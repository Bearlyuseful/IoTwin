<#
.SYNOPSIS
    Builds a self-contained IoTwin release for Windows (core + visualizer).

.DESCRIPTION
    1. Builds and tests the C core (iotwin-bus, can_sim) with the gcc-release preset.
    2. Builds, cooks and stages the Unreal visualizer with RunUAT BuildCookRun.
    3. Assembles dist/IoTwin-<version>-win64 with a start script and zips it.

    Requirements: MSYS2 UCRT64 GCC + CMake + Ninja on PATH, Unreal Engine 5.6,
    Visual Studio 2022 (C++ game workload).

.EXAMPLE
    ./tools/package.ps1 -EngineDir "C:\Program Files\Epic Games\UE_5.6\Engine"
#>
[CmdletBinding()]
param(
    # Engine folder of the Unreal installation (contains Build\BatchFiles\RunUAT.bat).
    [string]$EngineDir = $env:UE_ENGINE_DIR,
    [string]$Version = "0.2.0",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    # Reuse an existing cooked visualizer from a previous run.
    [switch]$SkipVisualizer,
    # Keep the .pdb debug symbols of the visualizer (adds ~350 MB).
    [switch]$IncludeDebugInfo,
    [switch]$SkipTests,
    # Do not build the NUCLEO-G0B1RE firmware (needs arm-none-eabi-gcc).
    [switch]$SkipFirmware,
    # Use the existing editor binaries for cooking (needed while an editor with
    # Live Coding is running, which blocks rebuilding the editor module).
    [switch]$SkipEditorBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $RepoRoot "visualizer\IoTwin.uproject"
$DistRoot = Join-Path $RepoRoot "dist"
$UeStaging = Join-Path $DistRoot "ue-staging"
$PackageName = "IoTwin-$Version-win64"
$PackageDir = Join-Path $DistRoot $PackageName

function Step($Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

function Invoke-Checked {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "'$File $($Arguments -join ' ')' failed with exit code $LASTEXITCODE" }
}

# --- 1. Core ---------------------------------------------------------------
Step "Building the core (gcc-release)"
Push-Location $RepoRoot
try {
    Invoke-Checked cmake @("--preset", "gcc-release")
    Invoke-Checked cmake @("--build", "--preset", "gcc-release")
    if (-not $SkipTests) {
        Step "Testing the core"
        Invoke-Checked ctest @("--preset", "gcc-release")
    }
} finally {
    Pop-Location
}
$CoreBin = Join-Path $RepoRoot "build\gcc-release\bin"

# --- 1b. Firmware ------------------------------------------------------------
$FirmwareBuild = Join-Path $RepoRoot "firmware\build\nucleo-g0b1re"
if (-not $SkipFirmware) {
    if (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue) {
        Step "Building the NUCLEO-G0B1RE gateway firmware"
        Push-Location (Join-Path $RepoRoot "firmware")
        try {
            Invoke-Checked cmake @("--preset", "nucleo-g0b1re")
            Invoke-Checked cmake @("--build", "--preset", "nucleo-g0b1re")
        } finally {
            Pop-Location
        }
    } else {
        Write-Warning "arm-none-eabi-gcc not found - the package will not contain the firmware."
        $SkipFirmware = $true
    }
}

# --- 2. Visualizer -----------------------------------------------------------
if (-not $SkipVisualizer) {
    if (-not $EngineDir -or -not (Test-Path (Join-Path $EngineDir "Build\BatchFiles\RunUAT.bat"))) {
        throw "Unreal Engine not found. Pass -EngineDir <UE_5.6>\Engine or set UE_ENGINE_DIR."
    }
    Step "Building, cooking and staging the visualizer ($Configuration) - this takes a while"
    $RunUAT = Join-Path $EngineDir "Build\BatchFiles\RunUAT.bat"
    $UatArgs = @(
        "BuildCookRun", "-project=$Project", "-noP4", "-unattended", "-utf8output",
        "-platform=Win64", "-clientconfig=$Configuration",
        "-build", "-cook", "-stage", "-pak", "-prereqs",
        "-archive", "-archivedirectory=$UeStaging"
    )
    if (-not $IncludeDebugInfo) { $UatArgs += "-nodebuginfo" }
    if ($SkipEditorBuild) { $UatArgs += "-nocompileeditor" }
    if (Test-Path $UeStaging) { Remove-Item $UeStaging -Recurse -Force }
    Invoke-Checked $RunUAT $UatArgs
}
$VisualizerStage = Join-Path $UeStaging "Windows"
if (-not (Test-Path (Join-Path $VisualizerStage "IoTwin.exe"))) {
    throw "Staged visualizer not found in $VisualizerStage"
}

# --- 3. Assemble -------------------------------------------------------------
Step "Assembling $PackageName"
if (Test-Path $PackageDir) { Remove-Item $PackageDir -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $PackageDir "bin") | Out-Null
Copy-Item (Join-Path $CoreBin "iotwin-bus.exe"), (Join-Path $CoreBin "can_sim.exe"), (Join-Path $CoreBin "serial_bridge.exe"), (Join-Path $CoreBin "can_monitor.exe") (Join-Path $PackageDir "bin")
$ConfigTarget = New-Item -ItemType Directory -Force (Join-Path $PackageDir "config")
Copy-Item (Join-Path $RepoRoot "data\dbc\iotwin.dbc") $ConfigTarget
Copy-Item $VisualizerStage (Join-Path $PackageDir "Visualizer") -Recurse
Copy-Item (Join-Path $PSScriptRoot "release\start-demo.bat"), (Join-Path $PSScriptRoot "release\start-demo-hardware.bat") $PackageDir
if (-not $SkipFirmware) {
    $FirmwareTarget = New-Item -ItemType Directory -Force (Join-Path $PackageDir "firmware")
    Copy-Item (Join-Path $FirmwareBuild "iotwin_gateway.bin"), (Join-Path $FirmwareBuild "iotwin_gateway.hex") $FirmwareTarget
}
Copy-Item (Join-Path $PSScriptRoot "release\README.txt") $PackageDir
Copy-Item (Join-Path $RepoRoot "LICENSE") $PackageDir

$Zip = Join-Path $DistRoot "$PackageName.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path $PackageDir -DestinationPath $Zip

$SizeMb = [math]::Round((Get-Item $Zip).Length / 1MB, 1)
Step "Done: $Zip ($SizeMb MB)"
