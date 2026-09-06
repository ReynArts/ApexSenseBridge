# ApexSenseBridge 0.6.2

> **Bridge your Flydigi APEX 4 & APEX 5 controller into a native virtual PlayStation 5 DualSense on Windows.**  
> Experience authentic in-game Adaptive Triggers (FORCEADAPT), rich Haptic Feedback, motion gestures, and verified touchpad shortcuts with sub-2 ms latency.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/ReynArts/ApexSenseBridge?color=brightgreen)](https://github.com/ReynArts/ApexSenseBridge/releases/latest)
[![Authenticode](https://img.shields.io/badge/code%20signing-Authenticode-blueviolet.svg)](#security--integrity)

---

## ⚡ Key Highlights in 0.6.2

- **🛡️ New USBip 0.9.8.0 Kernel Driver:** Upgraded from the vulnerable 0.9.7.7 to the official Microsoft-attestation-signed `OSSign 0.9.8.0 x64` build (upstream commit `83bd1f78`). Fixes the infamous send-spinlock freeze (`DPC_WATCHDOG_VIOLATION 0x133`) and memory corruption bug (`4139f44`).
- **🎛️ Onboard APEX 5 Profiles (1–4) per Game:** Configure a specific onboard hardware profile (1 to 4) automatically per game in Playnite and the Standalone Tray App. The previous profile is cleanly restored upon exit.
- **✨ Flydigi Space Station Macro Whitelisting:** Keyboard & mouse shortcuts configured in Flydigi Space Station stay 100% functional during gameplay while the physical gamepad remains safely isolated to prevent double input.
- **🎮 Ghost DualSense Filter:** Automatically hides Space Station's legacy A-0356 proxy while ApexSenseBridge is running to prevent dual-controller conflicts with native games and Sony's *PlayStation Accessories* app.
- **🏎️ Blazing Fast Initialization (0.28s):** Built-in in-process `libVIIPER v0.7.0-asb7` backend with sub-millisecond USB attachment and loopback-only communication.

---

## 🕹️ Supported Controllers & Hardware Validation

| Controller | Connection Modes | Hardware Verification Status | Special Features |
|---|---|---|---|
| **Flydigi APEX 5** | 2.4 GHz Dongle / Wired USB | ✅ Fully Verified in Hardware & Gameplay | Full FORCEADAPT Trigger Resistance, Audio Haptics, Grip Rumble, Onboard Profiles 1–4 switching |
| **Flydigi APEX 4** | 2.4 GHz Dongle / Wired USB (DInput) | ✅ Fully Verified in Hardware & Gameplay | 32-byte direct vendor input (MI_02), FORCEADAPT Resistance, Grip Rumble, HidHide double-input isolation |

---

## 🎯 Game Compatibility & Validation Tiers

### ✅ Hardware Verified in Active Gameplay
Tested extensively with confirmed Adaptive Triggers, Haptics, and gestures:
- **Call of Duty: Modern Warfare 4 Beta:** dynamic per-weapon trigger stops (semi-auto wall, full-auto recoil kick, bolt-action reset) and audio haptics (footsteps, slides, explosions).
- **Marvel's Spider-Man 2:** dynamic web-swing tension, web-shooter click feedback, alternating D-pad Up camera gestures, and full FNSM touch swipes.
- **Grand Theft Auto V Enhanced:** throttle resistance, ABS brake pulses, terrain rumble, and engine rev vibrations.
- **Death Stranding 2:** terrain/cargo load trigger resistance, full DualSense controls, and immersive haptics.
- **Ghost of Tsushima Director's Cut:** combat clash resistance, wind guidance swipe gestures (D-pad Right + Stick), and textured haptics.
- **Marvel's Spider-Man: Miles Morales:** venom charge triggers, web-swing tension, and FNSM gestures.
- **Warframe:** instant ability swipe gestures and dynamic weapon trigger resistance.

### 📚 206+ Automatically Detected DualSense Games
Continuously synced with [PCGamingWiki](https://www.pcgamingwiki.com/) for native PlayStation 5 DualSense features. The Standalone Tray App and Playnite extension automatically recognize and engage the bridge when any supported title launches.

---

## 🔒 Runtime Architecture & Double-Input Protection

```text
Non-DualSense / Standard Game:
APEX Controller ──► Native XInput ──► Game (ApexSenseBridge is completely idle)

Supported DualSense Game:
APEX Controller ──► ApexSenseBridge (C++20 Engine) ──► Virtual DualSense ──► Game
                      └── HidHide isolates physical gamepad
                          (Space Station M/K macros remain whitelisted)
```

- **Zero In-Game Injection:** No process injection, DLL hijacking, or memory tampering. All translation operates strictly via OS-level HID/controller interfaces and standard Windows APIs.
- **Sub-2 ms Latency:** Measured 0.67–1.54 ms p99 report forwarding in active gameplay at ~800–900 Hz.
- **Fail-Closed Isolation:** If physical controller isolation cannot be guaranteed, the session halts safely to prevent confusing dual-input states.

---

## 🚀 Installation & Getting Started

> [!TIP]
> For common diagnostic questions, minidump analysis, or setup help, consult the [Knowledge Base & Troubleshooting Guide](TROUBLESHOOTING.md).

### Option A: Standard Offline Installer (Recommended)
1. Download **`ApexSenseBridge-Setup.exe`** from the [Latest Release](https://github.com/ReynArts/ApexSenseBridge/releases/latest).
2. Run the installer (elevates once as Administrator).
   - Installs the core engine, Tray app, and Control Panel under `%ProgramFiles%\ApexSenseBridge`.
   - Bundles certified `usbip-win2 0.9.8.0` and `HidHide 1.5.230`.
   - Automatically registers the Playnite extension if Playnite is installed.
3. **Restart your PC** if prompted (required after installing or updating the USBip driver).

> [!IMPORTANT]
> **Upgrading from 0.9.7.x:** If you have an older USBip package installed, uninstall it in Windows Settings (*Installed Apps*), restart Windows, and then run setup. This avoids the known upstream installer hang on *"Uninstalling USBip..."*.

### Option B: Portable Package
1. Download **`ApexSenseBridge-Portable.zip`**.
2. Extract the archive anywhere on your PC.
3. Run `Install-Drivers.cmd` as Administrator once to install the required kernel drivers, then restart Windows.
4. Launch `Start-ApexSenseBridge.cmd` or `ApexSenseBridgeTray.exe`.

---

## 🖥️ Usage Modes

### 1. Standalone System Tray App (Steam, Epic, EA, Game Pass, etc.)
- Launch **`ApexSenseBridgeTray.exe`** (or enable *Launch at Windows startup*).
- Sits silently in the System Tray.
- **Automatic Detection:** Launches and closes the DualSense bridge automatically when any of the 206+ supported games start.
- **Dynamic Executable Learning:** Detects renamed, modded, or custom game exes after 30 seconds of active play while filtering out game launchers (Steam, Epic, EA, Ubisoft).
- **Dashboard & Per-Game Profiles:** Right-click the Tray icon to open the game list, force a specific profile, or assign a dedicated APEX 5 hardware slot (1–4) for each game.

### 2. Playnite Integration
- Seamlessly integrated with Playnite Desktop and Fullscreen modes.
- Game launches automatically initiate the bridge, isolate the physical pad, and apply profile mappings.
- Right-click any game in Playnite > **ApexSenseBridge** to customize trigger remapping, touchpad gesture profiles, or onboard APEX 5 hardware slots.

---

## 👆 Touchpad Gesture Emulation

For games that use DualSense touchpad swipes for in-game mechanics, ApexSenseBridge translates intuitive physical controller shortcuts:

| Game Profile | Controller Action | Virtual DualSense Touch Output |
|---|---|---|
| **Spider-Man 2** | Hold `View` / Hold `D-pad Up` | Swipe Left (FNSM App) / Swipe Up/Down (Camera) |
| **Miles Morales** | Hold `View` | Swipe Left (FNSM App) |
| **Ghost of Tsushima** | Hold `D-pad Right` + Flick Right Stick | Directional Wind Swipe in flick direction |
| **Warframe** | Hold `RB` + Press `A/B/X/Y` | Ability Swipes (Up / Down / Left / Right) |
| **Standard DualSense** | Press `View` | Touchpad Click (no directional swipe) |

---

## 🛡️ Security, Signing & Crash Recovery

- **Authenticode Signed:** All executables (`ApexSenseBridge.exe`, `ApexSenseBridgeTray.exe`, `ApexSenseBridgeControl.exe`, `viiper.exe`, `libVIIPER.dll`, installer) are digitally signed to eliminate Windows SmartScreen warnings.
- **Auto-Recovery Watchdog & RunOnce:** If a game crashes or power is lost, controller visibility and original APEX onboard profiles are automatically restored via an armed watchdog and HKCU RunOnce registry protection.
- **Manual Visibility Restoration:** Should you ever need to manually unhide your controller, open the Control Panel or run:
  ```powershell
  ApexSenseBridge.exe restore-controller-visibility
  ```

---

## 🛠️ Diagnostics & CLI Quick Reference

```text
ApexSenseBridge.exe list
ApexSenseBridge.exe diagnose [--all-hid] [--json]
ApexSenseBridge.exe identify [index]
ApexSenseBridge.exe input-status [index] [--seconds N] [--json]
ApexSenseBridge.exe test-rt [index]
ApexSenseBridge.exe test-rumble [index]
ApexSenseBridge.exe restore-controller-visibility
```

---

## 🏗️ Building from Source

### Requirements
- Windows 10 / 11 x64
- Visual Studio 2022 (Desktop C++ & Windows SDK)
- CMake 3.25+
- Inno Setup 6 (for packaging)

```powershell
# 1. Build native engine and tests
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure

# 2. Build VIIPER backend and installer
.\scripts\build-libviiper-windows.ps1
.\scripts\build-installer.ps1
```

---

## 📄 License & Third-Party Notices
- Distributed under the MIT License. See [LICENSE](LICENSE) for details.
- Uses patched components from VIIPER, usbip-win2, and HidHide. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
