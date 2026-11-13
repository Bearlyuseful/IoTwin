#!/usr/bin/env bash
# Starts iotwin-bus and can_sim on Linux (e.g. a PREEMPT_RT machine).
#
#   tools/run-core.sh                    # local demo on 127.0.0.1:8080
#   BIND=0.0.0.0 tools/run-core.sh       # accept the visualizer from another machine
#   RT=1 tools/run-core.sh               # real-time settings (needs privileges, see docs/dev/realtime.md)
#
# The visualizer on Windows then connects with -IoTwinBusHost=<linux-ip>.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRESET="${PRESET:-gcc-release}"
BIN="$ROOT/build/$PRESET/bin"
PORT="${PORT:-8080}"
BIND="${BIND:-127.0.0.1}"

if [[ ! -x "$BIN/iotwin-bus" ]]; then
  echo "Core not built. Run: cmake --preset $PRESET && cmake --build --preset $PRESET" >&2
  exit 1
fi

BUS_RT=()
SIM_RT=()
if [[ "${RT:-0}" == "1" ]]; then
  BUS_RT=(--rt-priority 70 --rt-lock-memory)
  SIM_RT=(--rt-priority 80 --rt-lock-memory --tick-ms 1 --stats 5)
fi

"$BIN/iotwin-bus" --bind "$BIND" --port "$PORT" --log-frames "${BUS_RT[@]}" &
BUS_PID=$!
trap 'kill $BUS_PID 2>/dev/null || true' EXIT
sleep 0.5

"$BIN/can_sim" --port "$PORT" "${SIM_RT[@]}"
