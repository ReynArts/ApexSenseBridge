# Changelog

## 0.5.0

- Promoted the in-process `libVIIPER v0.7.0-asb5` backend to the official
  default after complete Call of Duty and Spider-Man 2 hardware validation.
  The release payload still contains the validated `asb3` sidecar and falls
  back to it automatically when the DLL or its ASB exports are unavailable.
- Added an advanced `--virtual-backend auto|integrated|sidecar` selector for
  controlled A/B validation. `auto` prefers the integrated DLL and retains the
  sidecar as its compatibility fallback.
- Hardware-validated asb5 in Call of Duty with full-range sticks and triggers,
  active adaptive-trigger and grip-rumble feedback, no lost input or write
  failures, and 0.671-1.545 ms p99 forwarding latency. Spider-Man 2 additionally
  validated touchpad taps plus up/down/left swipes and active audio haptics.
- Added the integrated DLL to the installer, portable ZIP and release CI while
  keeping `viiper.exe` beside it for automatic recovery.
- Removed the integrated backend's roughly two-second USB/IP attach penalty.
  Its private server now binds an ephemeral TCP port in the form expected by
  usbip-win2, while an ASB-only accept guard rejects every non-loopback client
  before any USB/IP payload is read. On the APEX 5 test system, a stabilized
  hardware launch fell from about 2.33 s to 0.28 s total initialization, with
  device attachment near 1 ms, roughly 800 Hz virtual HID input and no lost
  physical reports.
- Added per-stage virtual-backend initialization telemetry. Bootstrap, server,
  bus, device, feedback and initial-input timings are now present in both the
  console summary and telemetry JSON, making integrated/sidecar startup costs
  directly comparable.
- Fixed final telemetry for the automatic integrated/sidecar backend selector.
  Closing a session no longer discards the backend name, input/output counters
  or audio-haptics counters before the summary and JSON report are generated.
  With virtual-input verification enabled, the virtual report rate now uses
  the actual observed HID stream instead of only changed-state submissions.

## 0.4.0

- Removed the obsolete Spider-Man 2 WGI compatibility override completely:
  the CLI switch, registry mutation, recovery marker and Steam-closed launch
  requirement are gone. The profile keeps its verified touchpad gestures and
  works through the same path in Playnite and automatic standalone detection.
- Made Spider-Man 2's camera remapping behave like the original Xbox control:
  successive long D-pad Up presses now alternate touchpad swipe up (open) and
  swipe down (close), while short presses remain unchanged. The behavior is
  automatic in both Playnite and the standalone detector.
- Promoted the post-0.3.0 VIIPER `v0.7.0-asb3` backend to the official release
  payload after Call of Duty and Spider-Man 2 validation. The reproducible build
  kept the upstream libVIIPER output API intact, and isolated the complete
  adaptive-trigger/audio-haptics stream behind an ASB extension. The port adds
  composite USB audio, USB/IP isochronous transfers, old-wire input adaptation,
  firmware `0x0630`, usbip-win2 ABI fallback and regression tests. Bare upstream
  VIIPER builds are now rejected because they omit the required feedback fields.
  The second prototype restores the exact 273-byte standard DualSense HID
  descriptor instead of v0.7's shared 427-byte descriptor containing Edge-only
  reports, after Call of Duty rejected the inconsistent standard-controller
  identity.
- Fixed COD's 48-byte HID `SET_REPORT` path in `asb3`. The preceding prototype
  removed report ID `0x02` only from padded 64-byte writes, shifting flags,
  motor values and both adaptive-trigger payloads by one byte for short writes.
  Added an exact 48-byte regression fixture covering every routed field. The
  resulting prototype is validated in Call of Duty with full virtual input and
  feedback; the apparent right-stick regression was COD's in-game Aiming Input
  Device left on Mouse, not an input-proxy defect.
- Completed the VIIPER `v0.7.0-asb3` in-game regression pass in Spider-Man 2:
  lossless full input, 94 active adaptive-trigger effects, peak-preserving
  audio-haptics routing, clean motor/trigger shutdown and exact touchpad-click
  hold preservation. The release builder, installer, portable ZIP and GitHub
  workflow now all consume the promoted v0.7 backend.
