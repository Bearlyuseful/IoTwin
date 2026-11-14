#!/usr/bin/env python3
"""Checks the IoTwin engineering data in data/ (Python standard library only).

- every component file: required fields, id format, unique ids, parameter
  units and sources, min <= typ <= max, references to contained components
- every system file: node/component references, bus members, DBC files and
  the message names used in them, link endpoints

Usage: python tools/validate_data.py [data-directory]
Exit code 0 = ok, 1 = errors found.
"""

import json
import re
import sys
from pathlib import Path

ID_PATTERN = re.compile(r"^[a-z0-9]+(\.[a-z0-9][a-z0-9-]*)+$")
NAME_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")
UNITS = {"V", "A", "W", "Hz", "B", "bit/s", "°C", "s", "Ω", "F", "H", "lm", "%", "1", "A²s"}
CATEGORIES = {"microcontroller", "board", "transceiver", "sensor", "actuator", "lamp", "relay", "switch",
              "protection", "appliance",
              "power-supply", "cable", "connector", "ecu", "passive", "other"}


class Report:
    def __init__(self) -> None:
        self.errors = []

    def error(self, path: Path, message: str) -> None:
        self.errors.append(f"{path}: {message}")


def load_json(path: Path, report: Report):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        report.error(path, f"cannot read JSON: {exc}")
        return None


def check_parameter(path: Path, where: str, param: dict, source_ids: set, report: Report) -> None:
    if not isinstance(param, dict):
        report.error(path, f"{where}: must be an object")
        return
    unknown = set(param) - {"value", "min", "typ", "max", "unit", "source", "verified", "note"}
    if unknown:
        report.error(path, f"{where}: unknown fields {sorted(unknown)}")
    if param.get("unit") not in UNITS:
        report.error(path, f"{where}: unit {param.get('unit')!r} not in {sorted(UNITS)}")
    if param.get("source") not in source_ids:
        report.error(path, f"{where}: source {param.get('source')!r} is not listed in sources")
    if not isinstance(param.get("verified"), bool):
        report.error(path, f"{where}: 'verified' must be true or false")
    numbers = {k: param[k] for k in ("value", "min", "typ", "max") if k in param}
    if not numbers:
        report.error(path, f"{where}: needs at least one of value/min/typ/max (null if still unknown)")
    for key, number in numbers.items():
        if number is not None and not isinstance(number, (int, float)):
            report.error(path, f"{where}.{key}: must be a number or null")
    if param.get("verified") and all(v is None for v in numbers.values()):
        report.error(path, f"{where}: verified but no value")
    ordered = [numbers.get(k) for k in ("min", "typ", "max") if numbers.get(k) is not None]
    if ordered != sorted(ordered):
        report.error(path, f"{where}: min <= typ <= max violated")


def check_component(path: Path, data: dict, report: Report) -> None:
    for field in ("id", "name", "category", "sources", "parameters"):
        if field not in data:
            report.error(path, f"missing field '{field}'")
    if not ID_PATTERN.match(str(data.get("id", ""))):
        report.error(path, f"id {data.get('id')!r} must look like vendor.part (lower case)")
    if data.get("category") not in CATEGORIES:
        report.error(path, f"category {data.get('category')!r} unknown")
    source_ids = {s.get("id") for s in data.get("sources", []) if isinstance(s, dict)}
    for name, param in data.get("parameters", {}).items():
        if not NAME_PATTERN.match(name):
            report.error(path, f"parameter name {name!r} must be snake_case")
        check_parameter(path, f"parameters.{name}", param, source_ids, report)
    for index, interface in enumerate(data.get("interfaces", [])):
        if "type" not in interface:
            report.error(path, f"interfaces[{index}]: missing type")
        if "source" in interface and interface["source"] not in source_ids:
            report.error(path, f"interfaces[{index}]: source {interface['source']!r} not listed")
        for name, param in interface.get("parameters", {}).items():
            check_parameter(path, f"interfaces[{index}].parameters.{name}", param, source_ids, report)
    port_ids = [p.get("id") for p in data.get("ports", [])]
    if len(port_ids) != len(set(port_ids)):
        report.error(path, "duplicate port ids")


