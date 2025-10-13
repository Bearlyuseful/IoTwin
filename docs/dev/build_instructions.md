# Build Instructions

This document describes how to set up, build, and run the IoTwin project on **Linux**, **Windows**, and **macOS**.

---

## 📦 Prerequisites

### General Requirements

| Tool / Library | Minimum Version | Notes |
|----------------|-----------------|-------|
| **CMake** | 3.20+ | Required for building the C++ components |
| **Git** | — | To clone the repository |
| **C++ Compiler** | C++20-compatible | GCC ≥ 11, Clang ≥ 13, or MSVC 2022 |
| **Python 3** | 3.8+ | Used for helper scripts |

---

## 🧱 Building the IoTwin Core (C++)

### 1. Clone the repository

```bash
git clone https://github.com/BearlyUseful/IoTwin.git
cd IoTwin
```
### 2. Create a build directory

```bash
mkdir build
cd build
```
### 3. Configure with CMake
Linux/macOS

```bash 
cmake .. -DCMAKE_BUILD_TYPE=elease
```

Windows
```bash
cmake .. -G
```

### 4. Build

```bash 
cmake --build . --config Release
```

### 5. Run 
The built binaries will be located in:
```bash
build/bin/
```

## 🎮 Setting up the IoTwin Visualizer (Unreal Engine)
### 1. Install Unreal Engine

IoTwin requires Unreal Engine 5.3 or later.

Linux/macOS: Install via Epic Games Launcher
Windows: Install via Epic Games Launcher.

If building from source, follow the Unreal Engine GitHub setup guide

### 2. Open the Unreal Project

In the repository:
```bash
src/ue/IoTwin/IoTwin.uproject
```
Open this file in Unreal Engine. On first open, Unreal will prompt to rebuild missing modules — confirm with Yes.

### 3. Configure the Visualizer

The Unreal project communicates with the IoTwin Core via local networking interfaces (default: TCP localhost).
Ensure the backend is running before starting the simulation in the editor.