- Fixed near-continuous false grip rumble in Call of Duty by no longer treating
  DualSense `HAPTICS_SELECT` alone as validation of the compatibility-motor
  bytes. Only `COMPATIBLE_VIBRATION` or `COMPATIBLE_VIBRATION2` now updates the
  APEX motors, matching Sony's Linux driver semantics.
- Added automatic Playnite profile selection from normalized game titles and
  installation-folder names. Only the four verified games are recognized;
  unknown titles remain native XInput. Manual profiles take priority, explicit
  per-game disable is now persistent, and both a global toggle and a
  restore-automatic menu action are available.
- Fixed the Playnite startup path so it actually resolves automatic profiles
  instead of consulting only manually saved profiles. Playnite and the
  standalone tray now pass the same explicit per-game touchpad profile into the
  engine; this also removes the contradictory legacy Spider-Man arguments.
- Replaced the global View-hold/up-swipe shortcut with conservative per-game
  touchpad profiles for Spider-Man 2, Miles Morales, Ghost of Tsushima and
  Warframe. The mapper now supports holds, modifier chords, four swipe
  directions, source-input consumption, safe short-tap replay, per-direction
  telemetry and an explicit `none` fallback. The legacy CLI switch remains
  accepted, while Playnite exposes the four verified profiles.
- Locked DualSense profiles to a mandatory full-input proxy: the physical APEX
  is hidden before `Ready`, all controls transit through ApexSenseBridge, and a
  proxy/isolation failure cancels the Playnite launch. Unconfigured games keep
  native XInput and start no bridge process.
- Added `PhysicalInputSource`, overlapped event-driven HID discovery and a
  lossless internal XInput fallback associated by APEX VID/PID. Replaced the
  fixed 4 ms loop with events/high-resolution waits and allocation-free mapping.
- Added buffered VIIPER TCP feedback, 5 ms peak-preserving audio aggregation,
  initialization-stage metrics and optional JSON latency/CPU/memory telemetry.
- Removed the remaining Playnite launch pause by bounding the initial loopback
  `connect()` itself; socket send/receive timeouts did not cover that call.
  Also reduced the pre-VIIPER audio snapshot to endpoint IDs and resolves slow
  topology properties only for newly-created endpoints in the background.
- Added the single offline Inno Setup installer, pinned usbip-win2 0.9.7.7 and
  HidHide 1.5.230 payloads, exact dependency ownership metadata, static MSVC
  runtime, a non-resident Win32 control panel and full uninstall/recovery paths.
- Prevented setup from entering usbip-win2's potentially hanging nested
  uninstaller. Healthy ABI-compatible 0.9.7.5-0.9.7.7 drivers are preserved;
  unsupported or damaged packages are rejected with repair instructions.
  Fresh installs are bounded, logged and verified, and releases also include a
  standalone portable ZIP with a one-time guarded driver helper.
- Added an acknowledged global maintenance-stop event. Uninstall now waits for
  virtual-input neutralization, VIIPER detach and controller-visibility/HidHide
  restoration before
  using `taskkill` only as a bounded fallback for a hung engine.
- Kept HidHide fail-closed after an unexpected engine exit: the recovery
  watchdog now waits for Playnite's game-stop signal before exposing the APEX
  physical interfaces again.
- Removed the engine path and XInput-index choices from the Playnite UI. The
  engine is resolved from HKLM and legacy fields are accepted only for migration.
- Fixed a Playnite Fullscreen race where residual `A/Cross` input could relaunch
  a game after it stopped. Shutdown now publishes a neutral virtual state,
  waits for physical release before restoring visibility, and debounces a
  duplicate same-game startup for four seconds.
- Made the Playnite release build independent of a local Playnite installation
  by pinning/verifying PlayniteSDK 6.16.0 and packaging the official ZIP-based
  `.pext` format in CI.
- Completed the `0.3.0` virtual DualSense output-capture milestone.
- Added an isolated `VirtualDualSense` interface and Windows VIIPER backend.
- Added `virtual-ds [--seconds N] [--json] [--viiper PATH]` with a static neutral
  input state and explicit `apex_routing=disabled` behavior.
