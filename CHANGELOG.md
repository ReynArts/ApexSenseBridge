# Changelog

## 1.0.0-beta.11

### APEX 4/5 grip vibration gain ([#25](https://github.com/ReynArts/ApexSenseBridge/issues/25))

- Extend conventional grip vibration strength to 0–200% in CLI, verified-model
  Tray profiles and Playnite settings. Preserve the 100% default and the entire
  previous 0–100% mapping. Amplify both HID rumble and audio-derived grip output
  after mixing/gating, saturating at 255 rather than overflowing motor bytes.
  Silence and explicit stop commands remain zero.
- Keep trigger strength at 0–100%, with no changes to trigger resistance,
  translation, travel, frequency or timing. The audio activation threshold still
  filters only audio-derived grip output; it never gates standard HID rumble.
  Amplification cannot recover a signal already removed by that threshold.
- Preserve independent controller profiles and offline UI locking. APEX 6
  profiles retain their 0–100% common-strength range; legacy CLI/Playnite strength
  above 100% is capped only after verifying APEX 6 hardware. Its separate native
  PCM gain/renderer remain unchanged.
- Add localized guidance on moderate increases and the continuing influence of
  Space Station motor intensity. No firmware/profile overwrite, automatic gain,
  per-game intensity, new version or release package is introduced.
- **Far Cry 6 resistance report remains unverified:** tests compare identical
  LT/RT resistance, weapon/recoil and stop commands at audio thresholds 0% and
  95%, including amplified grip output. Request the exact slider/version,
  connection/firmware and a reproducible scene with `tray_bridge.log` before
  claiming a fix for the reported loss of trigger resistance.

### Controller-specific Tray calibration

- Lock controller strength, grip vibration, threshold, gyroscope and RGB controls
  until a single supported controller is connected and its identity is verified.
  Keep general Tray preferences available offline. Relock on disconnect, changed
  HID interface, failed identification or ambiguous multiple-controller detection.
- Save separate APEX 4/5/6 calibration profiles in the existing settings file.
  Migrate existing strength/rumble preferences without resetting them; restrict
  gyro tuning to APEX 4, RGB to APEX 5 and retain fixed zero threshold on APEX 6.
  Settings apply to the next Tray session, not a running bridge.
- Select calibration in the engine **after hardware identity verification**,
  before configuring output. Do not trust the last UI model during hot swaps;
  never fall back to another model's supplied profile. Existing CLI/Playnite
  launches without model profiles retain their previous behavior.
- Fix the detection process timeout by draining stdout/stderr asynchronously;
  fingerprint the single vendor HID interface to avoid retaining a replaced device.
  Reverify idle receivers whose HID path stays present while the controller sleeps.
  During a bridge, use its live verified IPC identity instead of competing HID reads.

### APEX 4 delayed trigger feedback investigation ([#23](https://github.com/ReynArts/ApexSenseBridge/issues/23))

- Fix a Tray dashboard initialization crash found in the reporter's logs:
  ignore navigation/status updates raised while XAML controls are still being
  created, and guard the game-title control during Starting/Failed phases.
- Serialize the APEX 4 async writer's pacing check with synchronous vendor
  writes. Keep the existing 25 ms pacing and latest-state LT/RT/rumble
  coalescing; do not change trigger translation or APEX 5/6 output behavior.
- Add shutdown diagnostics for async write attempts, replaced pending updates,
  writes taking at least 100 ms, and maximum queue/HID-write durations in
  microseconds. Successful writes may still be slow; zero write failures alone
  does not establish low output latency.
- Add a blocked-write regression with 200 trigger/rumble updates: feedback
  capture remains non-blocking and only the latest pending states, including
  recoil cancellation, are sent after the blocked write completes. Add 12 WPF
  initialization/phase/tab regression scenarios.
- **Still requires hardware validation:** the reporter confirms beta.10 fixes
  detection and input lag, but delayed recoil in Cyberpunk is not yet reproduced
  or confirmed fixed. Retest with the instrumented build, stop the bridge
  cleanly, and collect `tray_bridge.log` to distinguish queue delay from slow HID
  writes. No new release package or version change is included in this work.

### APEX 6 Pro native grip intensity ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))

- Automatically bypass the envelope activation threshold on APEX 6 Pro (effective
  0), including launches from Tray/Playnite that pass the shared APEX 4/5 setting.
  Preserve that saved setting, the conventional-motor filtering, HID rumble and
  the remaining envelope mapping. Native PCM already bypasses gating since beta.10.
  Diagnostics expose the effective zero threshold; UI hints clarify model scope.
- Prepare experimental CLI-only `--apex6-haptic-gain 100..200` for quiet native
  grip PCM. Default 100 remains bit-identical to beta.10. Higher settings use
  continuous amplitude companding, preserving zero/sign and full-scale endpoints
  without hard clipping, rather than multiplying every game by two. This changes
  amplitude dynamics, not actuator calibration, and requires hardware comparison.
