<p align="center">
  <img src="wobblin_icon.png" width="120" alt="Wobblin Logo">
</p>

<h1 align="center">Wobblin</h1>

<p align="center">
  <strong>Physics-based Wobbly Windows & Burn animations for modern Windows desktops.</strong><br>
  <sub>Inspired by the legendary Compiz Fusion & KWin Wobbly Windows effect.</sub>
</p>

<p align="center">
  <a href="https://github.com/aziqi/Wobblin-Windows/releases">
    <img src="https://img.shields.io/github/v/release/aziqi/Wobblin-Windows?color=7c3aed&style=for-the-badge&logo=github&label=Release" alt="Release">
  </a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Windows 10/11">
  <img src="https://img.shields.io/badge/Status-Beta-f59e0b?style=for-the-badge" alt="Beta">
  <a href="https://github.com/aziqi/Wobblin-Windows/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/aziqi/Wobblin-Windows?color=10b981&style=for-the-badge" alt="License">
  </a>
</p>

---

## ✨ What is Wobblin?

**Wobblin** is a lightweight, open-source desktop utility for Windows 10 & 11 that brings the iconic **Wobbly Windows** physics animation back to modern operating systems — a beloved feature from the Linux desktop era (Compiz Fusion, KWin).

It also adds **GPU-accelerated Burn/Fade animations** for window Open, Close, and Minimize events, all powered by **Direct3D 11** hardware acceleration through a clean **Qt6 Fluent Dark** control panel.

---

## 🌀 Features

| Feature | Description |
| :--- | :--- |
| **Wobbly Window Physics** | Spring-mass-damper soft-body mesh that reacts to every move & drag in real time. |
| **Burn Window Animations** | GPU shader effects on window Open, Close, and Minimize events. |
| **Self-Wobble** | Wobblin's own control panel window also wobbles and animates! |
| **Process Exclusions** | Exclude specific apps from wobble or animations by `.exe` name, independently. |
| **System Tray Integration** | Runs quietly in the background; accessible from the system tray. |
| **Fluent Dark UI** | Modern frameless Qt6 control panel with custom titlebar & smooth transitions. |

---

## 🛠️ Tech Stack

| Component | Technology |
| :--- | :--- |
| **Language** | C++17 |
| **UI Framework** | Qt 6.8+ (Core, Gui, Widgets, Svg, Network) |
| **Graphics Engine** | Direct3D 11 + DXGI (hardware texture capture & pixel shaders) |
| **Hooking** | Win32 API — `SetWinEventHook`, `SetWindowsHookEx` |
| **Physics Model** | Real-time planar Spring-Mass-Damper 2D grid |
| **Build System** | CMake 3.16+ with MSVC (Visual Studio 2022) |

---

## 🚀 Building from Source

### Prerequisites

- **Windows 10 or 11** (64-bit)
- **Visual Studio 2022** with the *Desktop development with C++* workload
- **CMake** ≥ 3.16
- **Qt 6** (tested with Qt 6.8) — components: Core, Gui, Widgets, Svg, Network

### Steps

```powershell
# Clone the repository
git clone https://github.com/aziqi/Wobblin-Windows.git
cd Wobblin-Windows

# Configure the build
cmake -B build -S .

# Compile the Release binary
cmake --build build --config Release
```

Output: `build/Release/Wobblin.exe` (self-contained with all Qt DLLs deployed automatically)

---

## 🧪 Tested Environment

This project was developed and tested on the following hardware and OS configuration:

| | Details |
| :--- | :--- |
| **OS** | Windows 11 Pro 25H2 (Build 26200) |
| **Machine** | Lenovo LOQ 15ARP9 |
| **CPU** | AMD Ryzen 7 7435HS (8-core / 16-thread) @ 4.55 GHz |
| **GPU** | NVIDIA GeForce RTX 4060 Laptop (8 GB VRAM) |
| **RAM** | 32 GB DDR5 |
| **Display** | 1920×1080 @ 144 Hz (primary) + 2560×1440 @ 180 Hz (external) |

> **Note:** Testing was limited to this single machine. Behavior on other hardware configurations, DPI scales, or GPU vendors (Intel, AMD iGPU) has not been verified.

---

## ⚠️ Expect Bugs — First Release

> **This is the very first public release of Wobblin.**

There are likely many edge cases, crashes, and compatibility issues that have not been discovered yet. Known limitations include:

- Certain fullscreen games or DirectX-exclusive apps may behave unexpectedly.
- Very high DPI (200%+ scaling) environments are untested.
- Some system tray interactions may not behave correctly across all Windows versions.
- Running as Administrator is required for hooking into elevated-privilege windows.

**Your bug reports are greatly appreciated!** Please open an [Issue](https://github.com/aziqi/Wobblin-Windows/issues) with your OS version, hardware, and a description of what happened.

---

## 🙏 Credits & Acknowledgements

Inspired by the pioneering work of the **Compiz Fusion** and **KWin** open-source communities who first brought physics-based window animations to life, as well as the modern DirectX and Win32 developer communities.

---

## 📄 License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for full details.

<br>
<div align="center">
  <small>
    <b>Keywords:</b> wobbly windows, jelly windows, windows 10, windows 11, desktop customization, desktop ricing, window physics, compiz fusion alternative, kwin wobbly windows alternative, directx 11 window animation, qt6 desktop app, win32 hook, dwm effects, open-source windows tool.
  </small>
</div>