- Added compact DualSense HID-output/audio-haptics frame decoding and counters
  for output, adaptive-trigger, rumble, audio, malformed, and unknown frames.
- Added compatible patched-VIIPER version checks, usbip-win2 missing-driver
  detection, sidecar launch, and clean stream/device/bus teardown.
- Added platform-independent protocol tests and a Windows fake-VIIPER lifecycle
  integration test. Real driver-backed enumeration remains to be validated.
- Built and started the real `v0.6.1-steamless9` sidecar from its pinned source.
- Updated the virtual DualSense firmware feature report from obsolete `0x0224`
  to current `0x0630`, matching VIIPER v0.7.0, and added runtime verification
  telemetry so native games can no longer silently reject adaptive feedback as
  outdated controller firmware.
- Installed the signed usbip-win2 `0.9.7.7` driver after creating a Windows
  restore point; deliberately avoided the officially warned-against `0.9.7.8`.
- Hardware-validated the virtual DualSense as Sony `VID 054C`, `PID 0CE6`,
  `MI_03` and captured output reports with clean device/bus teardown.
- Added the OpenFlydigi DualSense-to-FORCEADAPT trigger translation, guarded
  APEX routing, effect deduplication, detailed telemetry and automatic reset.
- Added the full-input proxy foundation so games associate actions and
  adaptive-trigger output with the same virtual DualSense.
- Hardware-validated that proxy mode makes Spider-Man 2 emit active type-33
  trigger effects, translated to APEX command 81 with no write failures.
- Initially used a 4 ms submission cadence to keep the emulated DualSense
  counter and sensor timestamp advancing; the current implementation is
  event-driven with keepalive only when physical reports stop.
- Added explicit `--isolate-apex` routing through the official signed HidHide
  driver. It targets only the selected controller's HID-game and XInput
  interfaces, preserves the FORCEADAPT handle, and is implied by the
  Spider-Man 2 profile.
- Added exact HidHide configuration snapshot/restore, an independent crash
  watchdog, an HKCU RunOnce power-loss fallback, and the manual
  `restore-controller-visibility` recovery command.
- Installed signed HidHide 1.5.230 after validating the Nefarius installer,
  MSI, Microsoft-signed driver and catalog. Its broken optional updater action
  was skipped with an external MSI transform; Windows uninstall registration
  remains present.
- Hardware-validated the complete Spider-Man 2 profile: all controls, adaptive
  triggers and touchpad gestures work when Steam is already running.
- Added optional standard DualSense rumble routing to the APEX grip motors via
  Flydigi command `0x12`, preserving low/high-frequency motor ordering. The
  route coalesces unchanged levels, reports detailed telemetry and always
  attempts a zero-motor command during shutdown.
- Added a gentle isolated `test-rumble` hardware command, extended `clear` to
  stop grip rumble, and added a dedicated rumble bridge test (7/7 tests pass).
- Hardware-validated Flydigi rumble command `0x12`, then added native DualSense
  audio-haptics translation using VIIPER's 5 ms energy/peak/transient windows.
  The route preserves stereo channels, mixes with standard rumble, coalesces
  small changes, caps writes at 200 Hz, exposes signal telemetry and fails safe
  to zero if the audio stream becomes stale.
- Corrected `dualsense_rumble_reports` to count only reports whose DualSense
  motor-enable flags request an update, excluding residual non-zero bytes.
- Added the Playnite-ready session IPC foundation: a validated 128-bit token,
  manual-reset Windows `Ready`/`Stop` events, a versioned 512-byte shared status
  block, initialization failure signalling, graceful stop polling and stable
  error messages. The Windows lifecycle test brings the suite to 9/9 tests.
- Implemented the official Playnite GenericPlugin (`playnite/ApexSenseBridge`)
  targeting .NET Framework 4.6.2 and the installed Playnite 10 SDK 6.16. It
  hooks `OnGameStarting`, `OnGameStopped`, `OnGameStartupCancelled`, and
  `OnApplicationStopped` to handle bridge launch, readiness verification, and clean
  shutdown without killing processes.
- Added per-game profile selection via Playnite context menu (DualSense standard,
  Spider-Man 2, and Disabled), showing the active mode with selection
  checkmarks and persistent configuration
  storage in Playnite's user data directory.
