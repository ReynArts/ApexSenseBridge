# ApexSenseBridge 1.0.0 — Stable Release Notes

ApexSenseBridge 1.0.0 is the first stable release of the native input and haptic bridge for Flydigi controllers on Windows. It translates native PlayStation 5 DualSense input, adaptive trigger effects, and audio-driven haptics to the Flydigi **APEX 4**, **APEX 5**, and **APEX 6 Pro** via an isolated virtual DualSense device with sub-2 ms latency.

---

## 1. Supported Controllers & Hardware Translation

### Flydigi APEX 6 Pro (Standard & PBZ `0x98` / `0x96`)
- **Native Voice-Coil Architecture:** Operates four voice-coil actuators (two grip voice coils and multiplexed trigger motors) via a dedicated 125 Hz realtime writer (`0x53` handshake, `0x57` streaming blocks with eight 1 kHz subframes).
- **Synchronized Trigger Carriers:** Continuous trigger carrier clocks run on a unified 1 kHz sample clock across both sides, preventing phase-drift after asymmetric presses.
- **Native Bow Effect `0x22`:** Decodes draw and snap parameters into a bounded tactile draw texture and single 16 ms snap pulse.
- **Native Grip PCM Preservation:** 8 ms PCM blocks are preserved at unity gain without being truncated by the global audio activation threshold.
- **Experimental PCM Companding Gain:** Opt-in CLI gain (`--apex6-haptic-gain 100..200`) provides smooth amplitude companding without hard clipping or signal distortion.
- **Quadraphonic Audio Preflight:** Inspects the virtual controller's mix channels and speaker mask at startup, providing actionable guidance if quadraphonic output is not configured.
- **Accurate Frequency & Vibration Decoding:** Native `0x26` vibration frequency is decoded from effect byte 9; empty zone masks and zero frequency cleanly stop trigger vibration.

### Flydigi APEX 5
- **FORCEADAPT Adaptive Triggers:** Translates DualSense trigger resistance, weapon stops, and vibration patterns into native Flydigi FORCEADAPT motor commands.
- **Simultaneous Trigger Stream Guard:** Uses the independent NewXInput `0xEF` vendor stream to preserve simultaneous LT+RT presses across Desktop and Full Screen Experience (FSE), preventing trigger releases during concurrent aim-and-fire actions.
- **Loading Stall Tolerance:** A 1.5-second grace period and up to 3 routing restarts per 10 minutes prevent bridge aborts during CPU-intensive game loading sequences (such as Call of Duty HQ).
- **6-Axis Motion & IMU Decoding:** Full gyroscope and accelerometer telemetry parsed from event-driven vendor reports and converted to standard DualSense calibration.
- **Motion Diagnostic Guard:** Automatically activates raw motion transport during diagnostic sessions and guarantees clean restoration without altering the controller's saved onboard profile.
- **LightSync RGB Synchronization:** Intercepts DualSense lightbar packets and updates the controller's working LED configuration atomically via latched transfers. Uses a condition-variable wait to eliminate CPU polling, with zero flash memory wear.
- **Seamless Profile Switching:** Automatically synchronizes and reacquires input when switching between onboard profiles 1 through 4.

### Flydigi APEX 4
- **Native 16-Bit Gyroscope Decoding:** Direct hardware parsing of the vendor motion stream (including split yaw bytes 18 and 20) with customizable sensitivity (25–400%) and independent yaw axis correction.
- **Interface Auto-Detection:** Automatically distinguishes 64-byte trigger interfaces from degraded 32-byte modes, advising reconnection if the second trigger endpoint is missing.
- **Wireless 2.4 GHz Stability:** Background writer pacing (25 ms) and coalescing prevent command queues from stalling communication over the 2.4 GHz dongle.
- **Pacing Diagnostics:** Real-time tracking of async write duration, replaced pending states, and queue latency.

---

## 2. Controller Feel & Per-Model Calibration

- **Model-Locked Settings:** Hardware calibration controls lock automatically until a supported controller is connected and its identity is verified, preventing misconfiguration and hot-swap desync.
- **Independent Saved Profiles:** Distinct settings profiles are maintained for APEX 4, APEX 5, and APEX 6 Pro; the engine applies the matching profile only after hardware identity verification.
- **Grip Vibration Amplification:** Conventional grip vibration strength can be adjusted from 0% to 200% on APEX 4 and APEX 5 (saturating cleanly at the 255 motor limit), and 0% to 100% on APEX 6.
- **Trigger Strength & Audio Threshold:** Global sliders for trigger force (0–100%) and audio-haptic activation threshold (0–95%). Reducing strength scales effect intensity while preserving physical travel, timing, and frequency.
- **Threshold Gating Scope:** The audio threshold gates only envelope fallback on APEX 6 Pro (native PCM is ungated) and never suppresses standard HID rumble on any model.

---

## 3. Standalone Application & Modern Console Interface

