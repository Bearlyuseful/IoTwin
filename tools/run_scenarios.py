#!/usr/bin/env python3
"""Runs scenario scripts against a fresh iotwin-bus and can_sim (one pair per scenario).

    python tools/run_scenarios.py --bin build/gcc-debug/bin data/scenarios/light-demo

Each scenario gets its own bus on a free port and a newly started light ECU, so
injected faults, DTCs or settings never leak into the next scenario. Exit code 0
when every scenario passed. Used as CTest "scenarios".
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

EXE = ".exe" if os.name == "nt" else ""


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def bus_nodes(port: int) -> int:
    """Number of nodes on the bus (not counting this query), -1 if not reachable."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1) as conn:
            conn.sendall(b"!status\n")
            conn.settimeout(1)
            data = b""
            while b"\n" not in data:
                chunk = conn.recv(256)
                if not chunk:
                    return -1
                data += chunk
            for field in data.decode(errors="replace").split():
                if field.startswith("nodes="):
                    return int(field[6:]) - 1
    except OSError:
        return -1
    return -1


def wait_for(predicate, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return False


def stop(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def run_one(bin_dir: Path, scenario: Path, verbose: bool) -> bool:
    port = free_port()
    bus = subprocess.Popen([str(bin_dir / f"iotwin-bus{EXE}"), "--port", str(port)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    sim = None
    try:
        if not wait_for(lambda: bus_nodes(port) >= 0, 5):
            print(f"FAIL {scenario.name}: iotwin-bus did not start")
            return False
        sim = subprocess.Popen([str(bin_dir / f"can_sim{EXE}"), "--port", str(port), "--no-console"],
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if not wait_for(lambda: bus_nodes(port) >= 1, 5):
            print(f"FAIL {scenario.name}: can_sim did not connect")
            return False
        time.sleep(0.2)  # registration for test bench lines

        command = [str(bin_dir / f"can_scenario{EXE}"), "--port", str(port), str(scenario)]
        if verbose:
            command.insert(1, "--verbose")
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        sys.stdout.write(result.stdout.decode(errors="replace"))
        passed = result.returncode == 0
    finally:
        if sim is not None:
            stop(sim)
        stop(bus)
    if not passed and sim is not None and sim.stdout is not None:
        log = sim.stdout.read().decode(errors="replace").strip().splitlines()
        print("     can_sim log (last lines):")
        for line in log[-12:]:
            print(f"       {line}")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bin", required=True, type=Path, help="directory with iotwin-bus, can_sim, can_scenario")
    parser.add_argument("--verbose", action="store_true", help="print every step and frame")
    parser.add_argument("paths", nargs="+", type=Path, help="scenario files or directories")
    args = parser.parse_args()

    scenarios: list[Path] = []
    for path in args.paths:
        scenarios += sorted(path.glob("*.scn")) if path.is_dir() else [path]
    if not scenarios:
        print("no scenarios found")
        return 1

    bin_dir = args.bin.resolve()
    failed = [s.name for s in scenarios if not run_one(bin_dir, s.resolve(), args.verbose)]
    print(f"\n{len(scenarios) - len(failed)} of {len(scenarios)} scenarios passed")
    for name in failed:
        print(f"  failed: {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