- Restrict the gain option to a verified APEX 6 Pro with `--rumble`; ordinary
  vibration strength/mute still applies afterwards. Gain does not affect trigger
  rendering, envelope fallback, HID rumble, stereo routing or frame rate. APEX 4/5
  behavior remains unchanged.
  Startup and JSON diagnostics record the selected PCM gain. No UI preset,
  automatic gain, frequency remapping or firmware/driver changes are added.
- Beta.10 feedback confirms restored Endfield trigger consistency and good
  PRAGMATA behavior on the reporting tester's setup, while ZZZ grip intensity
  remains weak on another setup. Do not treat those results as universal
  compatibility or as proof that lowering the threshold changes native PCM.
  See the updated hardware comparison checklist. Existing beta.10 packages are
  not replaced by this unreleased adjustment.

## 1.0.0-beta.10

### Open-issue fixes

- **Apex 4 gyro completion ([#10](https://github.com/ReynArts/ApexSenseBridge/issues/10))**:
  preserve the capture-derived calibration at 100% while exposing bounded
  25–400% global sensitivity and a separate yaw correction in the engine,
  Tray and Playnite. This addresses the hardware validation result that all
  axes work in game but remain weak, especially yaw, without pretending the
  available free-hand captures provide factory calibration.
- **Stale external session after startup timeout ([#15](https://github.com/ReynArts/ApexSenseBridge/issues/15))**:
  after a cooperative stop timeout, forcibly reap only the exact engine child
  launched by Tray or Playnite. A failed initialization can no longer discard
  its process handle while the orphan keeps the global session lock.
- **Apex 4 degraded USB identity ([#26](https://github.com/ReynArts/ApexSenseBridge/issues/26))**:
  classify the reported 32-byte output interface separately from the full
  64-byte trigger interface. `identify` and bridge startup now state that HID
  writes can succeed while RT remains unavailable, and advise reconnecting
  instead of falsely reporting complete adaptive-trigger support.

### APEX 6 Pro fixes ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))

- **Known Pro variants**: accept the official `0x98` Phantom Blade Zero identity
  alongside `0x96`, retaining checksum and grip/trigger capability checks.
  Non-Pro `0x95` and unknown models remain refused before motor writes. A valid
  but unsupported identity reply is reported explicitly instead of a timeout.
- **Simultaneous trigger carriers**: continuous native feedback/vibration uses
  a shared 1 kHz sample clock, including muted zones and independently installed
  effects. Identical effects no longer remain phase-shifted after asymmetric
  presses and unnecessarily fall back to alternating half-duty routing.
  Different effects/strengths still require the protocol's alternating route.
  This fixes a reproducible renderer defect; Endfield's reported progressive
  weakening still needs an on-controller before/after comparison.
- **Native Bow `0x22`**: decode the captured zones and independent draw/snap
  strengths. Render a bounded draw-progress texture and a single 16 ms snap,
  rearmed after release, including start zone zero. A held trigger does not
  repeat the snap. This is a tactile approximation, not mechanical resistance.
- **Quiet native grip haptics**: preserve native PCM at unity gain instead of
  discarding each quiet 8 ms block below the global audio activation threshold.
  The threshold still controls envelope fallback; strength/mute settings still
  apply. No automatic boost, clipping, speaker-audio substitution or firmware
  change is introduced. Historical PCM threshold-drop counters remain zero;
  telemetry identifies the new `native-pcm-preserved` policy.
- **Windows audio preflight**: inspect the newly observed virtual controller's
  mix channels and speaker masks before announcing APEX 6 PCM-session readiness.
  Stereo, unknown and missing endpoints produce actionable quadriphonic-setup
  guidance and diagnostics. Inspection is read-only; ASB does not overwrite
  Windows audio formats or select the virtual controller as the default output.
  Automatic quadriphonic configuration and live game-audio reacquisition after
  bridge recreation are not claimed fixed.

### Release identification and validation

- **Catalogue packaging regression**: restore the 29 manual-fix flags and
  PCGamingWiki guidance URLs lost in the supported-games merge, from the last
  pre-merge catalogue. Current games, capabilities, profiles, covers and
  executable mappings are preserved; no new compatibility claim is invented.
- Engine help, Windows product versions, Tray/Playnite informational versions
  and setup display identify `1.0.0-beta.10`; numeric `1.0.0` versions, install
  identity and user settings remain compatible. Release checks verify the full
  candidate label as well as the numeric version. Setup and portable include
  the updated APEX 6 validation checklist under `Docs`.
- APEX 4/5 input, FORCEADAPT translation, rumble and audio timing paths remain
  unchanged. The multichannel preflight is requested only for APEX 6 grip-PCM
  sessions. Hardware/game validation and clean-VM installation remain release
  gates; automated tests do not establish physical fidelity.

## 1.0.0-beta.9

### Touchpad & Motion

- **Death Stranding 2 touchpad remapping ([#5](https://github.com/ReynArts/ApexSenseBridge/issues/5))**:
  `View/Back` sends a right-side touchpad click immediately and preserves the
  physical hold duration for Likes, communication and changing the Like icon.
  `LB + Menu` sends a left-side click for Photo Mode, consuming the chord until
  both buttons are released. Automatic Tray/Playnite selection and manual
  profile selection are supported; older standard catalogue entries inherit
  the embedded remapping. Other games remain unchanged. In-game validation
  is still required.

- **Experimental APEX 4 motion decoding**: use the native signed 16-bit gyro
  fields found in the 2026-09-30 USB and dongle captures, including split yaw
  bytes 18/20, instead of firmware mouse deltas. Correct accelerometer axis
  orientation, add captured-report regression tests, and clarify diagnostic
  yaw/roll instructions. Gyro gains are empirical estimates, not factory
  calibration; on-device direction/sensitivity and in-game behavior still
  require tester validation. See `APEX4_GYRO_VALIDATION.md`.

### Tray application and shared features — APEX 4/5/6

- **Guided disconnection recovery**: after a confirmed runtime controller or
  virtual-stream disconnection, or physical-input stream loss (including sleep
  with a dongle still connected), Tray pauses automatic activation and offers a
  voluntary bridge restart with the original game, profile and APEX slot.
  Controller identity, virtual DualSense readiness, HidHide isolation and final
  runtime readiness are displayed from token-scoped structured IPC. A failed
  retry remains paused; dismissing the notice does not silently restart it.
  Games are never terminated or relaunched automatically and may need restarting
  to reacquire the recreated DualSense. Playnite-owned interruptions remain
  owned by Playnite and display guidance rather than a Tray takeover.

- **Tray game settings and session status**: optional per-game executable path,
  learned-path choices and exact configured-path detection; explicit session
  phases, owner, controller, game/profile and refusal/stop reasons, including
  sessions managed by the updated Playnite extension. Prepared `.exe` launching
  is now in controller diagnostics rather than the dashboard.
- **Controller feel**: global Tray trigger/vibration strength (0–100%) and
  audio-haptic threshold (0–95%), applied at next session startup on APEX 4/5/6.
  Output scaling preserves effect travel, timing and frequency; zero strength
  stops the corresponding effect. APEX 6 PCM channels now honor the threshold.
- **Clear main entry point**: launching the engine without arguments opens the
  Tray application. Windows file descriptions, installer shortcuts and usage
  distinguish the main Tray application from the command-line engine.

### APEX 6-specific fixes

- **APEX 6 trigger-vibration decoding ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))**:
  reads native `0x26` frequency from effect byte 9, not reserved byte 10. Empty
  native zone masks and zero-frequency vibration are valid stop requests rather
  than rejected effects. This corrects a confirmed beta.8 decoding defect;
  Endfield's zipline effect still needs an in-game hardware retest.
- **APEX 6 feedback evidence**: keeps a bounded 64-transition trace with raw
  trigger bytes, elapsed time, side, position and acceptance status, plus the
  last active/rejected request for each side even after an off command. Reports
  malformed parameters separately from unsupported types and rejection-induced
  stops separately from explicit stops. Adds pre-filter 48 kHz audio peaks/RMS,
  filtered active-block RMS, thresholded channels and active queue-drop counts.
  The optional audio trailer is backward compatible; its collection is limited
  to waveform-enabled sessions. The integrated backend identifies its actual
  ASB patch version instead of printing a hard-coded label. No PCM gain, USB
  descriptor, driver installation or APEX 4/5 motor routing is changed by these
  fixes. The reported ZZZ freeze remains unconfirmed and is not marked fixed.

### APEX 4/5-specific fixes

- **APEX 5 simultaneous LT/RT stream guard**: combined-axis HID sessions now
  verify a live independent operator stream before announcing readiness, even
  when the requested onboard profile is already active. Missing or stalled
  streams receive one temporary raw-routing restart at startup and one at
  runtime; settings are snapshotted before changes and restored by the existing
  session/watchdog recovery. Runtime recovery neutralizes virtual input first;
  repeated loss stops cleanly instead of silently canceling aim while firing.
  Cached vendor pairs expire, mapped Space Station controls remain authoritative,
  and separate-axis, APEX 4/6 and explicit XInput sources are unchanged. Recovery
  attempts are logged. Call of Duty hardware validation is still required.

- **Adaptive-trigger translation fixes for Horizon reports on APEX 4/5 ([#22](https://github.com/ReynArts/ApexSenseBridge/issues/22))**:
  corrects defects in the existing adaptive-trigger support identified while
  investigating continuous left-trigger twitching in Horizon Zero Dawn
  Remastered and missing left-trigger effects in Horizon Forbidden West on
  APEX 5. Valid left-trigger vibration effects are no longer discarded or tied
  to grip rumble; native effect zones and strengths are decoded consistently
  for LT and RT, and empty native effects or zero-frequency vibration reset
  the corresponding trigger to Normal. Native weapon effects use the length
  of the resistance interval rather than its absolute end position, matching
  Space Station's breakthrough parameter. These shared translation fixes apply
  to all games using the affected effects on APEX 4/5, rather than a
  Horizon-specific profile. The current APEX 4/5 mapping still approximates
  multi-zone effects with a single start and peak strength. Resolution of the
  reported Horizon symptoms remains pending hardware validation in both games.

## 1.0.0-beta.8

- **APEX 6 DualSense trigger fidelity ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))**:
  decodes native feedback, weapon, and vibration effects using their packed
  per-zone strengths. Frequency no longer raises trigger drive strength, weapon
  break is a short retriggerable pulse, and explicit off commands stop the
  corresponding actuator. The renderer remains specific to the APEX 6 voice
  coils; APEX 4/5 FORCEADAPT translation is unchanged.
- **APEX 6 haptic signal and session diagnostics**: reports actual trigger
  requests, supported effects, rumble updates, non-silent left/right PCM blocks,
  waveform peaks, output source, queue age, stale drops, route usage, and motor
  enable/disable counts. APEX 6 summaries no longer present inapplicable APEX
  4/5 effect and audio counters as zeros. The JSON telemetry gains an optional
  `apex6` object without changing the existing schema for other controllers.
- **APEX 6 grip routing**: a silent PCM channel no longer hides an active HID
  rumble request on that side. Blocks older than 24 ms are discarded rather
  than replayed after a transport stall.
- **Prepared game launch**: the Tray dashboard can select a game executable,
  wait for the bridge to become ready, then launch the game. Manual activation
  remains in effect until the user turns it off, accommodating launchers that
  exit before their child game process.
- **Experimental APEX 4 motion decoding ([#10](https://github.com/ReynArts/ApexSenseBridge/issues/10))**:
  decodes the legacy `04 FE` report's packed yaw/pitch rates, roll, and three
  accelerometer axes into the virtual DualSense input path. The capture tool
  now detects the firmware's all-zero IMU state and explains that gyro mapping
  must be enabled in the active onboard profile before recording.
- **APEX 5 Xbox Mode / AnyFSE trigger routing**: uses the controller's existing
  NewXInput `0xEF` vendor stream to recover confirmed simultaneous LT+RT presses
  in both Desktop and FSE. Mapped HID remains authoritative for profile-aware
  controls and either trigger used alone, while ordinary XInput remains a
  startup-only fallback and cannot collapse aim-and-fire input.

## 1.0.0-beta.7

- **APEX 5 simultaneous-trigger regression**:
  keeps mapped HID authoritative for buttons, sticks, and the D-pad while
  sourcing only LT and RT from the matching XInput device. Full-state XInput is
  now limited to startup when mapped HID has not produced its first report,
  preventing source alternation from releasing aim when fire is pressed.
- **APEX 6 idle-input resilience ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))**:
  polls the independent XInput trigger axes while mapped HID is quiet and
  publishes a bounded heartbeat. This prevents an otherwise healthy stationary
  controller from tripping the mandatory one-second input watchdog, as observed
  during PRAGMATA testing.
- **APEX 6 waveform continuity ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))**:
  replaces the single pending waveform slot with a bounded three-block,
  sequence-aware queue. Batched USB audio delivery can now be absorbed without
  unbounded latency, while gaps, duplicates, reordering, underruns, and genuine
  queue-overflow drops are reported separately.
- **Simultaneous-trigger diagnostics**: bridge session summaries and telemetry
  now record simultaneous LT/RT levels and report counts for both the physical
  input and virtual DualSense paths, making trigger-concurrency regressions
  directly observable in gameplay logs.
- **Bridge runtime modularization**: separates command-line option parsing,
  fixed-allocation runtime measurements, JSON support, and post-session
  telemetry from the hardware bridge command. The input-forwarding loop and
  its latency/button trackers remain allocation-free and inline, while new
  contract tests cover option compatibility, malformed numeric values,
  latency histograms, hold tracking, JSON escaping, and the telemetry schema.
- **Launcher configuration consistency**: Tray and Playnite now share one
  normalized bridge-argument builder, and manual forced-profile state is kept
  transient instead of being serialized into user settings. Production engine
  discovery no longer falls back to development build or distribution paths.
- **Portable-driver verification**: pins the portable HidHide dependency to
  the expected version and SHA-256 and verifies its product registration and
  service state before accepting or completing installation.
- **APEX 5 hardware regression validation**: an end-to-end dongle session
  sustained roughly 800 physical and virtual reports per second with no lost
  or coalesced reports. Simultaneous LT/RT reached 255 on both the physical and
  virtual paths, forwarding latency remained below 30 microseconds at p99, and
  virtual neutralization, trigger reset, profile recovery, and physical-device
  restoration all completed successfully.

## 1.0.0-beta.6

- **APEX 5 physical-input startup ([#15](https://github.com/ReynArts/ApexSenseBridge/issues/15), [#6](https://github.com/ReynArts/ApexSenseBridge/issues/6))**:
  standard controls now initialize from the mapped HID collection without
  waiting for the optional vendor motion stream. Vendor reports are merged as
  they arrive for gyro and accelerometer data, and the required raw-input
  transport is enabled temporarily with crash-safe restoration.
- **APEX 5 identity wake-retry resilience ([#17](https://github.com/ReynArts/ApexSenseBridge/issues/17))**:
  retries read-only APEX 5 identity verification commands across controller
  wake-up and benign detector races instead of aborting the session on the
  first missing reply.
- **APEX 5 mapped-HID XInput fallback ([#18](https://github.com/ReynArts/ApexSenseBridge/issues/18))**:
  adds a seamless XInput polling fallback if the primary mapped-HID collection
  times out, preventing input stalls while continuing to drain vendor motion
  reports.
- **APEX 6 independent trigger input ([#12](https://github.com/ReynArts/ApexSenseBridge/issues/12))**:
  preserves the event-driven mapped-HID controls while sourcing only LT and RT
  from the matching XInput device. This works around the controller's mapped HID
  collection collapsing simultaneous trigger presses, without changing the
  APEX 4 or APEX 5 input paths.
- **APEX 6 haptic strength and cadence**: expands grip waveform samples to the
  full safe signed 8-bit range, uses a high-resolution 125 Hz hardware deadline,
  and keeps VIIPER isochronous completion pacing on an absolute timeline rather
  than accumulating scheduler lateness. Integrated `libVIIPER v0.7.0-asb12`
  and sidecar `v0.7.0-asb10` contain the corrected pacing.

## 1.0.0-beta.5

- **APEX 6 hardware-validation follow-up**: corrects the reversed Flydigi HID
  LT/RT axis ordering shared with the APEX 5 mapped-HID path, drives the APEX 6
  trigger voice coils with bipolar AC instead of a DC force level, and reports
  the APEX 6 realtime routing state accurately in session diagnostics.
- **APEX 6 realtime haptic pacing**: paces each virtual DualSense isochronous
  audio transfer by its USB packet duration instead of completing every URB
  after a fixed 2 ms. This prevents the 1 kHz waveform from being consumed
  roughly six times too fast and losing most samples before the 125 Hz hardware
  writer can render them. The behavior is gated by the APEX 6 waveform opt-in;
  APEX 4/5 keep the original VIIPER path and timing. Diagnostics now report
  rendered waveform blocks and average/maximum APEX 6 HID write duration.
- **Upgraded VIIPER Sidecar & Library**: incorporates `libVIIPER v0.7.0-asb11`
  and sidecar `v0.7.0-asb9` with packet-duration USB pacing and refined
  decimated haptic stream delivery.

## 1.0.0-beta.4

- **In-app project support**: adds a dedicated, prominently placed Ko-fi card
  to the Tray settings while keeping donations visually and functionally
  separate from technical support.
- **Consent-driven bug reports**: adds a localized report window requiring only
  a title and explanation, with up to five optional screenshots, a previewable
  anonymized installation diagnostic, and a prefilled GitHub issue draft. The
  diagnostic reads only bounded local metadata when explicitly requested and
  adds no timer, background scan, bridge activity, or idle network traffic. The
  report entry uses the supplied vector bug icon and supports gamepad settings
  navigation.
- **Manual-fix game safety ([#14](https://github.com/ReynArts/ApexSenseBridge/issues/14))**:
  the PCGamingWiki synchronizer now imports
  `RequireManualFix` separately for adaptive triggers and haptic feedback.
  Games whose selected features require an external modification no longer
  activate the bridge and hide the working physical XInput controller. The
  Tray identifies them with a localized badge and one-shot notification that
  links to their PCGamingWiki instructions.
- **Initial APEX 6 Pro realtime-haptics port**: recognizes the verified
  `37D7:2502` / usage-page `FFA0` interface and its `0x96` identity without
  routing it through the APEX 5 protocol. A dedicated 125 Hz writer performs
  the `0x53` enable/disable handshake and streams `0x57` blocks containing
  eight 1 kHz subframes for the two grip voice coils and the routed trigger
  force channel. Independent LT/RT programs are position-gated and multiplexed
  without starting any realtime worker on APEX 4 or APEX 5.
- **Native APEX 6 haptic waveform transport**: VIIPER retains the existing
  energy/peak/transient frame used by APEX 4/5 and additionally low-pass
  decimates the virtual DualSense stereo haptic endpoint from 48 kHz to eight
  signed 1 kHz samples. Integrated `libVIIPER v0.7.0-asb11` and sidecar
  `v0.7.0-asb9` expose the new frame while preserving the existing format.
- **Safe APEX 6 diagnostics and shutdown**: `identify`, `test-trigger`,
  `test-rumble`, `clear`, and `bridge-triggers` select model-specific behavior.
  Shutdown sends repeated neutral blocks followed by both motor-disable frames;
  protocol, identity, waveform, and streamer tests ensure APEX 6 commands can
  never be sent to APEX 4/5 transports. Real-hardware validation is still
  required before marking APEX 6 support fully verified.
- **Xbox Mode / AnyFSE input and Space Station compatibility**: the APEX 5
  reader now combines the mapped game-controller HID collection with the
  vendor motion stream instead of polling XInput. Full Screen Experience can
  no longer neutralize the bridge's standard controls, and Space Station's
  onboard mappings remain authoritative across Desktop ↔ FSE transitions.
- **Stellar Blade ranged attacks ([#6](https://github.com/ReynArts/ApexSenseBridge/issues/6))**:
  the virtual DualSense now mirrors host
  timestamps and synthesizes the adaptive-trigger mechanism state reported by
  real hardware. Weapon effects advance through ready, firing, and fired as RT
  crosses the game-defined break points, so games that consume Sony's raw
  trigger-status bytes no longer see an analog press with a permanently idle
  trigger. Neutral trigger-arm, device-timestamp, and wired-connection metadata
  now also match a physical USB DualSense.

## 1.0.0-beta.3

- **APEX 4 wireless stability**: avoids the trigger apply flag that stalls the
  controller over its 2.4 GHz receiver, and paces/coalesces trigger and rumble
  writes on a background writer so game input and haptics no longer queue
  behind vendor commands.
- **Game detection exclusions**: a removed game can be added again; both its
  normalized identifier and display-title alias are now cleared together.
- **Interactive Controller Diagnostic & Test suite (`ControllerTestWindow`)**:
  - **6-axis Gyroscope & Accelerometer Suite (`TabGyro`)**: Real-time Artificial Horizon flight instrument with dynamic pitch elevation and roll banking; six telemetry gauges tracking angular velocity (DPS X/Y/Z) and gravitational acceleration (G-force X/Y/Z) at ~300 Hz; dynamic motion state detection (Stationary vs Moving) and interactive zero-calibration centering.
  - **Adaptive Triggers Testing**: Real-time excitation of force feedback motors across multiple modes (Progressive Resistance, Weapon Break/Snap, Haptic Vibration, Elastic Bow Tension) and 4 resistance levels (Soft, Medium, Strong, Rigid/Ultra) with instant zero-reset.
  - **Dual Rumble Motors Testing**: Independent and combined testing of heavy low-frequency and light high-frequency motors with 0-255 sliders, presets (20%, 50%, 100%), and 1-second pulse triggers.
  - **LightSync RGB LED Testing**: Direct color customization and preset testing through the APEX 5 working LED configuration, with instant profile restoration and no flash commit.
  - **Virtual DualSense Visualizer & Persistence Verifier**: Interactive real-time vector visualizer of a PlayStation 5 DualSense controller with pixel-perfect focus highlight overlays and verification of hardware NVRAM persistence and disk configuration integrity.
  - **Zero-Cost Idle Architecture**: In tray/background mode, polling timers and test execution sub-processes are completely shut down (strictly 0% CPU and HID bus overhead).
- **APEX 5 End-to-End Motion Support**: Decodes all three gyroscope and accelerometer axes from the event-driven vendor stream, converts accelerometer scale to virtual DualSense calibration, and forwards all six signed axes through VIIPER.
- **Real Physical Battery & Charge Reporting**: Reports physical controller battery percentage and charging state via VIIPER report byte 53 (`kDischarging = 0`, `kCharging = 2`, `kFull = 4`), with periodic 15-second non-blocking refresh.
- **Dynamic DualSense Lightbar (RGB) Synchronization**: Intercepts virtual DualSense lightbar packets and updates every APEX 5 working RGB frame to one uniform color in one continuous latched transfer, followed by a single unlock packet. Packet runs use protocol-relative indices and wait for the controller's acknowledgement after every report, preventing shifted frames and dropped colors with the smallest reliable transfer sequence. Shutdown restoration uses the same atomic path, avoiding a final half-profile/half-lightbar mixture. No flash commit is sent, and the non-functional factory `0xF5 TestLed` path is no longer used. Configurable via `--sync-lightbar` CLI switch, Tray dashboard, and Playnite extension.
- **Faithful Lightbar Output Filtering**: `libVIIPER v0.7.0-asb8` now carries RGB bytes together with the DualSense `LIGHTBAR_CONTROL_ENABLE` bit in its raw feedback frame. Rumble-only and trigger-only reports are ignored by LightSync instead of being misread as black, eliminating invented color transitions and intermittent flashing.
- **Call of Duty / Playnite Input Reliability**: Uses the APEX 5 vendor HID stream for motion and its matching XInput slot for sticks, triggers, D-Pad, and buttons, then forwards the merged state through the isolated virtual DualSense. Automatic onboard profile switching remains enabled; the bridge waits for the delayed firmware transition, restarts controller and raw-data routing, reopens the hybrid backend, requires three fresh reports, and resynchronizes the virtual controller before Playnite launches the game. Original routing is restored after normal exits and crashes.
- **Physical Input Stall Protection**: Stops a bridge session after one second without a fresh event-driven physical report instead of indefinitely replaying a frozen neutral or cached state through an apparently connected virtual DualSense.
- **LightSync RGB CPU Fix**: Replaces the RGB rate limiter's active wait with a blocking condition-variable wait. Enabled synchronization remains capped at 12.5 Hz without consuming a CPU core between writes; disabling RGB now creates no RGB worker, performs no RGB HID writes, and installs no RGB feedback branch.
- **Gamepad-First Console UI**: Full 2D D-Pad / thumbstick navigation across Tray library and settings window (`GameListWindow`), featuring analog stick deadzone hysteresis, debounce protection, contextual HUD actions, and official Xbox Game Bar button glyphs (A, B, X, Y).
- **Live Controller Hot-Plug Detection**: Polling connected Flydigi hardware every 2 seconds (`ControllerDetectionService`) to dynamically distinguish APEX 4, APEX 5, unsupported devices, or disconnected states with live status badges.
- **Modernized Localization Architecture**: Embedded UTF-8 JSON language files (`en.json`, `fr.json`, `es.json`, `zh.json`) with strict key parity, supporting optional local folder overrides and automated English fallback.
- **Dedicated Modal Language Picker (`LanguagePickerWindow`)**: Controller-friendly grid navigation replacing inline radio selectors.
- **Hardened Virtual DualSense Startup & Recovery**: Preserves and concatenates pending recovery errors instead of overwriting diagnostic traces.
- **Playnite Extension 1.0.0 Alignment**: Updated extension manifest, assembly metadata, settings view with Lightbar sync toggle, and streamlined session launching.

## 0.6.3

Full validation and uninstall-policy details are available in
[`RELEASE_NOTES_0.6.3.md`](RELEASE_NOTES_0.6.3.md).

- Never removes usbip-win2 from the ApexSenseBridge uninstaller. The Control
  Panel and silent uninstall no longer expose the legacy dependency-removal
  switch, so the upstream USB filter can only be removed separately by the user
  through Windows Settings.
- Preserves HidHide unless the user accepts a separate default-No prompt and a
  fresh-install provenance marker, exact version, product registration and
  service check all succeed. Incomplete or foreign HidHide installations are
  rejected during setup instead of being overwritten.
- Preserves settings, logs, learned bindings and Playnite profiles by default.
  Their separate removal prompt defaults to No; silent deletion requires the
  data-only `/REMOVEUSERDATA` switch.
- Restricts automatic updates to the exact setup asset in the official GitHub
  repository, validates product/version metadata and a trusted Authenticode
  signature, and pins the signer to the installed ApexSenseBridge publisher.
- Adds release contracts for source/tag version consistency, exact artifact
  naming, checksums and publisher signatures, and runs the full CTest suite
  before GitHub can publish a release.
- Closes a narrow Tray shutdown race so a newly learned executable binding is
  marked for synchronous persistence before it becomes visible to readers.

## 0.6.2

- Adds native usbip-win2 0.9.8.0 attach-ABI support to both integrated VIIPER
  and its sidecar, while retaining the two older ABI fallbacks. The command
  fallback now also resolves the standard USBip installation directory instead
  of requiring `usbip.exe` to be present in `PATH`.
- Replaces the vulnerable bundled usbip-win2 0.9.7.7 prerequisite with the
  Microsoft-attestation-signed OSSign 0.9.8.0 x64 build from upstream commit
  `83bd1f78`. This contains both the 0.9.7.7 send-spinlock fix and critical
  memory-corruption fix `4139f44`; installer, portable helper and build-time
  SHA-256 checks are pinned to the exact signed payload.
- Refuses to preserve or upgrade older USBip packages in place. Users with a
  0.9.7.x installation must uninstall it and restart first, avoiding the known
  nested-uninstaller hang while preventing continued use of vulnerable drivers.
- Fixes the installer's post-prerequisite registry checks using section-escaped
  product IDs inside Pascal Script. A successful USBip installation is no
  longer misreported as failed or as unregistered driver remnants; existing
  HidHide installations are detected through their correct product key too.
- Temporarily hides Space Station's verified GeniTech gamepad bus and its
  DualSense/XInput proxies while a bridge session is active, preventing its
  obsolete or duplicate controllers from appearing beside ApexSenseBridge's
  current A-0630 DualSense.
- Preserves Space Station keyboard and mouse remapping by keeping its service
  whitelisted and leaving its distinct shortcut endpoints visible. The full
  pre-session HidHide configuration is restored when the session ends or is
  recovered after an unexpected exit.
- Prevents Tray and Playnite sessions from terminating or replacing each
  other's bridge/VIIPER processes. The engine now owns a machine-session mutex,
  and the Tray stops only the child session it created.
- Rejects weak fuzzy matches for short game names such as `Control`, preventing
  utilities such as Controlify or controller-control panels from repeatedly
  activating the bridge. Exact title, executable and Steam AppID matches remain
  supported.
- Improves game detection and executable learning with independent concurrent
  process tracking, a stable 30-second observation window that is not restarted
  by repeated sightings, limited-rights path resolution for most elevated game
  processes, reliable persistence on shutdown, and detection diagnostics under
  `%LocalAppData%\ApexSenseBridge\logs`.
- Retries transient HidHide control-device access failures and reads the final
  active/device/application lists back before reporting `Ready`. Windows error
  5 now includes an actionable restart/repair message instead of a raw failure.
- Gives the standalone Playnite package a readable release filename:
  `ApexSenseBridge-Playnite-0.6.2.pext`.
- Adds independent per-game controls for bridge activation, automatic touchpad
  remapping, and APEX 5 onboard profiles 1-4 in Playnite. The standalone tray's
  compatible-games list also stores an optional onboard profile per game.
- Switches the selected APEX 5 onboard profile only for the game session and
  restores the exact previous profile on exit. The crash watchdog and RunOnce
  recovery marker now protect both HidHide visibility and profile restoration.

## 0.6.1

- Kept Flydigi Space Station keyboard and mouse shortcuts available during an
  active DualSense bridge by granting its installed `SpaceStationService.exe`
  targeted access through HidHide. The physical APEX game interfaces remain
  hidden from games, preserving the existing double-input protection.
- Replaced the virtual DualSense's mixed A-0356/0x0630 firmware identity with
  the complete 64-byte report captured from current 0x0630 hardware.
  PlayStation Accessories and native games no longer see an obsolete controller
  while runtime telemetry reports the newer update word.

## 0.6.0

- Added Authenticode digital code signing support (`sign-windows-artifacts.ps1`)
  for all release binaries (`ApexSenseBridge.exe`, `ApexSenseBridgeTray.exe`,
  `ApexSenseBridgeControl.exe`, `viiper.exe`, `libVIIPER.dll`, and `ApexSenseBridge-Setup.exe`)
  as well as the Playnite extension, ensuring release integrity and eliminating
  Windows SmartScreen warnings.
- Added full Flydigi APEX 4 hardware bridge support over wired USB and 2.4 GHz
  receiver on retail DeviceTypes `84` and `103`, firmware `0x6830`/`0x6837`:
  read-only identity verification, complete 32-byte `04 FE` event-driven input
  decoding on vendor interface `MI_02`, grip rumble, and FORCEADAPT trigger resistance.
- Implemented targeted HidHide isolation for APEX 4, hiding both the `MI_00`
  gamepad and `MI_01` auxiliary HID mouse nodes while leaving the `MI_02/MI_03`
  vendor transports accessible to prevent double-input or sticky aim.
- Added dynamic game library learning and executable tracking to the Standalone
  Tray application (`ExecutableLearningService.cs`, `LearnedExecutablesWindow.xaml`),
  supporting exact executable resolution, launcher filtering (Steam/Epic/EA),
  and cover caching without runtime external web requests.
- Added clarification in documentation and troubleshooting on upstream `usbip-win2`
  kernel driver instability (issue #172), confirming that BSOD risks occur strictly
  within the third-party kernel driver and not in user-space ApexSenseBridge.
- Hardened APEX 4 isolation so launch fails closed unless both HID and USB nodes
  for the `MI_00` gamepad and `MI_01` auxiliary input have been identified.
- Added last-active adaptive-trigger type and translated FORCEADAPT parameters
  to session telemetry for diagnosable trigger effects.
- Added a compact, no-install APEX 4 tester bundle and interactive validation
  script (`scripts/Test-Apex4-Full-Session.ps1`, `scripts/Test-Apex4-Port.ps1`).

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