- **Modern Console Interface:** Dark console aesthetic with ambient backdrop lighting, fluid page transitions, and responsive controls.
- **Full Gamepad Navigation:** Complete navigation via D-pad and analog sticks (with deadzone hysteresis and debounce), Cross/A, Circle/B, Options/Menu, bumpers/triggers, and contextual HUD action glyphs.
- **Keyboard Navigation:** Full keyboard support (arrows, Enter, Escape, Ctrl+Tab, Ctrl+F).
- **Curated Home Shelf:** Displays newly added games with "New" badges for titles added within the last 30 days, backed by chronological date tracking (`addedAt`).
- **Dynamic Executable Learning:** Automatically identifies renamed, modded, elevated, or launcher-child executables after 30 seconds of stable gameplay.
- **Per-Game Settings:** Custom executable selectors, profile assignments, and dedicated APEX 5 hardware slots (1–4).
- **Unified Main Entry Point:** Executing `ApexSenseBridge.exe` without parameters automatically opens the Tray application.

---

## 4. Internationalization (9 Languages)

- **Supported Languages:** English (`en`), French (`fr`), Spanish (`es`), Simplified Chinese (`zh`), Russian (`ru`), Korean (`ko`), Vietnamese (`vi`), Japanese (`ja`), and Brazilian Portuguese (`pt`).
- **Strict Dictionary Parity:** Complete 367-key translation parity across all 9 languages covering sessions, recovery, settings, diagnostics, and bug reporting.
- **Automatic Detection & In-App Switching:** Detects Windows display language automatically, with on-the-fly switching via the modal gamepad-friendly language picker (`LanguagePickerWindow`).

---

## 5. Disconnection Recovery, Process Lifecycles & Security

- **Guided Disconnection Recovery:** Pauses automatic bridge engagement upon controller disconnect or sleep; the home screen provides a voluntary **Resume bridge** action with real-time hardware, isolation, and virtual device checks.
- **Intelligent HidHide Conflict Diagnostics:** Identifies when third-party software (DSX, DS4Windows, BetterJoy, HidHide Configuration Client) locks the HidHide control device, explicitly displaying the names of conflicting processes.
- **Zombie Process Reaper:** Automatically terminates orphaned engine child processes if startup times out, preventing stale locks.
- **Fail-Closed Double-Input Isolation:** Guarantees physical controller isolation through HidHide before bridge activation while preserving Space Station keyboard/mouse macros.
- **Crash Recovery & Watchdog:** Armed watchdog and HKCU RunOnce registry protection automatically restore controller visibility and onboard profiles after unexpected crashes or reboots.

---

## 6. Interactive Controller Diagnostic Suite (`ControllerTestWindow`)

- **6-Axis Motion Flight Instrument:** Artificial Horizon visualization with pitch/roll banking, DPS X/Y/Z angular velocity, and G-force X/Y/Z telemetry gauges sampled at ~300 Hz with zero-centering calibration.
- **Adaptive Triggers Excitation:** Interactive testing of Progressive Resistance, Weapon Break/Snap, Haptic Vibration, and Elastic Bow Tension across Soft, Medium, Strong, and Rigid levels.
- **Dual Rumble Motors:** Independent left/right motor sliders (0–255), quick presets (20%, 50%, 100%), and 1-second pulse triggers.
- **LightSync RGB LED Test:** Real-time color customization and presets verified against APEX 5 RAM configuration without flash commits.
- **Live Battery & Charging Telemetry:** Periodic non-blocking telemetry reflecting controller charge percentage and charging status.
- **Prepared Game Launcher:** Diagnostics tool that waits for complete bridge readiness before launching the target executable.
- **Zero-Cost Idle Architecture:** Test subprocesses and polling timers shut down completely when the diagnostic window is closed (0% CPU / HID bus usage).

---

## 7. Touchpad Emulation & Game Profiles

- **Death Stranding 2 Profile:** Maps `View/Back` to right-side touchpad click (Likes, Communication) and `LB + Menu` to left-side touchpad click (Photo Mode).
- **Gesture Shortcuts:** Hardware shortcuts for touch swipes and gestures in Marvel's Spider-Man 2, Miles Morales, Ghost of Tsushima (`D-pad Right + Stick`), and Warframe.
- **Stellar Blade Mechanism Synthesis:** Accurate emulation of host timestamps and Sony raw weapon status states (ready, firing, fired) across analog trigger travel.
- **Manual-Fix Safety:** Games requiring third-party mods (tracked via PCGamingWiki) display localized warning badges and help links rather than triggering false bridges.

---

## 8. Playnite Integration

- **Seamless Launch Integration:** Fully integrated with Playnite Desktop and Fullscreen modes.
- **Per-Game Customization:** Configurable trigger remapping, touchpad profiles, APEX 5 hardware slots, Lightbar synchronization, and 0–200% grip rumble gain.
- **Session Mutual Exclusion:** Prevents duplicate session launching and conflicting bridge instances between Tray and Playnite.

---

## 9. Installation, Architecture & Packaging

- **Attestation-Signed Kernel Drivers:** Uses Microsoft attestation-signed USBip 0.9.8.0 drivers and libVIIPER v0.7.0 with loopback-only communication.
- **Offline Setup & Portable Archives:** Standard Inno Setup installer and zero-install portable ZIP package.
- **Clean Uninstallation:** Full uninstallation support with optional `/REMOVEUSERDATA` flag to clean user preferences.
- **Authenticode Signing:** Release binaries and packages are signed with Authenticode certificates to ensure binary integrity and origin verification.