- Added a full WPF settings interface (`ApexSenseBridgeSettingsView.xaml`) with
  automatic installation status, rumble toggle, haptic threshold slider,
  timeout, and configured game profile overview.
- Added the automated extension packaging script (`scripts/build-playnite-extension.ps1`)
  producing `.pext` packages via Playnite `Toolbox.exe`.
- Added a configurable audio-haptics activation gate after the first physical
  Spider-Man 2 test showed that thousands of very short texture pulses feel
  nearly continuous on conventional APEX motors. The 12% default suppresses
  low-level texture and re-expands the remaining dynamic range; users can tune
  `--haptic-threshold 0..95`. Added low/medium/high frame buckets, active duty
  percentage and runtime telemetry.
- Hardware-validated the 12% haptic gate in Spider-Man 2: active audio windows
  fell from 35.65% to 5.07% while strong intensity variations remained.
- Fixed the XInput View/Back mapping for native DualSense games: it now emits
  touchpad click (`0x0002`) instead of Create/Share (`0x1000`), restoring the
  Spider-Man map action. Added a complete pure mapping test (10/10 tests).
- Added the read-only `xinput-view-test` diagnostic and bridge-side hold
  telemetry. Hardware measurements confirmed that a 6.465-second View hold
  reaches the virtual DualSense HID report without being shortened.
- Initially added `--view-hold-swipe-up` gesture emulation for controllers with
  no touch surface. It remains accepted for command-line compatibility, but the
  per-game mapper above now supersedes it. The codec carries both VIIPER touch
  contacts and the native suite now covers the profile state machines.
- Made VIIPER logging optional when an existing log file is inaccessible, so
  a logging ACL mismatch cannot prevent virtual-controller startup.
- Added automatic Windows default-playback protection around virtual DualSense
  creation. The bridge snapshots all playback roles, recognises only a new Sony
  `054C:0CE6 MI_00` audio endpoint, and restores the previous output only when
  Windows redirected a role to it. The DualSense endpoint remains enabled for
  haptic audio, with explicit status telemetry and a pure identity-guard test
  (11/11 tests pass).
- Hardware-validated the audio guard on Windows: VIIPER created an active
  `Wireless Controller` render endpoint, all three default playback roles were
  restored, and haptic-audio frames continued to arrive.
- Added strictly read-only `diagnose`, `diagnose --all-hid`, and `diagnose --json` commands.
- Added extended SetupAPI diagnostics: serial, feature-report length, hardware and compatible IDs, instance and parent IDs, class, friendly name, and USB `MI_xx` interface number.
- Added unit tests for HID relevance filtering and text/JSON formatting.
- Build script now clears CMake caches that still reference a previous source directory.
- Added Flydigi command `0x01` identity verification and an `identify` command.
- FORCEADAPT writes are now refused until the open device reports an Apex 5 `k5` DeviceType.
- Added bounded overlapped HID reads for reply handling without busy polling.
- Hardware-validated the Windows dongle path on an Apex 5 DeviceType `128`.
- Hardware-validated a gentle RT command `81` effect and automatic LT/RT reset.

## 0.2.2
- Fix MSVC build failure by including `<iterator>` for `std::back_inserter`.
- Build script now auto-detects Visual Studio Build Tools 2026 or 2022.
- Automatically clears a stale CMake generator cache when switching Visual Studio versions.

## 0.2.0

- Refactored the prototype into protocol / device / platform layers.
- Corrected the vendor HID transport framing to the measured `0x03 0x5A 0xA5` interface used by the APEX 5 command collection.
- Added strict APEX-family candidate filtering: VID `0x37D7`, PID family `0x2xxx`, usage page `0xFFA0`.
- Added native Windows HID enumeration using SetupAPI + HID APIs; no third-party runtime dependency.
- Added direct output-report transport with `WriteFile` and `HidD_SetOutputReport` fallback.
- Added `list`, `test-rt`, `clear`, and `dry-run` commands.
- Added a gentle physical RT test with automatic LT/RT reset.
- Added Ctrl+C-aware test loop and RAII cleanup.
- Expanded protocol tests.
