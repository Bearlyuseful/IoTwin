@echo off
rem IoTwin light demo with a NUCLEO-G0B1RE as real CAN node.
rem Usage: start-demo-hardware.bat COM5 [loopback|normal]
setlocal
cd /d "%~dp0"

if "%~1"=="" (
  echo Usage: %~nx0 COMx [loopback^|normal]
  echo.
  echo The COM port is listed in the Device Manager as "STMicroelectronics STLink Virtual COM Port".
  echo Flash firmware\iotwin_gateway.bin first: copy it to the NOD_G0B1RE drive.
  exit /b 1
)
set MODE=%~2
if "%MODE%"=="" set MODE=loopback

start "IoTwin Bus" bin\iotwin-bus.exe --log-frames
ping -n 2 127.0.0.1 >nul
start "IoTwin CAN-Sim (light ECU)" bin\can_sim.exe
start "IoTwin Serial Bridge (%~1)" bin\serial_bridge.exe --serial %~1 --mode %MODE% --stats 30
start "" Visualizer\IoTwin.exe -windowed -ResX=1600 -ResY=900

echo.
echo IoTwin started with the NUCLEO-G0B1RE on %~1 (%MODE% mode):
echo   - blue user button B1: wall switch, the lamp in the visualizer follows
echo   - green LED LD4:       shows the lamp command of the ECU