def check_contains(path: Path, data: dict, components: dict, report: Report) -> None:
    instances = {}
    for entry in data.get("contains", []):
        if entry.get("component") not in components:
            report.error(path, f"contains: unknown component {entry.get('component')!r}")
        else:
            instances[entry.get("instance")] = components[entry["component"]]
    for port in data.get("ports", []):
        target = port.get("connects_to")
        if not target:
            continue
        instance, _, port_id = target.partition(".")
        sub = instances.get(instance)
        if sub is None:
            report.error(path, f"port {port.get('id')}: connects_to {target!r} names no contained instance")
        elif port_id not in {p.get("id") for p in sub.get("ports", [])}:
            report.error(path, f"port {port.get('id')}: {sub['id']} has no port {port_id!r}")


def dbc_messages(path: Path) -> set:
    names = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*BO_\s+\d+\s+([A-Za-z_][A-Za-z0-9_]*)\s*:", line)
        if match:
            names.add(match.group(1))
    return names


def check_system(path: Path, data: dict, components: dict, report: Report) -> None:
    nodes = {}
    for node in data.get("nodes", []):
        node_id = node.get("id")
        if node_id in nodes:
            report.error(path, f"duplicate node id {node_id!r}")
        nodes[node_id] = node
        if node.get("kind") not in ("physical", "virtual"):
            report.error(path, f"node {node_id}: kind must be physical or virtual")
        if "component" in node and node["component"] not in components:
            report.error(path, f"node {node_id}: unknown component {node['component']!r}")

    known_messages = set()
    for bus in data.get("buses", []):
        for member in bus.get("members", []):
            if member not in nodes:
                report.error(path, f"bus {bus.get('id')}: unknown member {member!r}")
        if "dbc" in bus:
            dbc_path = (path.parent / bus["dbc"]).resolve()
            if not dbc_path.exists():
                report.error(path, f"bus {bus.get('id')}: DBC {bus['dbc']} not found")
            else:
                known_messages |= dbc_messages(dbc_path)

    for link in data.get("links", []):
        for end in ("from", "to"):
            node_id = str(link.get(end, "")).split(":", 1)[0]
            if node_id not in nodes:
                report.error(path, f"link {link.get('id')}: unknown node {node_id!r} in '{end}'")
            elif ":" in str(link.get(end)):
                port = link[end].split(":", 1)[1]
                component = components.get(nodes[node_id].get("component", ""))
                if component and port not in {p.get("id") for p in component.get("ports", [])}:
                    report.error(path, f"link {link.get('id')}: {component['id']} has no port {port!r}")

    for message in data.get("messages", []):
        if known_messages and message.get("name") not in known_messages:
            report.error(path, f"message {message.get('name')!r} is not defined in the bus DBC")
        for role in ("producers", "consumers"):
            for node_id in message.get(role, []):
                if node_id not in nodes:
                    report.error(path, f"message {message.get('name')}: unknown {role[:-1]} {node_id!r}")


# Terminals per device type - must match libs/elsim/elsim.c.
TERMINALS = {
    "supply": {"L1", "L2", "L3", "N", "PE"},
    "breaker": {"in", "out"},
    "fuse": {"in", "out"},
    "rcd": {"L_in", "N_in", "L_out", "N_out"},
    "switch": {"in", "out"},
    "load": {"L", "N", "PE"},
    "socket": {"L", "N", "PE"},
    "junction": {"L", "N", "PE"},
    "dimmer": {"in", "out"},
}
# Component categories that fit a device type.
TYPE_CATEGORIES = {
    "breaker": {"protection"},
    "fuse": {"protection"},
    "rcd": {"protection"},
    "switch": {"switch"},
    "load": {"lamp", "appliance", "actuator", "other"},
    "socket": {"connector", "other"},
    "supply": {"power-supply", "other"},
    "junction": {"connector", "other"},
    "dimmer": {"switch", "actuator", "other"},
}
EARTHING = {"TN-S", "TN-C-S", "TT"}


