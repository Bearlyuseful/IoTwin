# ADR 0005: Repository Layout - One Repository, Flat Top-Level Areas

**Date:** 2026-11-13
**Status:** Accepted

**Context:**
The repository grew from a C simulator to bus server, bridges, tools, firmware,
an Unreal project and engineering data. Code lived three levels deep
(`src/core/can`, `src/communication/tcp`, `src/ue/IoTwin`, ...) and the layout
no longer showed what is a library, what is a program and what is data. The
question was whether to split it into several repositories.

**Decision:**
Keep **one repository** with flat, role-based top-level directories:

| Directory | Content |
|-----------|---------|
| `libs/` | shared C libraries: `can`, `log`, `net`, `serial`, `platform` |
| `apps/` | programs: `bus` (iotwin-bus), `can_sim`, `serial_bridge`, `socketcan_bridge`, `can_monitor` |
| `firmware/` | NUCLEO-G0B1RE gateway (own CMake project and toolchain) |
| `visualizer/` | Unreal Engine project |
| `data/` | engineering data: DBC files, component database, system descriptions |
| `tests/`, `tools/`, `docs/` | tests, scripts, documentation (incl. `docs/Doxyfile`) |

**Why not several repositories:**

- `libs/can` is compiled into the firmware, the bridges, the tools and the tests;
  protocol changes touch all of them and must land in one change.
- One CI run proves that host programs, firmware and tests fit together.
- A single maintainer pays the cost of versioning shared code across
  repositories without getting its benefits.

**Candidate for a later split:** `visualizer/` has its own toolchain (Unreal,
MSVC), large binary assets (Git LFS) and its own release rhythm. Once the bus
protocol is stable it can move into its own repository; it only depends on the
documented TCP protocol and the files in `data/`.

**Consequences:**

- Paths are shorter and the role of each directory is obvious.
- The Unreal project moved from `src/ue/IoTwin` to `visualizer/`; local build
  folders (`Binaries`, `Intermediate`, `Saved`) are not tracked and are
  recreated on the first build.
