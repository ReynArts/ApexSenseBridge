# ApexSenseBridge 1.1.0

**Your Flydigi APEX. Native DualSense features. On Windows.**

ApexSenseBridge brings native PlayStation 5 DualSense input and feedback to
Flydigi **APEX 4, APEX 5 and APEX 6 Pro** controllers through a virtual DualSense.
APEX 4/5 translate adaptive triggers into FORCEADAPT resistance and audio haptics
into grip vibration. APEX 6 Pro uses its four voice-coil actuators for native
haptic audio and translated trigger feedback.

**1.1.0 is a major update**, even though the version number only moves from
1.0 to 1.1. **Trigger effects have been reworked on the APEX 6 Pro and the
APEX 5**: each DualSense effect now follows what it means (weapon breaks, Bow,
progressive resistance, vibration, Machine, Galloping) instead of a flat
translation. The APEX 4 triggers are unchanged. The APEX 6 Pro also gains its motion sensors, real battery level,
onboard profile slots and a trigger-effect translation tuned by meaning. You can
now choose between several connected controllers. On APEX 4 and APEX 5 the
virtual DualSense is kept alive across a controller power cycle, and the automatic
activation is far more robust. See [What's new in 1.1.0](#whats-new-in-110).

**The flagship feature of 1.1.0 is 2 kHz haptics on the APEX 6 Pro**, on by
default: the controller's voice coils now receive the full 2000 samples per
second they can play, so DualSense haptic effects keep their 500 to 1000 Hz
content instead of being folded down. See
[2 kHz haptics](#2-khz-haptics-on-the-apex-6-pro).

> [!WARNING]
> **The 2 kHz mode is new and has not been tested over the long term.** The
> stability of the controller hardware under long sessions at this rate is not
> validated. You can turn it off at any time (see
> [how](#2-khz-haptics-on-the-apex-6-pro)).

[Download ApexSenseBridge 1.1.0](https://github.com/ReynArts/ApexSenseBridge/releases/latest) ·
[Troubleshooting](TROUBLESHOOTING.md) · [Changelog](CHANGELOG.md) ·
[Roadmap](ROADMAP.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/ReynArts/ApexSenseBridge?color=brightgreen)](https://github.com/ReynArts/ApexSenseBridge/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/ReynArts/ApexSenseBridge/total?label=downloads&color=blue)](https://github.com/ReynArts/ApexSenseBridge/releases)
[![Authenticode](https://img.shields.io/badge/code%20signing-Authenticode-blueviolet.svg)](#security-signing--crash-recovery)

<img width="500" alt="screen ASB 1 0" src="https://github.com/user-attachments/assets/c47eba4c-e718-4890-9947-4afcca206aa8" />

---

## Contents

- [What's new in 1.1.0](#whats-new-in-110)
- [2 kHz haptics (APEX 6 Pro)](#2-khz-haptics-on-the-apex-6-pro)
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
- [Reporting a problem](#reporting-a-problem)
- [Building from source](#building-from-source)
- [License and notices](#license--third-party-notices)

---

## What's New in 1.1.0

1.1.0 is a major update. These are the changes you will notice; the
[changelog](CHANGELOG.md) has the complete list, with measurements and issue links.

### 🔊 Flagship: 2 kHz haptics on the APEX 6 Pro, on by default

Native game haptics now reach the grip voice coils at the full 2000 samples per second the controller can play, instead of 1000, with a proper low-pass filter in between. Their 500 to 1000 Hz content keeps its pitch instead of folding down into low tones, which brings the APEX 6 Pro noticeably closer to the way a DualSense renders haptic effects. It is on by default and can be turned off. **It has not been tested over the long term, so the stability of the controller hardware under long sessions is not validated.** Details, checks and limits: [2 kHz haptics](#2-khz-haptics-on-the-apex-6-pro).

### Reworked trigger effects (APEX 6 Pro and APEX 5)

- **APEX 6 Pro:** each DualSense effect is translated into a vibration that follows its meaning (see below), with twelve weapon presets.
- **APEX 5:** the bridge follows DualSense per-zone force curves and rhythms (graded or gapped profiles, Galloping, Machine) instead of one constant resistance, and the Bow effect releases at its end zone. On by default; the **Trigger effect emulation** switch turns it off.
- **APEX 4:** trigger behaviour is unchanged in 1.1.0.

### APEX 6 Pro

- **💡 Lightbar sync** is now available on the APEX 6 Pro and the onboard profile selector is unlocked.
- **🎯 Weapon snaps that no longer get cut.** Games set and clear a trigger effect about sixteen times a second; a clear used to cut a snap in progress and the trigger position was forgotten, so many presses never crossed the break point. A clear no longer cuts a snap, and the position memory survives a new effect.
- **🧭 Gyroscope and accelerometer.** A bridge session now forwards the motion sensors to the virtual DualSense. The session switches on the controller's raw-data routing when it starts, waits up to 1.5 s for the first motion report and restores the routing when it ends; if no report arrives, it starts without motion and prints a warning. `test-gyro` works on the APEX 6 Pro.
- **🔋 Real battery level and charge state** reach the virtual DualSense instead of a fixed 100 %, refreshed every 15 seconds with self-timing safeguards. A controller that reports nothing keeps showing 100 %.
- **🗂️ Onboard profile slots 1–4** (`bridge-triggers --apex-profile`, `test-profile-switch`). The original slot is restored and verified when the session ends, and after a crash by the watchdog. `test-profile-switch` compares the slot checksums before and after and fails if they changed.
- **🎯 Trigger effects translated into vibration by meaning.** The APEX 6 Pro triggers only vibrate, so each DualSense effect becomes a vibration that follows what the effect means instead of a flat buzz: one burst when the trigger crosses the break point for weapon effects, a draw that builds up to a release burst for Bow, smooth transitions between zones for progressive resistance and vibration, and a rhythm for Machine and Galloping. They are tuned by feel, not calibrated against a real DualSense, and they reproduce vibration rather than mechanical resistance.
- **🔫 Twelve weapon presets** (machine gun, rifle, submachine gun, light machine gun, pistol, shotgun, sniper rifle, sword, blade, dagger, axe, club) to try from the Tray trigger test or with `test-trigger --mode <preset>`. `test-trigger` now plays the real native effect and follows your trigger.
- **Cleaner trigger release.** The trigger hum that newer firmware kept playing after a release is stopped ([#74](https://github.com/ReynArts/ApexSenseBridge/issues/74)). When only the triggers are driven, the grip motors stay in the controller's own mode.

### All controllers

- **🎛️ Choose between several APEX controllers.** With two or more APEX controllers or receivers connected, the Tray identifies each one and shows the selected model followed by the number of the others in the top bar, for example "Apex 6 Pro + 1". Click it, or press View on a gamepad outside the Games tab, to pick the controller: the home picture, the settings and the next bridge session follow your choice. The choice is saved, and locked while a session runs. The engine still bridges one controller at a time; bridging two at once (couch co-op) is not supported. Sessions no longer fail outright with several controllers connected ([#70](https://github.com/ReynArts/ApexSenseBridge/issues/70)).
- **🔁 The virtual DualSense survives a controller power cycle (APEX 4 and APEX 5).** Switching the controller off, letting it sleep or unplugging it no longer ends the session: the game keeps a connected DualSense with neutral input and its button prompts do not flip. Validated on real hardware on the APEX 5; not yet validated on the APEX 4. See [Disconnection Recovery](#disconnection-recovery).
- **🛠️ Far more robust automatic activation.** Elevated and anti-cheat protected games are no longer taken for closed, which made the virtual DualSense reconnect in a loop ([#67](https://github.com/ReynArts/ApexSenseBridge/issues/67)). Repeated start failures of the same game pause automatic starts instead of looping, an engine that stops unexpectedly after becoming ready is retried after 10 s and paused the same way ([#53](https://github.com/ReynArts/ApexSenseBridge/issues/53), [#54](https://github.com/ReynArts/ApexSenseBridge/issues/54), [#55](https://github.com/ReynArts/ApexSenseBridge/issues/55), [#59](https://github.com/ReynArts/ApexSenseBridge/issues/59)), and Grand Theft Auto V Enhanced is no longer matched to the original game.
- **🎚️ Per-game tuning and reset buttons.** Every tuning slider has a reset button, and a game's detail pane can override trigger strength, vibration strength and (APEX 4/5) the haptic threshold for that game only.
- **🐞 New report window.** **Settings** > **Send a report** offers Bug, Feature or improvement, Unsupported game and Download the logs. Reports can attach the full logs, and be sent anonymously without a GitHub account.
- **📚 Manual-fix notes in the Tray.** The game catalogue (over 225 games) keeps each manual-fix game's PCGamingWiki workaround note, with the tools and links it names, shown in the interface language. See [What does "Manual fix" mean?](#what-does-manual-fix-mean).
- **⚠️ Competing engine warning.** The bridge prints a warning at startup when Flydigi X-Haptics (`XHaptics.exe`, `x-haptic-engine-fusion.exe`) is running, because it can write haptic frames to the same controller. Nothing is stopped or blocked.
- **🕹️ Alan Wake 2 touchpad profile** and a detailed **Touchpad remapping** panel in the Tray.
- **🚀 `ApexSenseBridgeTray.exe --force-always-on`** starts the Tray with Force continuous activation already on, for that run only.

### APEX 5 and APEX 4

- **APEX 5, per-zone and rhythmic trigger emulation, on by default.** The bridge follows DualSense trigger curves and rhythms (graded or gapped profiles, Galloping, Machine) from the PC, and the Bow effect now releases at its end zone. The lightbar sync keeps the brightness saved in Space Station, with a **Lightbar max brightness** slider.
- **APEX 4, faster rumble cadence** (10 ms instead of 25 ms) so short haptic bursts are no longer replaced by the next stop; the identity exchange sends far fewer requests.

### Good to know when upgrading

- **APEX 5:** the per-zone and rhythmic trigger emulation is on by default. Turn it off with the **Trigger effect emulation** switch in the Tray settings, or `--no-zone-emulation`, if you prefer the previous single-resistance behaviour.
- **APEX 6 Pro:** triggers now feel different, because each effect is translated by meaning instead of played as a flat vibration.
- **APEX 6 Pro:** the 2 kHz haptics mode is on by default. Turn it off with the **2 kHz grip haptics** switch in the Tray settings, or pass `--apex6-rate 1000`, if you prefer the previous 1 kHz stream.
- **APEX 4:** `ASB_APEX4_RUMBLE_MS=25` restores the previous rumble spacing without rebuilding.
- **Known limitation (APEX 6 Pro):** the firmware drives both triggers from a single signal, so when a game plays two different effects on the two triggers at the same time, the bridge plays the sum of both effects on both triggers (each trigger feels a blend). One effect on one trigger, or the same effect on both, is unaffected. A better solution is on the [roadmap](ROADMAP.md).
- **Not covered yet:** the APEX 6 Pro does not ride out a power cycle (the bridge stops and the voluntary recovery applies). See the [roadmap](ROADMAP.md).

---

## 2 kHz Haptics on the APEX 6 Pro

The APEX 6 Pro plays its haptic stream at 2000 samples per second, but until
1.1.0 the bridge fed it 1000 and the controller interpolated the rest, losing
everything between 500 and 1000 Hz. In 1.1.0 the bridge sends the real 2 kHz
data: the virtual DualSense filters the game's 48 kHz haptic channels with a
low-pass filter (flat within 0.25 dB up to 800 Hz, at least 48 dB down from
1150 Hz) and the engine sends two raw eight-sample reports every 8 ms. It is on
by default for the APEX 6 Pro with grip vibration on; with any other controller,
or with an older libVIIPER or the sidecar backend, the engine falls back to the
previous behaviour automatically.

> [!CAUTION]
> **Not tested over the long term.** The stability of the controller hardware
> (voice-coil actuators, firmware and radio link) under long sessions at 2 kHz is
> **not validated**. The software is provided as is, under the MIT license.

**What has been checked, and what has not.**

- Checked on a single APEX 6 Pro over the 2.4 GHz receiver (firmware 9.0.6.8), by ear: tones of 700 and 900 Hz were heard an octave above their 350 and 450 Hz references in 5 trials out of 5, and a 700 Hz tone and a short fanfare through the real bridge were judged clearly better at 2000 than at 1000.
- Nothing was measured with a sensor. The wired connection, long sessions, other firmware versions and other units have not been tested.
- Light stutters can appear during long continuous effects. In a side-by-side test they were heard identically at 1000 and 2000, so they are not specific to this mode; the cause is not isolated.
- The motors give little sensation above roughly 300 Hz, the two triggers still share one signal, and how much of the 500 to 1000 Hz band real games actually use has not been measured.

**Turning it off.** Switch off **2 kHz grip haptics** in the Tray settings, or pass
`--apex6-rate 1000` to `bridge-triggers`. If you report a problem, say whether
the 2 kHz mode was on and attach the logs.

---

## Stable 1.0.0 Highlights

1.0.0 was the first stable release and everything below is still part of the app.

- **🎮 Console Interface Refresh:** Complete console-grade interface for the Tray app, controller diagnostic, and language picker. Full gamepad navigation (D-pad/stick, Cross/A, Circle/B, Options/Menu, bumpers/triggers) and keyboard shortcuts (arrows, Enter, Escape, Ctrl+Tab, Ctrl+F).
- **🌍 9 Supported Languages:** English, French, Spanish, Simplified Chinese, Russian, Korean, Vietnamese, Japanese, and Brazilian Portuguese, with automatic Windows language detection and in-app language switching.
- **✨ Recently Added Games & 215+ Titles:** The home screen features a curated shelf of newly added games with "New" badges (last 30 days) and date tracking, backed by an automated daily PCGamingWiki sync.
- **🕹️ Controller-Specific Calibration & Hot-Plug Safety:** Independent calibration profiles for APEX 4, APEX 5, and APEX 6 Pro. Settings controls lock when no supported pad is connected, preventing misconfiguration. Engine selects calibration only after verified hardware identity.
- **🔊 Grip Vibration Amplification (0–200%):** Conventional grip vibration strength can be boosted up to 200% on APEX 4 and APEX 5 with saturation at the motor-command limit, without changing trigger resistance.
- **🎯 APEX 6 Pro Native Voice-Coil Haptics:** Full support for the APEX 6 Pro and official Phantom Blade Zero `0x98` variant. Features synchronized 1 kHz trigger carrier clocks, native Bow effect `0x22`, preserved quiet native PCM, experimental companding gain (`--apex6-haptic-gain 100..200`), and an opt-in 2 kHz grip stream (`--apex6-rate 2000` or the Tray switch).
- **🧭 APEX 4 Native Gyroscope Decoding:** Direct 16-bit hardware gyro parsing with customizable sensitivity (25–400%) and independent yaw correction slider.
- **🛡️ Intelligent HidHide Diagnostics:** Diagnoses access conflicts when DSX, DS4Windows, or BetterJoy hold the HidHide driver open and explicitly lists conflicting processes.
- **🕹️ Death Stranding 2 Touchpad Remapping:** Dedicated profile mapping `View/Back` to right-touchpad click (Likes, Communication) and `LB + Menu` to left-touchpad click (Photo Mode).
- **🛡️ Guided Disconnection Recovery & Zombie Reaper:** Automatic bridge recovery guidance on controller disconnect or sleep, plus automatic orphan child process termination on startup timeout.
- **🏎️ Built-In libVIIPER v0.9.0 Backend & Kernel USBip 0.9.8.0:** Attestation-signed Microsoft kernel driver with loopback-only communication and measured sub-2 ms report forwarding.

See the [changelog](CHANGELOG.md) for the development history and the
[release checklist](RELEASE_CHECKLIST.md) for packaging and validation requirements.

---

## Supported Controllers

| Controller | Connection Modes | Hardware Verification Status | Special Features |
|---|---|---|---|
| **Flydigi APEX 6 Pro** | 2.4 GHz Dongle / Wired USB | ✅ Verified in Hardware (Standard & PBZ `0x98`), including the 1.1.0 additions | Four voice-coil actuators, native DualSense PCM haptics & companding gain, trigger effects translated into vibration by meaning, 12 weapon presets, gyroscope & accelerometer, battery level, onboard profile slots 1–4 |
| **Flydigi APEX 5** | 2.4 GHz Dongle / Wired USB | ✅ Fully Verified in Hardware & Gameplay | Full FORCEADAPT Trigger Resistance with per-zone & rhythmic emulation, Audio Haptics, 0–200% Grip Rumble Gain, Onboard Profiles 1–4 switching, survives a controller power cycle |
| **Flydigi APEX 4** | 2.4 GHz Dongle / Wired USB (DInput) | ✅ Fully Verified in Hardware & Gameplay | Direct 16-bit Native Gyroscope with 25–400% sensitivity & yaw correction, FORCEADAPT Resistance, 0–200% Grip Rumble Gain, 32-byte/64-byte interface auto-detection |

The APEX 6 Pro does not yet ride out a controller power cycle.

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

### 📚 225+ Automatically Detected DualSense Games

The catalogue tracks native PlayStation 5 DualSense support using PCGamingWiki. Catalogue detection does not certify every effect on every APEX model. The Standalone Tray App and Playnite extension automatically recognize and engage the bridge when any supported title launches.

**[🎮 Browse the full game catalogue](GAMES.md)** — an alphabetical list with adaptive-trigger and haptic-feedback support, manual-fix links, and recent additions with cover art. Updated automatically with the daily catalogue refresh.

### What Does "Manual fix" Mean?

A **Manual fix** badge in the game list means the game does not provide the listed DualSense feature by itself: PCGamingWiki classes it as "hackable", so it needs a third-party modification or setting that **you** apply. ApexSenseBridge does not install, run or check these fixes. By default the bridge therefore stays off for such games and leaves your physical controller available.

**Allow automatic activation** only records that you applied the fix: the bridge then starts for that game like for any other and hides the physical controller. It does not make the game accept the virtual DualSense and does not replace a community utility such as the Forza Horizon DualSense scripts, which have their own requirements. Leave it off unless you applied the fix and it is documented to work with a DualSense; if the game shows no controller after enabling it, turn it off again. Details and an example are in [Troubleshooting](TROUBLESHOOTING.md#8-what-does-manual-fix-mean).

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
>
> **Windows 11 Smart App Control blocked the installer or `ApexSenseBridgeTray.exe`?** A Defender exclusion does not help. See [Troubleshooting](TROUBLESHOOTING.md#windows-11-smart-app-control-blocks-the-app).

### Requirements

- Windows 10 or 11 **x64**.
- A supported controller connected by **USB or its 2.4 GHz receiver**. For APEX 4, use **DInput mode**.
- Administrator access for the one-time USBip and HidHide driver installation.
- For Steam games, **disable Steam Input for that game** so native DualSense input and feedback reach the virtual controller. A few games, such as Monster Hunter Wilds, accept controllers only through Steam Input; see [Troubleshooting](TROUBLESHOOTING.md#issue-h-games-that-only-accept-controllers-through-steam-input-monster-hunter-wilds).

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

> [!TIP]
> **Steam shows two controllers or double inputs?** Steam may have opened the physical controller before the bridge hid it. Exit Steam completely, start the bridge (for example with **Force continuous activation**) and wait for **Bridge ready**, then reopen Steam and keep Steam Input disabled for the game. See [Troubleshooting](TROUBLESHOOTING.md#issue-e-steam-shows-both-the-physical-controller-and-the-virtual-dualsense).

`ApexSenseBridge.exe` is the engine and CLI diagnostic tool. Opening it without
a command also opens the Tray app.

---

## Usage Modes

### 1. Standalone System Tray App (Steam, Epic, EA, Game Pass, etc.)

- Launch **`ApexSenseBridgeTray.exe`** (or enable *Launch at Windows startup*).
- Sits silently in the System Tray with a modern console-style interface.
- **Automatic Detection:** Launches and closes the DualSense bridge automatically when any of the 225+ supported games start.
- **Dynamic Executable Learning:** Detects renamed, modded, elevated or multi-process game executables after 30 seconds of a stable automatically detected session while filtering out game launchers (Steam, Epic, EA, Ubisoft). Force Continuous Activation alone cannot identify which game should own an executable.
- **Dashboard & Per-Game Profiles:** Right-click the Tray icon to open the game list, force a specific profile, or assign a dedicated APEX 5 or APEX 6 Pro hardware slot (1–4) for each game.
- **Start with Force Continuous Activation (`--force-always-on`):** `ApexSenseBridgeTray.exe --force-always-on` (for example in a shortcut or a scheduled task) starts the Tray with **Force continuous activation** already on, exactly as if you had switched it on from the home screen: the bridge starts at once and automatic detection no longer starts or stops it. It applies to that run only and is never written to the settings file, so launching the Tray without the argument is back to automatic detection. The argument is read only by the launch that becomes the running Tray: if the Tray is already running, the new launch just brings up its window and the argument is ignored, so close the Tray first. If the bridge cannot start (for example no APEX connected), the option is dropped for that run and automatic detection resumes.

### 2. Playnite Integration

- Seamlessly integrated with Playnite Desktop and Fullscreen modes.
- Game launches automatically initiate the bridge, isolate the physical pad, and apply profile mappings.
- Right-click any game in Playnite > **ApexSenseBridge** to customize trigger remapping, touchpad gesture profiles, onboard APEX 5 or APEX 6 Pro hardware slots, or 0–200% grip rumble gain.
- Do not run the Tray and Playnite automation for the same game. ApexSenseBridge protects the active session from being killed, but selecting one owner avoids duplicate notifications and ambiguous start/stop events.

### Update checks and anonymous version statistics

The Tray (at startup) and the Playnite extension (at Playnite startup) ask the project's relay which release is the latest. The request carries only the component and its version (for example `tray/1.1.0`) and a random installation ID generated on first use, never derived from your hardware or your account. The relay stores the version and a hash of that ID that changes every month, so installations cannot be followed from one month to the next, and it does not store IP addresses. The statistics only tell the maintainer which versions are still in use.

- On by default. Turn it off with **Settings** > **Anonymous version statistics** in the Tray, or the matching checkbox in the Playnite extension settings. The installation ID is then deleted and update checks query GitHub directly.
- If the relay is unreachable, the check falls back to GitHub.
- Installers are still downloaded straight from GitHub Releases and checked as before (Authenticode signature, product name, version and publisher).
- When a release provides `ApexSenseBridge-Update.zip` and the drivers are unchanged, the Tray and the Playnite extension apply it through `ApexSenseBridgeUpdater.exe` (one UAC prompt, no setup wizard) after checking its signatures and file hashes. The update waits while a game session is active, and falls back to the full setup in any doubtful case.

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
the next Tray session, except trigger and vibration strength, which also apply at once to a running Tray session; 100% preserves original strength and 0% mutes the effect.
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
for APEX 4 gyro to work, the gyro must be enabled in the active Flydigi Space
Station profile (Gyro tab). The RGB lightbar sync is available on the APEX 5 and
the APEX 6 Pro (off by default): it writes the game color with the brightness
saved in Flydigi Space Station, and the "Lightbar max brightness" slider (0–100%)
caps it further. The APEX 6 Pro keeps what is written to its lighting setting
after a power cycle, so the bridge first saves your Space Station lighting in
`%LOCALAPPDATA%\ApexSenseBridge\apex6_led_backup.bin`, sends at most one color
every 1.5 s and 300 per session, restores your lighting when the session ends,
and restores it at the next start if a session was killed. The APEX 6 haptic
threshold is fixed at 0. General Tray preferences remain editable without a
controller. Existing strength/rumble preferences migrate without being reset;
subsequent edits affect only the selected model. Playnite keeps its independent
settings.

### Disconnection Recovery

**Switching the controller off and on.** On an APEX 4 or APEX 5, turning the
controller off, letting it fall asleep or unplugging it during a session does
not end the session. The virtual DualSense stays connected with neutral input,
so the game never sees a controller disappear, does not need to be restarted
and does not change its button prompts. The bridge waits up to 10 minutes (the
`--reattach-seconds` option of `bridge-triggers`; `0` turns it off) for the same
controller, then re-checks it, hides it again if Windows gave it new device
identifiers (another USB port, cable instead of receiver), restores the profile
and input routing the session needs, and replays the game's last trigger effect
and RGB colour. Rumble is deliberately not replayed. The Tray shows
"Waiting for controller" meanwhile. If the controller does not return in time,
is lost more than six times within 10 minutes, or is an APEX 6 Pro, the bridge
stops as before and the voluntary recovery below applies.

After a confirmed runtime disconnection that the bridge cannot ride out, Tray pauses automatic activation for
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
| **Alan Wake 2** | Press `View` / Hold `View` | Right-side Click (Mind Place / Writer's Room) / Left-side Click while held (Map) |
| **Standard DualSense** | Press `View` | Touchpad Click (no directional swipe) |

Death Stranding 2 uses the default Xbox layout: `View/Back` is forwarded without
an artificial hold delay, and the Photo Mode chord is consumed until both
buttons are released. `LB` and `Menu` alone retain their ordinary actions.
The left-side Photo Mode gesture is documented in the
[PlayStation guide](https://www.playstation.com/en-id/games/death-stranding-2-on-the-beach/death-stranding-2-on-the-beach-guide/);
the Xbox `LB + Menu` shortcut is corroborated by
[players on Steam](https://steamcommunity.com/app/3280350/discussions/1/809097865497552734/).

Rear buttons M1–M4 are not independent controls in a standard DualSense input
report. Assign them in Flydigi Space Station to a standard controller button
(forwarded to the game like any other button) or to a keyboard/mouse input
(produced by Space Station itself, which must then be running).
ApexSenseBridge keeps the verified mapping service authorized through HidHide
during an active session. A game, emulator or PC port that reads only the
DualSense ignores keyboard/mouse mappings, so use a gamepad-button mapping
there. A step-by-step check is in
[Troubleshooting](TROUBLESHOOTING.md#rear-buttons-m1m4-do-nothing-in-an-emulator-or-pc-port).

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
ApexSenseBridge.exe test-gyro [index] [--stream] [--seconds N]
ApexSenseBridge.exe test-profile-switch [index] [--target 1..4]
ApexSenseBridge.exe restore-controller-visibility
```

---

## Reporting a Problem

In the Tray, open **Settings** > **Send a report** and pick Bug, Feature or improvement, Unsupported game or Download the logs. A bug report prepares an anonymized installation report that you can preview before anything is published, and can be sent without a GitHub account. Please also attach:

- `tray_bridge.log` and `tray_detection.log` from `%LocalAppData%\ApexSenseBridge\logs\` (the report only holds the latest lines).
- Whether the controller is on the USB cable or the 2.4 GHz receiver.
- Whether Flydigi Space Station is running or fully closed.
- The game, its executable, and whether Steam Input is disabled for it.

The full checklist is in [Troubleshooting](TROUBLESHOOTING.md#9-what-to-attach-to-a-bug-report).

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
