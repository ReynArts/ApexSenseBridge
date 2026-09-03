# ApexSenseBridge 0.6.0

ApexSenseBridge gives a Flydigi APEX 4 or APEX 5 a virtual DualSense path on Windows while translating native adaptive-trigger and haptic feedback back to the physical controller.

## ⚠️ Known USBIP-WIN2 crash risk

ApexSenseBridge relies on the third-party `usbip-win2` kernel driver through
VIIPER to expose the virtual DualSense. A defect in that driver can crash
Windows with a blue screen; because it runs in kernel mode, ApexSenseBridge
cannot catch or recover from such a failure in user space. Repeated
`DPC_WATCHDOG_VIOLATION (0x133)` crashes have been observed during active game
sessions with `usbip-win2 0.9.7.7`, matching the upstream
[`usbip2_ude!send` report](https://github.com/vadimgrn/usbip-win2/issues/172).
The upstream `0.9.7.8` release is not a safe workaround: its own release notes
warn that it can cause memory corruption and BSODs.

This risk applies whether ApexSenseBridge is started by Playnite or by the
standalone tray application; the launcher is not the component executing the
faulting kernel code. If a BSOD occurs, do not repeatedly reproduce it: restart Windows,
stop the current bridge session, preserve the newest file from
`C:\Windows\Minidump`, and review that dump before trying again. A future
signed `usbip-win2` release must be validated before this warning can be
removed.

APEX 4 and APEX 5 are both hardware- and in-game-validated release paths. The
APEX 4 bridge has passed wired and 2.4 GHz DInput testing on retail DeviceTypes
`84`/`103` and firmware `0x6830`/`0x6837`: read-only identity verification,
complete event-driven input, grip rumble, FORCEADAPT resistance and automatic
trigger reset all passed. A Death Stranding 2 session confirmed complete
virtual DualSense controls, adaptive-trigger feedback and haptics. After adding
targeted isolation for the auxiliary `MI_01` mouse, a nine-minute Fortnite
regression observed no leaked physical event and no double input, forwarded
499,461 input samples at about 913 Hz with 149 us p99 latency and no lost or
coalesced report, and delivered rumble without a write failure. Fortnite sent
only Normal trigger commands in that run, so games still need to provide an
active native DualSense effect for resistance to be felt. The bridge verifies
the controller's Flydigi identity before it permits any FORCEADAPT or rumble
write.

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

## Game compatibility & validation levels

- **✅ Hardware verified with ApexSenseBridge (In-Game Tested & Confirmed):**
  - **Call of Duty: Modern Warfare 4 Beta:** dynamic adaptive triggers per weapon (semi-auto wall, full-auto recoil kick, bolt-action stop) and textured audio haptics (footsteps, slides, explosions).
  - **Marvel's Spider-Man 2:** adaptive web-line tension while swinging, web-shooter clicks, gadget resistances, alternating D-pad Up camera gestures, and full touchpad swipe gestures (FNSM app & abilities).
  - **Grand Theft Auto V Enhanced (GTA V Enhanced):** driving trigger resistance (throttle resistance, ABS braking, off-road vibration) and deep haptic feedback (engine revs, gear shifts, surface grit).
  - **Death Stranding 2:** full virtual DualSense controls, adaptive terrain/cargo trigger resistances, and rich haptic vibration feedback.
  - **Ghost of Tsushima Director's Cut:** directional wind touchpad swipe gestures (D-pad Right + Stick), combat trigger resistance, and haptic feedback.
  - **Marvel's Spider-Man: Miles Morales:** venom powers trigger feedback and FNSM touchpad integration.
  - **Warframe:** custom touchpad gesture profiles for instant ability activation and adaptive trigger feedback.
- **206+ automatically detected games with documented native DualSense features:**
  - Continuously synchronized with PCGamingWiki for titles with documented native DualSense Adaptive Triggers and Haptics.
  - Automatically monitored and bridged by the Standalone Tray App and Playnite extension.

## Input performance & safety

- **Sub-2 ms measured p99 forwarding latency** on the hardware test system (measured 0.67–1.54 ms p99 report forwarding in active gameplay).
- **No game-process injection or memory modification.** Uses OS-level virtual HID/controller interfaces and standard Windows APIs. Anti-cheat compatibility may vary by game.

## End-user installation

For common troubleshooting, diagnostic interpretations (JSON test outputs), and game setup guides, see the [Knowledge Base & Troubleshooting Guide](TROUBLESHOOTING.md).

Run `ApexSenseBridge-Setup.exe`. The offline installer uses one administrator elevation and does not show PowerShell or console windows. It installs:

- the statically linked engine and Win32 control panel under `%ProgramFiles%\ApexSenseBridge`;
- the standalone background Tray application (`ApexSenseBridgeTray.exe`);
- the official integrated `libVIIPER asb5` backend, the `asb3` sidecar fallback,
  and their source/license notices;
- usbip-win2 `0.9.7.7` when no healthy compatible `0.9.7.5`–`0.9.7.7` driver is present, and HidHide `1.5.230` when its exact version is absent;
- the Playnite extension under the current user's Playnite extension directory;
- the engine location and exact dependency ownership metadata under `HKLM\Software\ApexSenseBridge`.

All Windows executables (`ApexSenseBridge.exe`, `ApexSenseBridgeTray.exe`, `ApexSenseBridgeControl.exe`, `viiper.exe`, `libVIIPER.dll`, and `ApexSenseBridge-Setup.exe`) are digitally Authenticode code-signed to ensure binary integrity and prevent Windows SmartScreen security warnings.

usbip-win2 `0.9.7.8` is explicitly refused because its official release warns about memory corruption and BSOD risk. This does not make `0.9.7.7` crash-proof; see the known-risk warning above. The native binaries use the static MSVC runtime, so the Visual C++ Redistributable is not a prerequisite. A Windows restart can be required after first driver installation.

Healthy WHLK-certified USBip `0.9.7.5`–`0.9.7.7` installations are preserved because they use the compatible driver ABI. Driver signing and ABI compatibility do not guarantee freedom from the kernel crash described above. If an unsupported version or an incomplete USBip installation is detected, setup now stops with repair instructions instead of invoking USBip's nested upgrade/uninstall path, which can hang on `Uninstalling USBip…`. A fresh prerequisite install is limited to five minutes and writes its persistent diagnostic log to `%ProgramData%\ApexSenseBridge\usbip-install.log`.

### Portable ZIP

`ApexSenseBridge-Portable.zip` contains the standalone tray application, engine,
control panel, integrated VIIPER backend, sidecar fallback, licenses, and offline
driver prerequisites. Extract the entire folder, run `Install-Drivers.cmd` once
as administrator, restart Windows, then launch `Start-ApexSenseBridge.cmd`.

The application payload is portable, but the USBip and HidHide kernel drivers necessarily remain system-wide Windows components. The portable helper preserves a healthy compatible existing installation and refuses ambiguous USBip upgrades rather than entering the upstream uninstaller hang. Use the regular setup when automatic Playnite integration, shortcuts, startup registration, and Windows uninstall metadata are wanted.

The lightweight `ApexSenseBridgeControl.exe` panel can test APEX detection, restore HidHide/controller visibility, open logs and start an explicit full dependency removal. It is not resident in memory.

## Standalone Tray App (Outside Playnite)

For players using other game launchers (Steam, Epic Games Store, GOG, EA App, Xbox Game Pass, etc.):

1. Launch **`ApexSenseBridgeTray.exe`** (or check "Launch at Windows startup" during installation).
2. The application sits quietly in the notification area (System Tray).
3. **Day-One Automatic Detection**: Supports **206+ automatically detected games with documented native DualSense features** out of the box. As soon as any supported game launches (continuously synchronized with PCGamingWiki for Adaptive Triggers & Haptics), ApexSenseBridge automatically starts the virtual DualSense bridge and shows a brief notification.
4. When you exit the game, the bridge cleanly closes and restores the native physical XInput controller.
5. Click the taskbar tray icon to open the clean dashboard, view the list of all 206+ supported titles, manually force a profile (e.g. *Spider-Man 2*, *Ghost of Tsushima*), or trigger a manual database sync.

The Tray learns the exact executable only after the existing detector has
started a successful bridge session and that session remains active for 30
seconds. The generated game database is also enriched offline with Windows,
non-launcher executable names from Discord's detectable-application catalog,
joined only by a Steam AppID whose store title has been verified by a unique
exact normalized-name match. Unknown or ambiguous identities remain unset and
cannot feed the executable index. Discord entries that look like launchers,
editors, server managers, benchmarks, crash reporters, updaters or configuration
tools are also excluded even when their upstream `is_launcher` flag is false.
At runtime the remaining names are resolved through a
lock-free, case-insensitive in-memory index; names shared by multiple supported
games are excluded. No Discord or PCGamingWiki request occurs while detecting a
launch.

The first launch therefore keeps the existing WMI/exact/metadata/fuzzy
detection path, with the exact database executable index ahead of the existing
heuristics. Later launches can additionally use one lock-free learned-path lookup
before falling back to the same detector. Validated associations are stored locally in
`%LocalAppData%\ApexSenseBridge\learned_executables.json`. The **Learned** view
can remove individual associations or export selected executable names for
manual database review; exports never contain absolute local paths and are never
uploaded automatically.

## Playnite usage

No executable path, XInput index, or initial per-game configuration must be selected for a normal setup installation.

1. Start a recognized game normally from Playnite Desktop or Fullscreen mode.
2. The extension selects Spider-Man 2, Miles Morales, Ghost of Tsushima, or
   Warframe from the normalized Playnite title or installation-folder name.
3. To override it, right-click the game in Playnite Desktop and open
   `ApexSenseBridge`. A manual profile or explicit disable always wins.

The extension resolves the engine from the machine-wide installation record by default. For a portable or custom installation, its Playnite settings provide an executable picker; that explicit `ApexSenseBridge.exe` path takes priority over automatic discovery. Existing `XInputIndex`, profiles, haptic threshold and preferences are still accepted during migration.

Before Playnite is allowed to launch a recognized or manually configured game, the engine verifies all of the following:

- the selected APEX is identified and its physical input source is open;
- the virtual DualSense exists and accepts a complete neutral report;
- every physical game-facing interface is hidden;
- the crash watchdog and RunOnce recovery are armed.

When the game stops, the engine submits a neutral virtual report, waits briefly for physical controls to be released, detaches the virtual controller, restores the exact controller-visibility/HidHide snapshot and exits. The extension also rejects a duplicate same-game startup event for four seconds after stop; this prevents a residual controller `A/Cross` press from relaunching the game as focus returns to Playnite Fullscreen.

The Spider-Man 2 profile only adds its verified touchpad gestures. It does not alter game settings and it works when Steam is already running.

Automatic detection is enabled by default and can be disabled globally in the
extension settings. `Use automatic detection` removes a per-game
override, while `Disable for this game` stores a persistent opt-out. Detection
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

For APEX 4 in DInput mode, `PhysicalInputSource` reads the complete 32-byte
Flydigi V1 state stream from the verified `04B4:2412` vendor interface `MI_02`.
Its game-facing `MI_00` gamepad and `MI_01` auxiliary mouse are isolated without
hiding the `MI_02`/`MI_03` command and input transports used by the bridge. A
wired hardware run received
5,001 reports in 10 seconds with 849 state changes, no timeout and no parse
failure; grip rumble and a gentle RT resistance were both physically confirmed.
The 2.4 GHz dongle run received 10,001 reports in 10 seconds with 547 state
changes, all four D-pad directions, no timeout or parse failure; grip rumble and
RT resistance were again physically confirmed. A subsequent full-session
regression confirmed zero physical Raw Input event leakage while the virtual
DualSense continued receiving the full stick, trigger, D-pad and button ranges.

For APEX 5, `PhysicalInputSource` first attempts an overlapped, event-driven HID source on the APEX `IG_01` interface belonging to the same Windows container as FORCEADAPT. A HID descriptor that cannot represent the full physical state is rejected. On the validated APEX 5 firmware, the missing `Rz` control makes the internal XInput source the lossless fallback.

That fallback is still private to ApexSenseBridge: the physical APEX remains hidden from the game. It associates the controller by VID/PID through `XInputGetCapabilitiesEx`, translates the complete state without per-report allocation and uses Windows events/high-resolution timers instead of the former fixed `sleep_for(4 ms)` loop. `--xinput-index` remains available only as an advanced diagnostic override.

Every physical report immediately produces a complete DualSense input report. Keepalive reports are emitted only when physical reports stop arriving. VIIPER feedback uses a buffered TCP reader; audio haptics are reduced in 5 ms windows while preserving peaks/transients, and adaptive-trigger effects bypass that aggregation.

The optional `--telemetry-json PATH` output includes p50/p95/p99 input latency,
physical and virtual rates, lost/coalesced reports, CPU, working set and both
top-level and backend-specific initialization stages. A hardware validation
session with active trigger movement measured 0.52% engine CPU, about 12.4 MiB
engine working set, 0.635 ms p99 report latency, no lost report and 0.794 s
initialization. A separate live process sample measured about 39.2 MiB total
working set for the engine, recovery watchdog and VIIPER together.

The official in-process `libVIIPER v0.7.0-asb5` backend removes process and
TCP-control overhead while keeping USB/IP traffic loopback-only. On the APEX 5
test system, a stabilized hardware launch completed in about 0.28 s versus
about 0.58 s for the sidecar, attached the virtual device in about 1 ms and
delivered roughly 800 Hz verified virtual HID input with no lost physical
reports. A warm runtime sample used about 18.3 MiB total working set versus
33.4 MiB for the engine plus sidecar. These figures are development benchmarks,
not guarantees across Windows and usbip-win2 versions.

## Failure recovery and uninstall

The bridge snapshots the complete existing HidHide configuration. After an engine crash during a Playnite session, the watchdog deliberately keeps the APEX hidden until Playnite signals that the game has stopped, then restores it. An HKCU RunOnce marker covers power loss or logout. Manual recovery is available from the control panel or:

```text
ApexSenseBridge.exe restore-controller-visibility
```

Normal uninstall sends an acknowledged maintenance-stop request and waits for input neutralization, virtual-device detach and APEX visibility restoration; forced termination is only a fallback for a hung engine. It then removes the extension, profiles, logs, recovery entries, files and registry records, followed by only the dependencies installed by ApexSenseBridge. Drivers that predated ApexSenseBridge are preserved. The control panel offers an explicit full-removal action for users who also want those pre-existing dependencies removed.

## Developer build

Requirements:

- Windows 10/11 x64;
- Visual Studio 2022 Build Tools with Desktop C++ and a Windows SDK;
- CMake;
- Go for rebuilding the pinned VIIPER sidecar; the integrated builder downloads
  its own pinned Go and LLVM-MinGW toolchains after verifying their hashes;
- Inno Setup 6 for the final installer.

Build the native targets and tests:

```powershell
.\scripts\build-windows.ps1
ctest --test-dir .\build-win -C Release --output-on-failure
```

Clean generated workspace content without touching `dist/` or private notes:

```powershell
# Preview every removal first.
.\scripts\clean-workspace.ps1 -IncludeBuildOutputs -IncludeToolCache -IncludeScratch -WhatIf

# Remove temporary sources, verification builds, C# bin/obj folders and logs.
.\scripts\clean-workspace.ps1
```

Use `-IncludeBuildOutputs` to also remove `build-win/`, `-IncludeToolCache` for
the downloaded libVIIPER toolchains, and `-IncludeScratch` for local scratch
work. Release artifacts in `dist/` and internal documents in `notes/` are
always protected.

Rebuild both pinned VIIPER payloads and create the offline installer plus
portable ZIP:

```powershell
.\scripts\build-viiper-windows.ps1
.\scripts\build-libviiper-windows.ps1
.\scripts\build-installer.ps1
```

Release builds are Authenticode-signed and RFC 3161-timestamped with SHA-256.
For GitHub Actions, configure the repository secrets
`ASB_SIGNING_CERTIFICATE_BASE64` (the base64-encoded PFX) and
`ASB_SIGNING_CERTIFICATE_PASSWORD`; the release workflow refuses to publish an
unsigned build. For a signed local build, set those same environment variables,
or set `ASB_SIGNING_CERTIFICATE_PATH` to a PFX path, then run:

```powershell
.\scripts\build-installer.ps1 -Sign
```

Certificates held in the Windows certificate store or on a compatible hardware
token can instead be selected with `ASB_SIGNING_CERTIFICATE_THUMBPRINT`. The
engine, control panel, Tray app, VIIPER payloads, Playnite assembly, installer
and generated uninstaller are all signed before packaging. PFX and P12 files
are ignored by Git and must never be committed.

The official backend is the pinned in-process `libVIIPER v0.7.0-asb5` build.
It loads inside the engine and falls back automatically to the validated
`v0.7.0-asb3` sidecar when the DLL or its ASB exports are unavailable. Both
artifacts use the same source patch containing the complete adaptive-trigger,
grip-rumble and audio-haptics protocol required by the bridge.

The ASB-only integrated server entry accepts only IPv4/IPv6 loopback clients;
the upstream public `NewUSBServer` behavior is unchanged. The integrated build
is reproducible from pinned Go and LLVM-MinGW archives with SHA-256 verification.
The fallback sidecar can also be built into `dist\experimental` for comparison:

```powershell
.\scripts\build-viiper-070-windows.ps1
```

The installer, portable ZIP and GitHub release workflow package both
`libVIIPER.dll` and `viiper.exe`. Promotion follows complete Call of Duty and
Spider-Man 2 regression passes, including full input, adaptive triggers, grip
rumble, audio haptics and Spider-Man 2 touchpad gestures.

The Playnite build script uses an installed Playnite SDK when available. On a clean CI worker it downloads the official pinned `PlayniteSDK 6.16.0` NuGet package, verifies its SHA-256 and creates the standard ZIP-based `.pext` format without requiring a full Playnite installation.

Release artifacts are written to `dist/`:

```text
ApexSenseBridge-Setup.exe
ApexSenseBridge-Portable.zip
ApexSenseBridgeTray.exe
ApexSenseBridge_e41b1737-6753-4b59-bc65-4fdd6a7df7f4_0_6_0.pext
SHA256SUMS.txt
```

## Useful diagnostic commands

```text
list
diagnose [--all-hid] [--json]
identify [index]
input-status [index] [--seconds N] [--json]
virtual-ds [--seconds N] [--json] [--viiper PATH]
           [--virtual-backend auto|integrated|sidecar]
bridge-triggers [index] [--seconds N] [--viiper PATH]
                [--virtual-backend auto|integrated|sidecar]
                [--telemetry-json PATH] [--xinput-index 0..3]
                [--rumble] [--haptic-threshold 0..95]
                [--verify-virtual-input] [--touchpad-profile NAME]
                [--view-hold-swipe-up] [--session-token 32HEX]
test-rt [index]
test-rumble [index]
clear [index]
restore-controller-visibility
```

`bridge-triggers` always enforces full proxying and physical isolation in 0.6.0, including when legacy `--proxy-xinput` or `--isolate-apex` flags are omitted. A session failure is fail-closed: the game is never allowed to fall back to a visible physical APEX during a DualSense profile.

`--virtual-backend` is an advanced validation switch. Normal users should leave
the default `auto`; `integrated` and `sidecar` force one implementation so the
same session can be benchmarked without changing game or Playnite settings.

`--touchpad-profile` accepts `none`, `spider-man-2`, `miles-morales`,
`ghost-of-tsushima`, or `warframe`. The old `--view-hold-swipe-up` switch remains
available only for command-line compatibility; new integrations should select
an explicit game profile. In Spider-Man 2, successive long D-pad Up presses
automatically alternate swipe up and swipe down so the camera opens and closes
like the original single Xbox control; short D-pad presses are preserved.

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

## Acknowledgments & AI-Assisted Development

This project was built with the assistance of agentic AI coding tools within an IDE environment, combining rapid reverse-engineering prototyping, hardware protocol analysis, and modern C++20 / C# WPF development.

See `THIRD_PARTY_NOTICES.md` and the licenses installed with the application for upstream attribution and redistribution terms.
