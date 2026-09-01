# ApexSenseBridge 0.3.0

ApexSenseBridge gives a Flydigi APEX 5 a virtual DualSense path on Windows while translating native adaptive-trigger and haptic feedback back to the physical controller.

## Locked runtime architecture

The two game modes are intentionally different:

```text
Unknown or explicitly disabled game
APEX physical ── native XInput ──► game
No ApexSenseBridge process

Recognized game or manual DualSense profile
APEX physical ── HID/XInput ──► ApexSenseBridge ──► virtual DualSense ──► game
                 physical game interfaces hidden by HidHide
```

In DualSense mode the game never receives sticks, buttons, triggers or D-pad input directly from the physical APEX. ApexSenseBridge reads and translates the complete state. Direct HID means direct input to the bridge, not direct input to the game. If the lossless proxy or physical isolation cannot be established, Playnite cancels the game launch.

Recognized games receive their verified profile automatically. Unknown and explicitly disabled games retain their native APEX XInput path and do not start the engine, sidecar, service or tray application.

## Verified and tested games

The bridge, triggers, and DSP haptics have been 100% verified in-game on:

- **Call of Duty: Modern Warfare 4 Beta:** dynamic adaptive triggers per weapon (semi-auto wall, full-auto recoil kick, bolt-action stop) and textured audio haptics (footsteps, slides, explosions).
- **Marvel's Spider-Man 2:** adaptive web-line tension while swinging, web-shooter clicks, gadget resistances, and full touchpad swipe gestures (FNSM app & abilities via WGI Fix).
- **Grand Theft Auto V Enhanced (GTA V Enhanced):** driving trigger resistance (throttle resistance, ABS braking, off-road vibration) and deep haptic feedback (engine revs, gear shifts, surface grit).

## End-user installation

Run `ApexSenseBridge-Setup.exe`. The offline installer uses one administrator elevation and does not show PowerShell or console windows. It installs:

- the statically linked engine and Win32 control panel under `%ProgramFiles%\ApexSenseBridge`;
- the pinned patched VIIPER sidecar and its source/license notices;
- usbip-win2 `0.9.7.7` and HidHide `1.5.230` when their exact versions are absent;
- the Playnite extension under the current user's Playnite extension directory;
- the engine location and exact dependency ownership metadata under `HKLM\Software\ApexSenseBridge`.

usbip-win2 `0.9.7.8` is explicitly refused because its official release warns about memory corruption and BSOD risk. The native binaries use the static MSVC runtime, so the Visual C++ Redistributable is not a prerequisite. A Windows restart can be required after first driver installation.

The lightweight `ApexSenseBridgeControl.exe` panel can test APEX detection, restore HidHide/WGI state, open logs and start an explicit full dependency removal. It is not resident in memory.

## Playnite usage

No executable path, XInput index, or initial per-game configuration must be selected.

1. Start a recognized game normally from Playnite Desktop or Fullscreen mode.
2. The extension selects Spider-Man 2, Miles Morales, Ghost of Tsushima, or
   Warframe from the normalized Playnite title or installation-folder name.
3. To override it, right-click the game in Playnite Desktop and open
   `ApexSenseBridge`. A manual profile or explicit disable always wins.

The extension resolves the engine from the machine-wide installation record. Existing `BridgeExecutablePath`, `XInputIndex`, profiles, haptic threshold and preferences are accepted during migration, but the obsolete path/index controls are no longer exposed.

Before Playnite is allowed to launch a recognized or manually configured game, the engine verifies all of the following:

- the selected APEX is identified and its physical input source is open;
- the virtual DualSense exists and accepts a complete neutral report;
- every physical game-facing interface is hidden;
- the crash watchdog and RunOnce recovery are armed.

When the game stops, the engine submits a neutral virtual report, waits briefly for physical controls to be released, detaches the virtual controller, restores the exact HidHide/WGI snapshots and exits. The extension also rejects a duplicate same-game startup event for four seconds after stop; this prevents a residual controller `A/Cross` press from relaunching the game as focus returns to Playnite Fullscreen.

The Spider-Man 2 profile additionally applies the temporary WGI compatibility setting, enables its two documented touchpad gestures and checks that Steam is not retaining a physical controller handle before isolation.

Automatic detection is enabled by default and can be disabled globally in the
extension settings. `Utiliser la détection automatique` removes a per-game
override, while `Désactiver pour ce jeu` stores a persistent opt-out. Detection
never guesses a profile for an unknown title.

### Per-game touchpad gesture profiles

Synthetic swipes are disabled for unknown games. The bridge only reverses a
documented Xbox fallback into the touch gesture expected by the same game when
it detects the virtual DualSense:

| Playnite profile | Conventional-controller input | Virtual touch output |
|---|---|---|
| Spider-Man 2 | hold `View`; hold `D-pad Up` | swipe left (FNSM); swipe up (camera) |
| Miles Morales | hold `View` | swipe left (FNSM) |
| Ghost of Tsushima | hold `D-pad Right` + move right stick | swipe in the same direction |
| Warframe | hold `RB`, then press `A/X/B/Y` | swipe up/down/left/right |

