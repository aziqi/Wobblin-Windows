<p align="center">
  <img src="wobblin_icon.png" width="160" alt="Wobblin Logo">
</p>

<h1 align="center">Wobblin</h1>

<p align="center">
  <strong>Bring your windows to life. Physics-based Wobbly Windows & GPU Burn animations for Windows 10 & 11.</strong><br>
  <sub>Inspired by the legendary Compiz Fusion & KWin Wobbly Windows effect.</sub>
</p>

<p align="center">
  <a href="https://github.com/aziqi/Wobblin-Windows/releases/latest">
    <img src="https://img.shields.io/badge/%E2%AC%87%EF%B8%8F_Download_Installer-v1.0-7c3aed?style=for-the-badge&logo=windows&logoColor=white" alt="Download Installer">
  </a>
  <a href="https://github.com/aziqi/Wobblin-Windows/releases/latest">
    <img src="https://img.shields.io/badge/Release-v1.0-10b981?style=for-the-badge&logo=github" alt="Release v1.0">
  </a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Windows 10/11">
  <img src="https://img.shields.io/badge/Status-Beta-f59e0b?style=for-the-badge" alt="Beta">
  <a href="https://github.com/aziqi/Wobblin-Windows/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-6366f1?style=for-the-badge" alt="License MIT">
  </a>
</p>

---

<p align="center">
  <img src="demo.gif" alt="Wobblin Demo Showcase" width="850">
</p>

## ✨ Overview

**Wobblin** is a lightweight, high-performance desktop enhancement utility for Windows 10 & 11. It restores the iconic **Wobbly Windows** soft-body physics from Linux (Compiz Fusion, KWin) to modern Windows environments, paired with **GPU-accelerated Direct3D 11 pixel shader animations** for window Open, Close, and Minimize events.

Controlled through a modern **Qt6 Fluent Dark** frameless dashboard, Wobblin gives you full control over physics stiffness, friction, and per-process animation exclusions.

---

## 🌀 Features at a Glance

<table>
  <tr>
    <td width="50%">
      <h3>🌀 Soft-Body Wobbly Physics</h3>
      <p>Real-time 2D Spring-Mass-Damper mesh physics that reacts dynamically to window dragging, flicking, and resizing without layout tearing.</p>
    </td>
    <td width="50%">
      <h3>🔥 GPU Shader Animations</h3>
      <p>Hardware-accelerated Direct3D 11 pixel shader effects (Burn, Fade, Scale) for window Open, Close, and Minimize transitions.</p>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <h3>🎛️ Fluent Dark Control Panel</h3>
      <p>Custom frameless Qt6 interface featuring glassmorphism, accent glows, custom titlebar controls, and responsive sliders.</p>
    </td>
    <td width="50%">
      <h3>🚫 Per-Process Exclusions</h3>
      <p>Independent exclusion lists for physics and animations by <code>.exe</code> name, ensuring full compatibility with games and full-screen tools.</p>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <h3>🖥️ Quiet System Tray Integration</h3>
      <p>Runs efficiently in the background with minimal system overhead; quickly toggle or configure settings from the Windows taskbar tray.</p>
    </td>
    <td width="50%">
      <h3>🪞 Self-Wobble Support</h3>
      <p>Wobblin’s own control panel window wobbles and animates seamlessly alongside all other desktop windows!</p>
    </td>
  </tr>
</table>

---

## 📦 Installation Options

Choose the installation method that fits your workflow:

