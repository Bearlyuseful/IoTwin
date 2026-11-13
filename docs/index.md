# IoTwin

**IoTwin** is a modular open-source platform for **digital twin simulation**, **visualization**, and **IoT system integration**.  
Its goal is to bridge **physical embedded systems** and **virtual simulations** within a unified environment suitable for research, testing, and real-world development.

---

## 💡 Overview

IoTwin consists of two main components:

1. **IoTwin Core (Bus + Simulators)**  
   - Written in C11 with a small platform layer (Linux with PREEMPT_RT as real-time target, Windows best effort)  
   - `iotwin-bus` is a virtual CAN bus over TCP, `can_sim` a simulated ECU  
   - Built using CMake, tested with Catch2  

2. **IoTwin Visualizer (Unreal Engine Project)**  
   - Provides real-time visualization of simulated devices and environments  
   - Supports interactive scenarios and rule-based simulations  
   - Useful for training, validation, and system analysis  

---

## 🧭 Goals

- Provide a **flexible development and testing environment** for embedded and IoT systems  
- Enable **digital twin modeling** for realistic device behavior  
- Ensure **extensibility**, allowing new modules, devices, and visualizations to be added easily  

---

## 🚀 Getting Started

- **Just run the demo:** see the [Quickstart](user/quickstart.md).
- **Develop:** follow the [Build Instructions](dev/build_instructions.md), then read the
  [Architecture Overview](dev/architecture_overview.md) and the [Bus Protocol](dev/protocol.md).

---

## 📖 Further Documentation

- [Build Instructions](dev/build_instructions.md)
- [Real-Time Operation](dev/realtime.md)
- [Visualizer](dev/visualizer.md)
- [Packaging](dev/packaging.md)
- [Roadmap](roadmap.md)

---