Short `View`, `D-pad Up`, `D-pad Right`, and `RB` presses are replayed when a
profile uses them as modifiers. Ghost requires the right stick to be neutral
when its layer starts. Warframe requires the default Xbox controller layout and
requires `RB` to arrive before the face button. These guards reduce accidental
actions. `DualSense standard` otherwise keeps `View` mapped directly to the
touchpad click and creates no directional gesture.

## Input, latency and resource behavior

`PhysicalInputSource` first attempts an overlapped, event-driven HID source on the APEX `IG_01` interface belonging to the same Windows container as FORCEADAPT. A HID descriptor that cannot represent the full physical state is rejected. On the validated APEX firmware, the missing `Rz` control makes the internal XInput source the lossless fallback.

That fallback is still private to ApexSenseBridge: the physical APEX remains hidden from the game. It associates the controller by VID/PID through `XInputGetCapabilitiesEx`, translates the complete state without per-report allocation and uses Windows events/high-resolution timers instead of the former fixed `sleep_for(4 ms)` loop. `--xinput-index` remains available only as an advanced diagnostic override.

Every physical report immediately produces a complete DualSense input report. Keepalive reports are emitted only when physical reports stop arriving. VIIPER feedback uses a buffered TCP reader; audio haptics are reduced in 5 ms windows while preserving peaks/transients, and adaptive-trigger effects bypass that aggregation.

The optional `--telemetry-json PATH` output includes p50/p95/p99 input latency, physical and virtual rates, lost/coalesced reports, CPU, working set and initialization stages. A hardware validation session with active trigger movement measured 0.52% engine CPU, about 12.4 MiB engine working set, 0.635 ms p99 report latency, no lost report and 0.794 s initialization. A separate live process sample measured about 39.2 MiB total working set for the engine, recovery watchdog and VIIPER together.

## Failure recovery and uninstall

The bridge snapshots the complete existing HidHide and WGI configuration. After an engine crash during a Playnite session, the watchdog deliberately keeps the APEX hidden until Playnite signals that the game has stopped, then restores it. An HKCU RunOnce marker covers power loss or logout. Manual recovery is available from the control panel or:

```text
ApexSenseBridge.exe restore-controller-visibility
```

Normal uninstall sends an acknowledged maintenance-stop request and waits for input neutralization, virtual-device detach and APEX/WGI restoration; forced termination is only a fallback for a hung engine. It then removes the extension, profiles, logs, recovery entries, files and registry records, followed by only the dependencies installed by ApexSenseBridge. Drivers that predated ApexSenseBridge are preserved. The control panel offers an explicit full-removal action for users who also want those pre-existing dependencies removed.

## Developer build

Requirements:

- Windows 10/11 x64;
- Visual Studio 2022 Build Tools with Desktop C++ and a Windows SDK;
- CMake;
- Go for rebuilding the pinned VIIPER sidecar;
- Inno Setup 6 for the final installer.

Build the native targets and tests:

```powershell
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure
```

Rebuild the pinned VIIPER payload and create the single offline installer:

```powershell
.\scripts\build-viiper-windows.ps1
.\scripts\build-installer.ps1
```

The Playnite build script uses an installed Playnite SDK when available. On a clean CI worker it downloads the official pinned `PlayniteSDK 6.16.0` NuGet package, verifies its SHA-256 and creates the standard ZIP-based `.pext` format without requiring a full Playnite installation.

Release artifacts are written to `dist/`:

```text
ApexSenseBridge-Setup.exe
ApexSenseBridge_e41b1737-6753-4b59-bc65-4fdd6a7df7f4_0_3_0.pext
```

## Useful diagnostic commands

```text
list
diagnose [--all-hid] [--json]
identify [index]
input-status [index] [--seconds N] [--json]
virtual-ds [--seconds N] [--json] [--viiper PATH]
bridge-triggers [index] [--seconds N] [--viiper PATH]
                [--telemetry-json PATH] [--xinput-index 0..3]
                [--rumble] [--haptic-threshold 0..95]
                [--verify-virtual-input] [--touchpad-profile NAME]
                [--view-hold-swipe-up]
                [--spiderman2-wgi-fix] [--session-token 32HEX]
test-rt [index]
test-rumble [index]
clear [index]
restore-controller-visibility
```

`bridge-triggers` always enforces full proxying and physical isolation in 0.3.0, including when legacy `--proxy-xinput` or `--isolate-apex` flags are omitted. A session failure is fail-closed: the game is never allowed to fall back to a visible physical APEX during a DualSense profile.

`--touchpad-profile` accepts `none`, `spider-man-2`, `miles-morales`,
`ghost-of-tsushima`, or `warframe`. The old `--view-hold-swipe-up` switch remains
available only for command-line compatibility; new integrations should select
an explicit game profile.

## Source layout

```text
src/core/                 value types and safety helpers
src/dualsense/            report codec, feedback translation and VIIPER protocol
src/flydigi/              FORCEADAPT protocol and APEX device facade
src/platform/windows/     HID/XInput, isolation, audio, IPC and virtual device
src/control/              non-resident Win32 control panel
playnite/ApexSenseBridge/ Playnite GenericPlugin
installer/                offline Inno Setup definition and driver manifest
tests/                    mapping, protocol, lifecycle and Windows integration tests
```

See `THIRD_PARTY_NOTICES.md` and the licenses installed with the application for upstream attribution and redistribution terms.
