# ApexSenseBridge 0.6.3

> **Bridge your Flydigi APEX 4 & APEX 5 controller into a native virtual PlayStation 5 DualSense on Windows.**  
> Experience authentic in-game Adaptive Triggers (FORCEADAPT), rich Haptic Feedback, motion gestures, and verified touchpad shortcuts with sub-2 ms latency.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/ReynArts/ApexSenseBridge?color=brightgreen)](https://github.com/ReynArts/ApexSenseBridge/releases/latest)
[![Authenticode](https://img.shields.io/badge/code%20signing-Authenticode-blueviolet.svg)](#security--integrity)

---

## ⚡ Key Highlights in 0.6.3

- **🛡️ Non-destructive uninstall:** ApexSenseBridge never uninstalls USBip,
  preserves HidHide and user data by default, and requires separate explicit
  consent plus verified provenance before removing HidHide.
- **🔐 Verified updates and releases:** All automatic update paths validate the
  exact official setup asset, version, product and Authenticode publisher. CI
  blocks inconsistent versions, stale packages, bad checksums and unsigned
  release payloads.
- **🛡️ New USBip 0.9.8.0 Kernel Driver:** Upgraded from the vulnerable 0.9.7.7 to the official Microsoft-attestation-signed `OSSign 0.9.8.0 x64` build (upstream commit `83bd1f78`). Fixes the infamous send-spinlock freeze (`DPC_WATCHDOG_VIOLATION 0x133`) and memory corruption bug (`4139f44`).
- **🎛️ Onboard APEX 5 Profiles (1–4) per Game:** Configure a specific onboard hardware profile (1 to 4) automatically per game in Playnite and the Standalone Tray App. The previous profile is cleanly restored upon exit.
- **✨ Flydigi Space Station Macro Whitelisting:** Keyboard/mouse shortcuts and standard-button remaps configured in Flydigi Space Station remain available while the physical gamepad is isolated. Rear buttons M1–M4 must be assigned in Space Station; a standard virtual DualSense cannot expose them as four independent buttons.
- **🎮 Space Station Proxy Isolation:** Temporarily hides the verified GeniTech gamepad bus and any existing or late-created DualSense/XInput proxies while ApexSenseBridge is active, preventing duplicate or frozen controllers in games such as Minecraft with Controlify.
- **🔒 Safe Tray/Playnite Coexistence:** A session-ownership lock prevents the Tray and Playnite extension from terminating or replacing each other's bridge process. Run one automation owner per game for predictable notifications and lifecycle handling.
- **🔎 Detection and Touchpad Fixes:** Short titles such as `Control` no longer match utilities such as Controlify, and the standard `View` → touchpad-click path remains immediate during rapid consecutive presses.
- **📚 More Reliable Executable Learning:** Concurrent game processes are tracked independently, repeated sightings no longer restart the 30-second stability window, elevated executable paths use a restricted Windows query fallback, and validated bindings are flushed during Tray shutdown.
- **🧰 Installer Verification Fix:** Corrects the registry-key escaping bug that could report a successful USBip installation as failed, then misclassify it as unregistered driver remnants on the next setup run.
- **🏎️ Blazing Fast Initialization (0.28s):** Built-in in-process `libVIIPER v0.7.0-asb7` backend with sub-millisecond USB attachment and loopback-only communication.

See the [complete 0.6.3 release notes](RELEASE_NOTES_0.6.3.md) for upgrade
instructions, compatibility notes and validation details.

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

Uninstalling ApexSenseBridge never removes USBip. This is intentional: the
upstream USBip filter removal restarts Windows USB hubs and must be handled
separately from Windows Settings if the user really wants to remove it. HidHide
is also kept by default and can only be removed after a separate default-No
confirmation when its exact ApexSenseBridge install provenance is verified.
Settings, logs, learned associations and Playnite profiles are kept by default;
a separate prompt controls their deletion. Silent uninstall preserves all
drivers and data unless `/REMOVEUSERDATA` is explicitly supplied for data only.

> [!IMPORTANT]
> **Upgrading from 0.9.7.x:** If you have an older USBip package installed, uninstall it in Windows Settings (*Installed Apps*), restart Windows, and then run setup. This avoids the known upstream installer hang on *"Uninstalling USBip..."*.
>
> Versions 0.6.0 and 0.6.1 could falsely report that USBip failed even when
> `usbip-install.log` recorded success. This verification bug is fixed in
> 0.6.3. Do not repeatedly run the older installer or remove driver packages
> manually with `pnputil`.

### Option B: Portable Package
1. Download **`ApexSenseBridge-Portable.zip`**.
2. Extract the archive anywhere on your PC.
3. Run `Install-Drivers.cmd` as Administrator once to install the required kernel drivers, then restart Windows.
4. Launch `Start-ApexSenseBridge.cmd` or `ApexSenseBridgeTray.exe`.

---

## 🐧 Linux (Proton / Wine)

Linux is supported for the **APEX 4** and needs no Windows components: no
HidHide, no usbip-win2, no installer. The virtual DualSense is created by the
kernel, the pad is hidden for the session with `EVIOCGRAB`, and the kernel
releases it again if the bridge ever dies.

Adaptive triggers, PlayStation button prompts and rumble work in Proton titles.
DualSense **audio haptics** need the libVIIPER backend described below; the
default uhid backend creates no audio endpoint and reports zero haptic frames,
which is an honest signal rather than a silent failure.

> [!NOTE]
> The APEX 5 is untested on Linux. Its code paths compile and are wired up, but
> nobody has run one. Onboard profiles (`--apex-profile`) are APEX 5 only.

### Build and install

Requirements: a C++20 compiler, CMake 3.20+, and the development packages for
`hidapi-hidraw`, `libudev` and `libevdev` (Debian/Ubuntu: `libhidapi-dev
libudev-dev libevdev-dev`; Fedora: `hidapi-devel systemd-devel libevdev-devel`;
Arch: `hidapi systemd-libs libevdev`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

./packaging/linux/install.sh          # installs into ~/.local, asks for root only for udev
ApexSenseBridge list                  # should show the APEX and its vendor interface
```

`install.sh` is the only step that touches anything outside `$HOME`, and only to
install udev rules. Your user must be in the `input` group for `/dev/uhid`; the
script says so if it is not.

### Running a game

`asb-run` starts the bridge, waits until it is actually ready, runs the game and
stops the bridge afterwards. It fails the launch rather than letting a game start
while seeing two controllers.

**Lutris** — per game, *System options* → **Command prefix**:

```
/home/you/.local/bin/asb-run wrap
```

Chain other wrappers after it if you use them, e.g. `asb-run wrap
game-performance`. Lutris appends the game's own command line.

**Steam** — per game, *Launch Options*:

```
/home/you/.local/bin/asb-run wrap %command%
```

Launchers with neither field can call `asb-run start "<game name>"` before and
`asb-run stop` after, waiting for the first to finish.

Then set these in the same per-game environment:

| Variable | Value | Why |
|---|---|---|
| `ASB_GAME` | the game's name | picks its touchpad profile from [`supported_games.json`](data/supported_games.json). Steam supplies `$SteamAppId` instead |
| `SDL_GAMECONTROLLER_IGNORE_DEVICES` | `0x04b4/0x2412` | hides the physical APEX from SDL, which still enumerates it after `EVIOCGRAB` stops its events |
| `SDL_JOYSTICK_HIDAPI_PS5` | `0` | stops SDL's own PS5 driver opening the virtual pad's `/dev/hidraw*` behind Proton's back |
| `PROTON_NO_STEAMINPUT` | `0` | see the warning below |

Without the two `SDL_*` variables the game sees the pad twice and its button
prompts flicker between PlayStation and Xbox on every press.

> [!WARNING]
> Set `PROTON_NO_STEAMINPUT=0` **explicitly**, even though 0 is the default.
> Proton's Wayland branch treats an unset value as a cue to set it to 1 itself
> and, on the way, deletes `SDL_GAMECONTROLLER_IGNORE_DEVICES` from the
> environment - which silently undoes the isolation above.

> [!WARNING]
> Do not set `PROTON_PREFER_SDL` or `PROTON_USE_SDL`. They imply
> `PROTON_DISABLE_HIDRAW=1`, which disables Proton's native DualSense path and
> with it the adaptive triggers.

### Backends

| Backend | Needs | Gives you |
|---|---|---|
| `uhid` (default) | nothing beyond the udev rules | triggers, prompts, rumble |
| `libVIIPER` over USB/IP | `vhci-hcd`, installed by `install.sh` | the above plus audio haptics |

`--virtual-backend auto` prefers libVIIPER when all of its prerequisites are
present - the kernel module, write access to its three paths, **and** the library
itself - and falls back to uhid otherwise. `--virtual-backend uhid` forces the
plain backend on a machine that would otherwise pick libVIIPER. Neither needs
root at runtime.

### Manual checks

```sh
ApexSenseBridge identify                      # confirm the pad and its firmware
ApexSenseBridge input-status --seconds 10     # confirm reports arrive (~1 kHz on an APEX 4)
ApexSenseBridge virtual-ds --seconds 20       # confirm the virtual DualSense enumerates
ApexSenseBridge stop-active-sessions          # clean up a session left behind
```

---

## 🖥️ Usage Modes

### 1. Standalone System Tray App (Steam, Epic, EA, Game Pass, etc.)
- Launch **`ApexSenseBridgeTray.exe`** (or enable *Launch at Windows startup*).
- Sits silently in the System Tray.
- **Automatic Detection:** Launches and closes the DualSense bridge automatically when any of the 206+ supported games start.
- **Dynamic Executable Learning:** Detects renamed, modded, elevated or multi-process game executables after 30 seconds of a stable automatically detected session while filtering out game launchers (Steam, Epic, EA, Ubisoft). Force Continuous Activation alone cannot identify which game should own an executable.
- **Dashboard & Per-Game Profiles:** Right-click the Tray icon to open the game list, force a specific profile, or assign a dedicated APEX 5 hardware slot (1–4) for each game.

### 2. Playnite Integration
- Seamlessly integrated with Playnite Desktop and Fullscreen modes.
- Game launches automatically initiate the bridge, isolate the physical pad, and apply profile mappings.
- Right-click any game in Playnite > **ApexSenseBridge** to customize trigger remapping, touchpad gesture profiles, or onboard APEX 5 hardware slots.
- Do not run the Tray and Playnite automation for the same game. Version 0.6.2 protects the active session from being killed, but selecting one owner avoids duplicate notifications and ambiguous start/stop events.

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

Rear buttons M1–M4 are not independent controls in a standard DualSense input
report. Assign them to standard controller buttons or keyboard/mouse inputs in
Flydigi Space Station; ApexSenseBridge keeps the verified mapping service
authorized through HidHide during an active session.

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

For Linux, see [Linux (Proton / Wine)](#-linux-proton--wine) above.

```powershell
# 1. Build native engine and tests
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure

# 2. Build VIIPER backend and installer
.\scripts\build-libviiper-windows.ps1
.\scripts\build-installer.ps1
.\scripts\verify-version-consistency.ps1 -CheckArtifacts
```

Before publishing a tag, follow the clean-VM matrix in
[`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md). The release workflow rejects a
tag/version mismatch, a stale package, a bad checksum or an unsigned payload.

---

## 📄 License & Third-Party Notices
- Distributed under the MIT License. See [LICENSE](LICENSE) for details.
- Uses patched components from VIIPER, usbip-win2, and HidHide. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
