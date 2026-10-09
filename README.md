# ApexSenseBridge 1.0.1

**Your Flydigi APEX. Native DualSense features. On Windows.**

ApexSenseBridge brings native PlayStation 5 DualSense input and feedback to
Flydigi **APEX 4, APEX 5 and APEX 6 Pro** controllers through a virtual DualSense.
APEX 4/5 translate adaptive triggers into FORCEADAPT resistance and audio haptics
into grip vibration. APEX 6 Pro uses its four voice-coil actuators for native
haptic audio and translated trigger feedback.

**1.0.0 is the first stable release**, bringing together the standalone app,
Playnite integration, controller-specific settings and a gamepad-friendly
interface in nine languages.

[Download ApexSenseBridge 1.0.0](https://github.com/ReynArts/ApexSenseBridge/releases/latest) ·
[Troubleshooting](TROUBLESHOOTING.md) · [Changelog](CHANGELOG.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/ReynArts/ApexSenseBridge?color=brightgreen)](https://github.com/ReynArts/ApexSenseBridge/releases/latest)
[![Authenticode](https://img.shields.io/badge/code%20signing-Authenticode-blueviolet.svg)](#security-signing--crash-recovery)

<img width="500" alt="screen ASB 1 0" src="https://github.com/user-attachments/assets/c47eba4c-e718-4890-9947-4afcca206aa8" />

---

## Contents

- [Stable 1.0.0 highlights](#stable-100-highlights)
- [Supported controllers](#supported-controllers)
- [Game compatibility](#game-compatibility)
- [How it works](#how-it-works)
- [Installation and first session](#installation--getting-started)
- [Standalone and Playnite usage](#usage-modes)
- [App settings and disconnection recovery](#app-settings--recovery)
- [Touchpad shortcuts](#touchpad-shortcuts)
- [Security and recovery](#security-signing--crash-recovery)
- [Diagnostics](#diagnostics--cli)
- [Building from source](#building-from-source)
- [License and notices](#license--third-party-notices)

---

## Stable 1.0.0 Highlights

- **🎮 Console Interface Refresh:** Complete console-grade interface for the Tray app, controller diagnostic, and language picker. Full gamepad navigation (D-pad/stick, Cross/A, Circle/B, Options/Menu, bumpers/triggers) and keyboard shortcuts (arrows, Enter, Escape, Ctrl+Tab, Ctrl+F).
- **🌍 9 Supported Languages:** English, French, Spanish, Simplified Chinese, Russian, Korean, Vietnamese, Japanese, and Brazilian Portuguese, with automatic Windows language detection and in-app language switching.
- **✨ Recently Added Games & 215+ Titles:** The home screen features a curated shelf of newly added games with "New" badges (last 30 days) and date tracking, backed by an automated daily PCGamingWiki sync.
- **🕹️ Controller-Specific Calibration & Hot-Plug Safety:** Independent calibration profiles for APEX 4, APEX 5, and APEX 6 Pro. Settings controls lock when no supported pad is connected, preventing misconfiguration. Engine selects calibration only after verified hardware identity.
- **🔊 Grip Vibration Amplification (0–200%):** Conventional grip vibration strength can be boosted up to 200% on APEX 4 and APEX 5 with saturation at the motor-command limit, without changing trigger resistance.
- **🎯 APEX 6 Pro Native Voice-Coil Haptics:** Full support for the APEX 6 Pro and official Phantom Blade Zero `0x98` variant. Features synchronized 1 kHz trigger carrier clocks, native Bow effect `0x22`, preserved quiet native PCM, and experimental companding gain (`--apex6-haptic-gain 100..200`).
- **🧭 APEX 4 Native Gyroscope Decoding:** Direct 16-bit hardware gyro parsing with customizable sensitivity (25–400%) and independent yaw correction slider.
- **🛡️ Intelligent HidHide Diagnostics:** Diagnoses access conflicts when DSX, DS4Windows, or BetterJoy hold the HidHide driver open and explicitly lists conflicting processes.
- **🕹️ Death Stranding 2 Touchpad Remapping:** Dedicated profile mapping `View/Back` to right-touchpad click (Likes, Communication) and `LB + Menu` to left-touchpad click (Photo Mode).
- **🛡️ Guided Disconnection Recovery & Zombie Reaper:** Automatic bridge recovery guidance on controller disconnect or sleep, plus automatic orphan child process termination on startup timeout.
- **🏎️ Built-In libVIIPER v0.7.0 Backend & Kernel USBip 0.9.8.0:** Attestation-signed Microsoft kernel driver with loopback-only communication and measured sub-2 ms report forwarding.

See the [changelog](CHANGELOG.md) for the development history and the
[release checklist](RELEASE_CHECKLIST.md) for packaging and validation requirements.

---

## Supported Controllers

| Controller | Connection Modes | Hardware Verification Status | Special Features |
|---|---|---|---|
| **Flydigi APEX 6 Pro** | 2.4 GHz Dongle / Wired USB | ✅ Verified in Hardware (Standard & PBZ `0x98`) | Four voice-coil actuators, native DualSense PCM haptics & companding gain, synchronized trigger carriers, native Bow `0x22` |
| **Flydigi APEX 5** | 2.4 GHz Dongle / Wired USB | ✅ Fully Verified in Hardware & Gameplay | Full FORCEADAPT Trigger Resistance, Audio Haptics, 0–200% Grip Rumble Gain, Onboard Profiles 1–4 switching |
| **Flydigi APEX 4** | 2.4 GHz Dongle / Wired USB (DInput) | ✅ Fully Verified in Hardware & Gameplay | Direct 16-bit Native Gyroscope with 25–400% sensitivity & yaw correction, FORCEADAPT Resistance, 0–200% Grip Rumble Gain, 32-byte/64-byte interface auto-detection |

---

## Game Compatibility

### ✅ Hardware Verified in Active Gameplay

Hardware-tested titles include the following. Features depend on the game and controller:
- **Call of Duty: Modern Warfare 4 Beta:** dynamic per-weapon trigger stops (semi-auto wall, full-auto recoil kick, bolt-action reset) and audio haptics (footsteps, slides, explosions).
- **Marvel's Spider-Man 2:** dynamic web-swing tension, web-shooter click feedback, alternating D-pad Up camera gestures, and full FNSM touch swipes.
- **Grand Theft Auto V Enhanced:** throttle resistance, ABS brake pulses, terrain rumble, and engine rev vibrations.
- **Death Stranding 2:** terrain/cargo load trigger resistance, full DualSense controls, and immersive haptics.
- **Ghost of Tsushima Director's Cut:** combat clash resistance, wind guidance swipe gestures (D-pad Right + Stick), and textured haptics.
- **Marvel's Spider-Man: Miles Morales:** venom charge triggers, web-swing tension, and FNSM gestures.
- **Warframe:** instant ability swipe gestures and dynamic weapon trigger resistance.

### 📚 215+ Automatically Detected DualSense Games

The catalogue tracks native PlayStation 5 DualSense support using PCGamingWiki. Catalogue detection does not certify every effect on every APEX model. The Standalone Tray App and Playnite extension automatically recognize and engage the bridge when any supported title launches.

---

## How It Works

```text
Non-DualSense / Standard Game:
APEX Controller ──► Native XInput ──► Game (ApexSenseBridge is completely idle)

Supported DualSense Game:
APEX Controller ──► ApexSenseBridge (C++20 Engine) ──► Virtual DualSense ──► Game
                      └── HidHide isolates physical gamepad
                          (Space Station M/K macros remain whitelisted)
```

- **Zero In-Game Injection:** No process injection, DLL hijacking, or memory tampering. All translation operates strictly via OS-level HID/controller interfaces and standard Windows APIs.
- **Sub-2 ms Latency:** Measured 0.67–1.54 ms p99 report forwarding on the validated APEX 5 setup at ~800–900 Hz. This measures bridge forwarding, not total input-to-display latency.
- **Fail-Closed Isolation:** If physical controller isolation cannot be guaranteed, the session halts safely to prevent confusing dual-input states.

---

## Installation & Getting Started

> [!TIP]
> For common diagnostic questions, minidump analysis, or setup help, consult the [Knowledge Base & Troubleshooting Guide](TROUBLESHOOTING.md).

### Requirements

- Windows 10 or 11 **x64**.
- A supported controller connected by **USB or its 2.4 GHz receiver**. For APEX 4, use **DInput mode**.
- Administrator access for the one-time USBip and HidHide driver installation.
- For Steam games, **disable Steam Input for that game** so native DualSense input and feedback reach the virtual controller.

### Option A: Standard Offline Installer (Recommended)

1. Download **`ApexSenseBridge-Setup.exe`** from the [Latest Release](https://github.com/ReynArts/ApexSenseBridge/releases/latest).
2. Run the installer (elevates once as Administrator).
   - Installs the core engine, Tray app, and Control Panel under `%ProgramFiles%\ApexSenseBridge`.
   - Bundles certified `usbip-win2 0.9.8.0` and `HidHide 1.5.230`.
   - Automatically registers the Playnite extension if Playnite is installed.
3. **Restart your PC** if prompted (required after installing or updating the USBip driver).

Uninstalling ApexSenseBridge never removes USBip. This is intentional: the
upstream USBip filter removal restarts Windows USB hubs and must be handled
separately from Windows Settings if the user really wants to remove it. HidHide
is also kept by default and can only be removed after a separate default-No
confirmation when its exact ApexSenseBridge install provenance is verified.
Settings, logs, learned associations and Playnite profiles are kept by default;
a separate prompt controls their deletion. Silent uninstall preserves all
drivers and data unless `/REMOVEUSERDATA` is explicitly supplied for data only.

> [!IMPORTANT]
> **Upgrading from older USBip versions (0.9.7.x):** If you have an older USBip package installed, uninstall it in Windows Settings (*Installed Apps*), restart Windows, and then run setup. This avoids the known upstream installer hang on *"Uninstalling USBip..."*.
>
> Do not repeatedly run older installers or remove driver packages manually with `pnputil`.

### Option B: Portable Package

1. Download **`ApexSenseBridge-Portable.zip`**.
2. Extract the archive anywhere on your PC.
3. Run `Install-Drivers.cmd` as Administrator once to install the required kernel drivers, then restart Windows.
4. Launch `Start-ApexSenseBridge.cmd` or `ApexSenseBridgeTray.exe`.

---

The portable package is intended for standalone use. Choose the installer for
automatic Playnite registration, Start-menu shortcuts and Windows uninstallation.

### Your First Session

1. Open **`ApexSenseBridgeTray.exe`**, the main app for games, settings and diagnostics.
2. Connect one supported controller and wait for its verified model to appear.
3. Launch a supported game. Tray starts the bridge automatically and displays its status; closing the game ends the session.
4. If the game only detects controllers at startup, close it, enable **Force continuous activation**, wait for **Bridge active**, then launch it again. Turn continuous activation off when finished.

`ApexSenseBridge.exe` is the engine and CLI diagnostic tool. Opening it without
a command also opens the Tray app.

---

## Usage Modes

### 1. Standalone System Tray App (Steam, Epic, EA, Game Pass, etc.)

- Launch **`ApexSenseBridgeTray.exe`** (or enable *Launch at Windows startup*).
- Sits silently in the System Tray with a modern console-style interface.
- **Automatic Detection:** Launches and closes the DualSense bridge automatically when any of the 215+ supported games start.
- **Dynamic Executable Learning:** Detects renamed, modded, elevated or multi-process game executables after 30 seconds of a stable automatically detected session while filtering out game launchers (Steam, Epic, EA, Ubisoft). Force Continuous Activation alone cannot identify which game should own an executable.
- **Dashboard & Per-Game Profiles:** Right-click the Tray icon to open the game list, force a specific profile, or assign a dedicated APEX 5 hardware slot (1–4) for each game.

### 2. Playnite Integration

- Seamlessly integrated with Playnite Desktop and Fullscreen modes.
- Game launches automatically initiate the bridge, isolate the physical pad, and apply profile mappings.
- Right-click any game in Playnite > **ApexSenseBridge** to customize trigger remapping, touchpad gesture profiles, onboard APEX 5 hardware slots, or 0–200% grip rumble gain.
- Do not run the Tray and Playnite automation for the same game. ApexSenseBridge protects the active session from being killed, but selecting one owner avoids duplicate notifications and ambiguous start/stop events.

---

## App Settings & Recovery

### Main App & Navigation

Open **ApexSenseBridgeTray.exe**, the main application (console interface, system tray, games,
controller settings and diagnostics). **ApexSenseBridge.exe** is the engine;
opening it without a command now opens the Tray application. Named commands
remain available for command-line diagnostics.

The Tray application features a modern console interface with full
gamepad navigation (D-pad/analog sticks, Cross/A, Circle/B, Options/Menu, bumpers/triggers)
and keyboard navigation (arrows, Enter, Escape, Ctrl+Tab, Ctrl+F). It automatically
detects the Windows display language among 9 supported localizations (English, French,
Spanish, Simplified Chinese, Russian, Korean, Vietnamese, Japanese, Brazilian Portuguese),
with on-the-fly switching via the language picker.

Each game has an optional executable selector in its details. A configured full
path takes priority over learned paths and catalogue detection; learned executables
are offered in that selector too. Leaving it empty preserves automatic detection.
The home screen features a curated shelf of newly added games with "New" badges
(for titles added within the last 30 days). The diagnostic panel contains the
prepared `.exe` launch tool, which waits for bridge readiness before starting the game.
Its manual session can be stopped from the home toggle or tray.

### Controller Feel & Per-Model Settings

The home page shows session phase, owner (Tray or Playnite), game, controller,
profile and refusal/stop reason. Per-model controller-feel settings live in Tray
preferences: trigger strength (0–100%), grip vibration strength (0–200% on
APEX 4/5, 0–100% on APEX 6) and audio-haptic threshold (0–95%). They apply on
the next Tray session; 100% preserves original strength and 0% mutes the effect.
Standard rumble is not gated by the audio threshold.
On APEX 4/5, values above 100% amplify existing rumble and audio-derived grip
vibrations, saturating at the existing 255 motor-command limit. Gain does not
restore audio removed by the threshold or alter trigger resistance, timing or
frequency. Start with a modest increase; Space Station's saved motor intensity
still influences the result. Playnite exposes the same grip-strength setting;
the engine caps its common strength at 100% after verifying an APEX 6 identity.
Connect a single supported controller and wait for its verified identity before
editing hardware settings. Controls lock on disconnect, failed identification
or ambiguous detection. APEX 4, APEX 5 and APEX 6 profiles are saved separately
in the settings file; the engine selects the matching profile after
verifying the actual hardware, not from the last model displayed in the UI.
Gyro tuning is APEX 4-only (25–400% sensitivity and separate yaw correction);
for APEX 4 gyro to work, the active Flydigi Space Station profile must map
gyro to "Mouse, always on". RGB is APEX 5-only, and the APEX 6 haptic
threshold is fixed at 0. General Tray preferences remain editable without a
controller. Existing strength/rumble preferences migrate without being reset;
subsequent edits affect only the selected model. Playnite keeps its independent
settings.

### Disconnection Recovery

After a confirmed runtime disconnection, Tray pauses automatic activation for
the interrupted session. The home page offers a voluntary **Resume bridge**
action and shows real controller, virtual-device, isolation and runtime checks.
Recovery retains the game/profile/APEX slot and never closes or relaunches the
game. A game that cannot reacquire the recreated DualSense must be restarted.
Playnite-owned sessions show guidance to resume from Playnite; Tray does not
take over. Dismissing the notice keeps detection paused for that game until it
closes; a fresh manual activation can also clear the pause.

For APEX 6 Pro audio configuration and model-specific limits, see
[APEX 6 setup and validation](APEX6_DUALSENSE_VALIDATION.md). Its voice-coil
trigger effects reproduce vibration rather than DualSense mechanical resistance.

---

## Touchpad Shortcuts

For games that use DualSense touchpad regions or swipes, ApexSenseBridge translates physical controller shortcuts:

| Game Profile | Controller Action | Virtual DualSense Touch Output |
|---|---|---|
| **Spider-Man 2** | Hold `View` / Hold `D-pad Up` | Swipe Left (FNSM App) / Swipe Up/Down (Camera) |
| **Miles Morales** | Hold `View` | Swipe Left (FNSM App) |
| **Ghost of Tsushima** | Hold `D-pad Right` + Flick Right Stick | Directional Wind Swipe in flick direction |
| **Warframe** | Hold `RB` + Press `A/B/X/Y` | Ability Swipes (Up / Down / Left / Right) |
| **Death Stranding 2** | Press/hold `View/Back` / `LB + Menu` | Right-side Click/Hold (Likes, Communication, Like Icon) / Left-side Click (Photo Mode) |
| **Standard DualSense** | Press `View` | Touchpad Click (no directional swipe) |

Death Stranding 2 uses the default Xbox layout: `View/Back` is forwarded without
an artificial hold delay, and the Photo Mode chord is consumed until both
buttons are released. `LB` and `Menu` alone retain their ordinary actions.
The left-side Photo Mode gesture is documented in the
[PlayStation guide](https://www.playstation.com/en-id/games/death-stranding-2-on-the-beach/death-stranding-2-on-the-beach-guide/);
the Xbox `LB + Menu` shortcut is corroborated by
[players on Steam](https://steamcommunity.com/app/3280350/discussions/1/809097865497552734/).

Rear buttons M1–M4 are not independent controls in a standard DualSense input
report. Assign them to standard controller buttons or keyboard/mouse inputs in
Flydigi Space Station; ApexSenseBridge keeps the verified mapping service
authorized through HidHide during an active session.

---

## Security, Signing & Crash Recovery

- **Authenticode Signed:** All executables (`ApexSenseBridge.exe`, `ApexSenseBridgeTray.exe`, `ApexSenseBridgeControl.exe`, `viiper.exe`, `libVIIPER.dll`, installer) are signed in official release packages. Signing does not guarantee the absence of Windows SmartScreen prompts.
- **Auto-Recovery Watchdog & RunOnce:** If a game crashes or power is lost, controller visibility and original APEX onboard profiles are automatically restored via an armed watchdog and HKCU RunOnce registry protection.
- **Manual Visibility Restoration:** Should you ever need to manually unhide your controller, open the Control Panel or run:
  ```powershell
  ApexSenseBridge.exe restore-controller-visibility
  ```

---

## Diagnostics & CLI

```text
ApexSenseBridge.exe list
ApexSenseBridge.exe diagnose [--all-hid] [--json]
ApexSenseBridge.exe identify [index]
ApexSenseBridge.exe input-status [index] [--seconds N] [--json]
ApexSenseBridge.exe test-rt [index]
ApexSenseBridge.exe test-trigger [index] [--side lt|rt|both] [--seconds N]
ApexSenseBridge.exe test-rumble [index] [--left 0..255] [--right 0..255] [--seconds N]
ApexSenseBridge.exe restore-controller-visibility
```

---

## Building from Source

### Build Requirements

- Windows 10 / 11 x64
- Visual Studio 2022 or 2026 Build Tools (Desktop C++ & Windows SDK)
- MSBuild and .NET Framework 4.6.2 targeting tools for Tray and Playnite
- CMake 3.25+
- Inno Setup 6 (for packaging)

```powershell
# 1. Build native engine and tests
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure

# 2. Build VIIPER backend and installer
.\scripts\build-libviiper-windows.ps1
.\scripts\build-installer.ps1
.\scripts\verify-version-consistency.ps1 -CheckArtifacts
```

There is one canonical local Tray executable:
`build-win\Release\ApexSenseBridgeTray.exe`. Files below `obj` are compiler
intermediates and must not be launched; the Tray build removes its intermediate
executable automatically. The repository root never contains runnable copies,
and `dist` is reserved for packaged release artifacts. The portable build
removes its expanded staging directory after creating the ZIP.

Before publishing a tag, follow the clean-VM matrix in
[`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md). The release workflow rejects a
tag/version mismatch, a stale package, a bad checksum or an unsigned payload.

---

## License & Third-Party Notices

- Distributed under the MIT License. See [LICENSE](LICENSE) for details.
- Uses patched components from VIIPER, usbip-win2, and HidHide. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