### Option A: Standard Setup Installer (Recommended)
1. Download **[`Wobblin-v1.0-Setup.exe`](https://github.com/aziqi/Wobblin-Windows/releases/latest)** from the latest release.
2. Run the installer wizard (includes options for Start Menu shortcuts, Desktop icons, and Auto-Start with Windows).
3. Launch Wobblin (run as Administrator if hooking into elevated windows is required).

### Option B: Portable ZIP Package
1. Download **[`Wobblin-v1.0-Windows-x64.zip`](https://github.com/aziqi/Wobblin-Windows/releases/latest)**.
2. Extract the archive contents to any folder.
3. Double-click **`Wobblin.exe`** to start immediately without installation.

---

## 🛠️ Tech Stack & Architecture

| Layer | Technology | Details |
| :--- | :--- | :--- |
| **Core Language** | C++17 | Compiled with MSVC 2022 |
| **GUI Framework** | Qt 6.8+ | Core, Gui, Widgets, Svg, Network |
| **Graphics Engine** | Direct3D 11 & DXGI | Hardware-captured window textures & HLSL pixel shaders |
| **OS Hooking** | Win32 API | `SetWinEventHook` & `SetWindowsHookEx` for event interception |
| **Physics Simulation**| Spring-Mass-Damper | Real-time planar 2D Euclidean Hooke's Law mesh |
| **Build System** | CMake 3.16+ | Native MSVC project generation & runtime deployment |

---

## 🚀 Building from Source

### Prerequisites
- **Windows 10 / 11** (64-bit)
- **Visual Studio 2022** (Desktop development with C++ workload)
- **CMake** ≥ 3.16
- **Qt 6.8+** (installed and added to `PATH` or `CMAKE_PREFIX_PATH`)

### Build Steps

```powershell
# 1. Clone the repository
git clone https://github.com/aziqi/Wobblin-Windows.git
cd Wobblin-Windows

# 2. Configure build with CMake
cmake -B build -S .

# 3. Build the Release executable
cmake --build build --config Release
```

The resulting executable and deployed Qt runtime DLLs will be created under `build/Release/Wobblin.exe`.

---

## 🧪 Tested Environment

Wobblin was tested and verified on the following hardware & software configuration:

| Component | Specification |
| :--- | :--- |
| **OS** | Windows 11 Pro 25H2 (Build 26200) |
| **Machine** | Lenovo LOQ 15ARP9 |
| **CPU** | AMD Ryzen 7 7435HS (8-core / 16-thread @ 4.55 GHz) |
| **GPU** | NVIDIA GeForce RTX 4060 Laptop (8 GB VRAM) |
| **RAM** | 32 GB DDR5 |
| **Displays** | Primary 1920×1080 @ 144 Hz + External 2560×1440 @ 180 Hz |

---

## ⚠️ Notes & Known Limitations

> [!WARNING]
> **Beta Release Status**
> This is the initial public release (v1.0). You may encounter edge-case behavior or compatibility issues in certain scenarios:
> - **Exclusive Fullscreen Games**: Some DirectX/Vulkan games running in exclusive fullscreen mode may block window hooking. Use the Process Exclusions tab to exclude them.
> - **Administrator Privileges**: Running Wobblin as Administrator is recommended if you wish to wobble windows that are running with elevated privileges (e.g. Task Manager, Admin Command Prompts).
> - **High-DPI Scaling**: Ultra-high DPI scalings (200%+) are undergoing ongoing refinement.

Found a bug or have a feature request? Please open a report on the **[GitHub Issues](https://github.com/aziqi/Wobblin-Windows/issues)** page!

---

## 🗺️ Roadmap

- [x] Initial Release (v1.0) with Wobbly physics & Direct3D 11 shader animations
- [x] Modern Frameless Qt6 Fluent UI with customizable titlebar
- [x] Per-process exclusion system
- [x] Inno Setup installer & portable ZIP distribution
- [ ] Customizable HLSL shader effects (e.g. Rain, Matrix, Cube transitions)
- [ ] Multi-monitor physics boundary constraints refinement
- [ ] Preset profiles (e.g. Gentle, Extreme Jelly, Retro Compiz)

---

## 🤝 Contributing

Contributions, feedback, and pull requests are welcome! If you'd like to improve physics tuning, add new HLSL shaders, or enhance UI components:

1. Fork the project repository.
2. Create your feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'feat: Add some AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

---

## 🙏 Acknowledgements

Grateful appreciation to the open-source Linux desktop communities (**Compiz Fusion** & **KWin**) who pioneered wobbly window physics, and the Microsoft DirectX / Win32 graphics developer ecosystem.

---

## 📄 License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.