def check_installation(path: Path, data: dict, components: dict, report: Report) -> None:
    for field in ("id", "name", "devices", "connections"):
        if field not in data:
            report.error(path, f"missing field '{field}'")
    cables = {}
    for cable in data.get("cables", []):
        cid = cable.get("id")
        if cid in cables:
            report.error(path, f"duplicate cable id {cid!r}")
        cables[cid] = cable
        if not isinstance(cable.get("cross_section"), (int, float)) or cable["cross_section"] <= 0:
            report.error(path, f"cable {cid!r}: cross_section must be > 0 (mm^2)")
        if not isinstance(cable.get("length"), (int, float)) or cable["length"] < 0:
            report.error(path, f"cable {cid!r}: length must be >= 0 (m)")

    devices = {}
    for device in data.get("devices", []):
        did, dtype = device.get("id"), device.get("type")
        if not did or ":" in did or " " in did:
            report.error(path, f"device id {did!r} must be set and contain no ':' or spaces")
        if did in devices:
            report.error(path, f"duplicate device id {did!r}")
        devices[did] = device
        if dtype not in TERMINALS:
            report.error(path, f"device {did!r}: unknown type {dtype!r} ({', '.join(sorted(TERMINALS))})")
            continue
        component_id = device.get("component")
        if component_id is not None:
            component = components.get(component_id)
            if component is None:
                report.error(path, f"device {did!r}: unknown component {component_id!r}")
            elif component.get("category") not in TYPE_CATEGORIES.get(dtype, set()):
                report.error(path, f"device {did!r}: component {component_id!r} ({component.get('category')}) "
                                   f"does not fit type {dtype}")
        if dtype == "supply" and device.get("system", "TN-S") not in EARTHING:
            report.error(path, f"device {did!r}: system must be one of {sorted(EARTHING)}")
    if not any(d.get("type") == "supply" for d in devices.values()):
        report.error(path, "no supply device")
    for did, device in devices.items():
        socket = device.get("plugged_into")
        if socket is not None:
            if device.get("type") != "load":
                report.error(path, f"device {did!r}: only loads can be plugged in")
            if devices.get(socket, {}).get("type") != "socket":
                report.error(path, f"device {did!r}: plugged_into {socket!r} is not a socket")

    def check_terminal(where: str, reference) -> None:
        device_id, _, terminal = str(reference).partition(":")
        device = devices.get(device_id)
        if device is None:
            report.error(path, f"{where}: unknown device in {reference!r}")
        elif terminal not in TERMINALS.get(device.get("type"), set()):
            report.error(path, f"{where}: {device.get('type')} {device_id!r} has no terminal {terminal!r}")

    connection_ids = set()
    for index, connection in enumerate(data.get("connections", [])):
        where = f"connections[{index}]"
        cid = connection.get("id")
        if cid is not None:
            if cid in connection_ids:
                report.error(path, f"{where}: duplicate id {cid!r}")
            connection_ids.add(cid)
        check_terminal(where, connection.get("from"))
        check_terminal(where, connection.get("to"))
        if connection.get("from") == connection.get("to"):
            report.error(path, f"{where}: connects a terminal to itself")
        if connection.get("role", "") not in ("L", "N", "PE", ""):
            report.error(path, f"{where}: role must be L, N or PE")
        if "cable" in connection and connection["cable"] not in cables:
            report.error(path, f"{where}: unknown cable {connection['cable']!r}")


def main() -> int:
    # Units like "Ω" and "°C" appear in messages; Windows consoles default to cp1252.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    data_dir =Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    report = Report()

    components = {}
    component_files = sorted((data_dir / "components").rglob("*.json"))
    for path in component_files:
        data = load_json(path, report)
        if data is None:
            continue
        check_component(path, data, report)
        if data.get("id") in components:
            report.error(path, f"duplicate component id {data.get('id')!r}")
        components[data.get("id")] = data
    for path in component_files:
        data = load_json(path, report)
        if data is not None:
            check_contains(path, data, components, report)

    system_files = sorted((data_dir / "systems").rglob("*.json"))
    for path in system_files:
        data = load_json(path, report)
        if data is not None:
            check_system(path, data, components, report)

    installation_files = sorted((data_dir / "installations").rglob("*.json"))
    for path in installation_files:
        data = load_json(path, report)
        if data is not None:
            check_installation(path, data, components, report)

    for line in report.errors:
        print(f"ERROR {line}")
    print(f"{len(component_files)} components, {len(system_files)} systems, "
          f"{len(installation_files)} installations, {len(report.errors)} errors")
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
