# APEX 6 Pro Per-Game PCM Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement per-game PCM amplitude peak measurement and linear proportional normalization for Flydigi APEX 6 Pro, scaling 1kHz haptic PCM linearly (e.g., 30% reference peak to 90% target peak = exact 3.0x scaling) while maintaining backward compatibility with APEX 4/5, adaptive triggers, conventional rumble, and legacy companding.

**Architecture:**
- C++ Engine: Define `Apex6PcmMode` (`Original`, `Companding`, `Linear`, `Normalize`) and dedicated `Apex6PcmScaler` to compute exact linear gain with 16-bit to 8-bit saturating quantization and clipping telemetry. Add CLI options (`--apex6-pcm-mode`, `--apex6-pcm-linear-gain-percent`, `--apex6-pcm-reference-percent`, `--apex6-pcm-target-percent`, `--apex6-pcm-max-gain-percent`) to `BridgeOptions`, wire through `Apex6HapticBridge`, and extend `BridgeTelemetry` JSON schema.
- C# Tray: Extend `TraySettings` with `Apex6PcmGlobal` and per-game `Apex6PcmGames` settings dictionary keyed by canonical normalized game IDs (`SupportedGame.Normalized`). Update `EngineSessionManager` and `ProcessMonitorService` to pass canonical game keys and build command-line flags. Implement WPF UI in `GameListWindow` for global and per-game mode selection, target peak slider/numeric input, measurement workflow/status, and save/reset.

**Tech Stack:** C++20 (MSVC / standard libraries), C# (.NET Framework 4.6.2 / WPF), Python (verification harness)

**Spec:**
- `ApexSenseBridge_APEX6_PCM_Normalization_Design_KR.md`
- `ClaudeCode_Apex6_PCM_Implementation_Prompt_KR.md`

## Global Constraints

- **Bit-identical baseline:** When new PCM options are omitted, output must be bit-identical to stable 1.0.0.
- **Legacy flag compatibility:** `--apex6-haptic-gain 100..200` must produce bit-identical companded output and cannot be silently combined with conflicting new options.
- **Strict linear scaling:** `gain = targetPeak / referencePeak`. A 10% input scales to 30%, 20% to 60%, 30% to 90% for ref=30%, target=90% (allowed quantization tolerance: ±1 int8 LSB).
- **Actuator limits:** Output is clamped to saturating `int8 [-127, 127]`; clipped samples and processed samples must be counted and reported in telemetry. Initial experimental max gain cap is 4.0x (configurable, not a hardware safety certification).
- **Stereo preservation:** Both channels share the identical common gain based on `max(abs(left), abs(right))`; no cross-channel bleed or alteration of frequency/timing.
- **No touch of shared scaling:** Do NOT modify `EffectStrength.h::scaleEffectStrength()` or change APEX 4/5 rumble/trigger parameters.
- **Hardware safety:** Never push to GitHub or execute destructive hardware stress tests without explicit user approval. Clearly flag any component that is verified via emulation/unit-testing rather than physical hardware.

## Review Focus

1. `INT16_MIN` (`-32768`) input: `abs(-32768)` in 16-bit integer overflows; calculation must use 64-bit integers or tested arithmetic without undefined behavior.
2. Zero or invalid reference peak (`referencePercent == 0` or missing): Must never divide by zero; must safely fallback to `Original` mode with telemetry status indicating `invalid`.
3. Clipping on late high-amplitude events: When events exceed the reference peak during gameplay, samples must smoothly saturate to `±127` without sign flip or wrapping, incrementing `pcm_output_clipped_samples`.
4. Downward scaling (`reference = 100%`, `target = 90%`): Linear gain `0.9x` must be allowed and correctly attenuate signals below 1.0x; gain must not be artificially clamped to `>= 1.0x`.
5. Configuration isolation: Settings for Game A must never leak into Game B; unconfigured games must default to global settings, and unrecognized processes must safely fall back to `Original`.

---

### Task 0: Baseline & Environment Diagnosis (Phase 0)

**Files:**
- Create: `docs/APEX6_PCM_DIAGNOSTIC_ANALYSIS_KR.md`
- Test: `tests/verify_pcm_baseline.py`

**Interfaces:**
- Consumes: `APEX6_DUALSENSE_VALIDATION.md`, `src/dualsense/Apex6PcmGain.h`, `src/dualsense/Apex6HapticBridge.cpp`
- Produces: Baseline diagnostic report documenting the 48kHz -> 1kHz -> 8-bit pipeline, ZZZ vs Endfield/PRAGMATA metrics, and environment build tool capabilities.

- [ ] **Step 1: Write diagnostic verification script in Python to analyze sample ZZZ peaks and companding behavior**

Create `tests/verify_pcm_baseline.py` to simulate `scaleApex6Pcm(10028, gain)` vs linear gain (30% -> 90%) and confirm the mathematical difference documented in `APEX6_DUALSENSE_VALIDATION.md`.

- [ ] **Step 2: Run verification script**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/verify_pcm_baseline.py`
Expected: Output shows peak 10028 with legacy companding=200 produces 59 (out of 127, ~46%), whereas 3.0x linear scaling produces 116 (out of 127, ~91.3%), confirming the exact problem statement.

- [ ] **Step 3: Document findings in `docs/APEX6_PCM_DIAGNOSTIC_ANALYSIS_KR.md`**

Write analysis detailing:
- Data path: Virtual DualSense 48kHz audio -> 1kHz low-pass decimated int16 PCM -> `Apex6HapticBridge` queue -> `renderBlock` scaling -> `writeApex6Haptics` (+128 offset uint8).
- APEX 4/5 vs APEX 6 difference (envelope energy translation vs native PCM streaming).
- Root cause of weak sensation in games like ZZZ: game-side low peak amplitude (~30%) combined with non-linear companding compressing high amplitudes rather than linear proportional expansion.
- Local host environment assessment (no local MSVC/CMake, requires Python-based verification test harnesses and precision code authoring).

---

### Task 1: C++ PCM Normalization Core Scaler (Phase 1A)

**Files:**
- Create: `src/dualsense/Apex6PcmNormalization.h`
- Modify: `src/dualsense/Apex6PcmGain.h:1-25`
- Test: `tests/test_apex6_pcm_normalization.py`

**Interfaces:**
- Consumes: `std::int16_t` PCM samples
- Produces:
  ```cpp
  namespace asb::dualsense {
  enum class Apex6PcmMode : std::uint8_t {
      Original = 0,
      Companding = 1,
      Linear = 2,
      Normalize = 3
  };

  struct Apex6PcmConfig {
      Apex6PcmMode mode = Apex6PcmMode::Original;
      unsigned linearGainPercent = 100;
      unsigned referencePercent = 0;
      unsigned referencePeakAbs16 = 0;
      unsigned targetPercent = 90;
      unsigned maxGainPercent = 400;
      unsigned legacyGainPercent = 100;
  };

  struct Apex6PcmScalingState {
      double effectiveGainRatio = 1.0;
      double requestedGainRatio = 1.0;
      bool gainLimited = false;
      bool valid = true;
      std::string status = "valid";
  };

  class Apex6PcmScaler {
  public:
      explicit Apex6PcmScaler(const Apex6PcmConfig& config = {}) noexcept;
      [[nodiscard]] std::int8_t scaleSample(std::int16_t sample, bool& clipped) noexcept;
      [[nodiscard]] const Apex6PcmScalingState& state() const noexcept;
      [[nodiscard]] const Apex6PcmConfig& config() const noexcept;
  };
  }
  ```

- [ ] **Step 1: Write test specification for `Apex6PcmScaler` in Python test script**

Create `tests/test_apex6_pcm_normalization.py` testing:
1. `Original` mode: bit-identical to `sample * 127 / (sample >= 0 ? 32767 : 32768)`.
2. `Companding` mode: bit-identical to legacy `scaleApex6Pcm(sample, gain)`.
3. `Linear` mode: gain 100% = 1.0x, gain 300% = 3.0x, gain 50% = 0.5x.
4. `Normalize` mode: ref=30%, target=90% -> G=3.0x: 10%->30%, 20%->60%, 30%->90% (±1 LSB).
5. Boundary conditions: `INT16_MIN` (-32768), 0, `INT16_MAX` (32767).
6. Saturation and clipping: samples exceeding target saturate to ±127 and set `clipped = true`.
7. Zero/invalid reference: `referencePercent = 0` falls back to `Original` with `valid = false`.
8. Max gain clamp: ref=10%, target=90% -> requested 9.0x, effective clamped to 4.0x (`gainLimited = true`).

- [ ] **Step 2: Run test to verify expected behavior**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/test_apex6_pcm_normalization.py`
Expected: PASS all mathematical assertions.

- [ ] **Step 3: Implement `src/dualsense/Apex6PcmNormalization.h`**

Implement `Apex6PcmConfig`, `Apex6PcmScalingState`, and `Apex6PcmScaler` header:
- Calculate `requestedGainRatio` and `effectiveGainRatio` cleanly.
- Ensure 64-bit arithmetic `std::int64_t` avoids any overflow on `sample = -32768`.
- In `scaleSample`, clamp result to `[-127, 127]`, set `clipped = true` if result was saturated outside `[-127, 127]`.
- Keep `Original` mode using exact integer arithmetic `static_cast<std::int8_t>(static_cast<std::int32_t>(sample) * 127 / fullScale)`.

- [ ] **Step 4: Update `tests/test_apex6_haptic_bridge.cpp` to include unit test for `Apex6PcmScaler`**

Add `testApex6PcmNormalization()` to `tests/test_apex6_haptic_bridge.cpp` testing the new C++ API directly.

---

### Task 2: CLI Options & Validation in C++ Engine (Phase 1B)

**Files:**
- Modify: `src/cli/BridgeOptions.h:20-55`
- Modify: `src/cli/BridgeOptions.cpp:140-340`
- Test: `tests/test_bridge_options_apex6_pcm.py`

**Interfaces:**
- Consumes: CLI args `--apex6-pcm-mode`, `--apex6-pcm-linear-gain-percent`, `--apex6-pcm-reference-percent`, `--apex6-pcm-target-percent`, `--apex6-pcm-max-gain-percent`, `--apex6-haptic-gain`
- Produces: Populated `BridgeCommandOptions::apex6PcmConfig` with validation and mutual exclusion rules.

- [ ] **Step 1: Write failing test script for CLI options**

Create `tests/test_bridge_options_apex6_pcm.py` testing:
1. Parsing `--apex6-pcm-mode normalize --apex6-pcm-reference-percent 30 --apex6-pcm-target-percent 90 --apex6-pcm-max-gain-percent 400`.
2. Parsing `--apex6-pcm-mode linear --apex6-pcm-linear-gain-percent 150`.
3. Legacy `--apex6-haptic-gain 150` sets mode to `Companding` and `legacyGainPercent = 150`.
4. Conflict rejection: `--apex6-haptic-gain 150` AND `--apex6-pcm-mode linear` fails with explicit error.
5. Missing reference fallback/warning: `--apex6-pcm-mode normalize` without reference peak.
6. Range validation: reference 1..100, target 1..100, max-gain 100..400, linear-gain 0..400.

- [ ] **Step 2: Run test to confirm expectations**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/test_bridge_options_apex6_pcm.py`
Expected: PASS.

- [ ] **Step 3: Modify `src/cli/BridgeOptions.h` and `src/cli/BridgeOptions.cpp`**

- Add `asb::dualsense::Apex6PcmConfig apex6PcmConfig;` and `bool apex6PcmModeExplicit = false;` to `BridgeCommandOptions`.
- Parse the 5 new `--apex6-pcm-*` flags.
- Check conflict with `--apex6-haptic-gain`.
- Update `bridgeCommandUsage()` to list the new options.

- [ ] **Step 4: Update `tests/test_bridge_options.cpp` with C++ test cases**

Add test cases in `tests/test_bridge_options.cpp` verifying CLI parsing and error cases.

---

### Task 3: Engine Pipeline Integration & Telemetry (Phase 2)

**Files:**
- Modify: `src/dualsense/Apex6HapticBridge.h:90-125`
- Modify: `src/dualsense/Apex6HapticBridge.cpp:485-535`
- Modify: `src/cli/BridgeCommand.cpp:360-375`
- Modify: `src/cli/BridgeTelemetry.h:30-60`
- Modify: `src/cli/BridgeTelemetry.cpp:180-240`
- Test: `tests/test_apex6_telemetry.py`

**Interfaces:**
- Consumes: `DualSenseFeedback` waveform blocks, `Apex6PcmConfig`
- Produces: Scaled motor block output, clip sample counters, telemetry JSON schema extension with `pcm_mode`, `pcm_reference_peak_abs16`, `pcm_target_percent`, `pcm_requested_gain_ratio`, `pcm_effective_gain_ratio`, `pcm_gain_limited`, `pcm_output_clipped_samples`, `pcm_processed_samples`, `pcm_measurement_status`, `pcm_observed_left_peak_abs16`, `pcm_observed_right_peak_abs16`.

- [ ] **Step 1: Write telemetry test script**

Create `tests/test_apex6_telemetry.py` verifying that:
1. Given an `apex6` telemetry JSON output, all new fields are present and valid JSON.
2. Older fields (`pcm_gain_percent`, `waveform_left_peak`, `pcm_output_left_peak`) remain intact.
3. If no PCM received, `pcm_measurement_status` is `"unavailable"`.

- [ ] **Step 2: Update `Apex6HapticBridge.h` and `Apex6HapticBridge.cpp`**

- Replace single `unsigned pcmGainPercent_` with `Apex6PcmScaler pcmScaler_`.
- Add `std::atomic_uint64_t pcmOutputClippedSamples_{0};` and `std::atomic_uint64_t pcmProcessedSamples_{0};` to `Apex6HapticBridgeStats`.
- In `renderBlock()`:
  Scale `waveform->left[index]` and `waveform->right[index]` using `pcmScaler_.scaleSample(...)`.
  Count processed samples and clipped samples.
- In `BridgeCommand.cpp`: pass `options.apex6PcmConfig` to `Apex6HapticBridge`.

- [ ] **Step 3: Update `BridgeTelemetry.h` and `BridgeTelemetry.cpp`**

- Add the new PCM normalization stats fields to `BridgeTelemetry::apex6Stats`.
- Serialize the new fields into the `"apex6"` JSON block in `BridgeTelemetry.cpp`.

- [ ] **Step 4: Update `tests/test_bridge_telemetry.cpp`**

Add test assertions for the extended JSON schema.

---

### Task 4: C# Tray Settings & Game Key Plumbing (Phase 3A)

**Files:**
- Modify: `ApexSenseBridgeTray/Models/TraySettings.cs`
- Modify: `shared/BridgeArguments.cs`
- Modify: `ApexSenseBridgeTray/Services/EngineSessionManager.cs`
- Modify: `ApexSenseBridgeTray/Services/ProcessMonitorService.cs`
- Test: `tests/test_tray_pcm_settings.py`

**Interfaces:**
- Consumes: JSON deserialization of `tray_settings.json`, game detection events
- Produces:
  ```csharp
  public class Apex6PcmConfig
  {
      public string Mode { get; set; } = "original";
      public int LinearGainPercent { get; set; } = 100;
      public int TargetPeakPercent { get; set; } = 90;
      public int MaxGainPercent { get; set; } = 400;
  }

  public class Apex6PcmGameSettings
  {
      public bool UseGlobal { get; set; } = true;
      public string Mode { get; set; } = "normalize";
      public int MeasuredReferencePeakAbs16 { get; set; }
      public int MeasuredReferencePercent { get; set; }
      public int TargetPeakPercent { get; set; } = 90;
      public int MaxGainPercent { get; set; } = 400;
      public int LinearGainPercent { get; set; } = 100;
      public string MeasuredUtc { get; set; }
      public int MeasurementVersion { get; set; } = 1;
      public int ObservedLeftPeakAbs16 { get; set; }
      public int ObservedRightPeakAbs16 { get; set; }
      public int ObservedActiveBlocks { get; set; }
      public string ObservedStatus { get; set; } = "none";
  }
  ```

- [ ] **Step 1: Write test script for Tray settings & arguments**

Create `tests/test_tray_pcm_settings.py` testing:
1. Backward compatibility: old `tray_settings.json` without PCM settings loads with safe defaults.
2. Saving and reloading global and game-specific settings.
3. Game key lookup uses canonical `SupportedGame.Normalized` (case-insensitive).
4. Unconfigured game inherits global settings; `UseGlobal = false` uses game override.
5. Command-line generation: `BridgeArguments.Build` produces exact `--apex6-pcm-*` arguments.

- [ ] **Step 2: Run test script**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/test_tray_pcm_settings.py`
Expected: PASS.

- [ ] **Step 3: Modify `TraySettings.cs`**

- Add `Apex6PcmGlobal` and `Apex6PcmGames` properties.
- Add helper methods: `GetApex6PcmSettings(string normalizedKey)` and `SetApex6PcmSettings(string normalizedKey, Apex6PcmGameSettings settings)`.
- Ensure compatibility with `JavaScriptSerializer` (no C# 6 property initializers if built with older compiler, or ensure compatible syntax).

- [ ] **Step 4: Update `BridgeArguments.cs` & `EngineSessionManager.cs`**

- Add overload to `BridgeArguments.Build` that accepts `Apex6PcmConfig` or PCM parameters.
- In `EngineSessionManager`:
  Update `StartSession` and `StartSessionCore` to accept `string gameKey = null`.
  In `BuildArguments`, resolve game PCM settings via `settings.GetApex6PcmSettings(gameKey)` and append flags.
- In `ProcessMonitorService`:
  Pass `game.Normalized` as `gameKey` when calling `sessionManager.StartSession`.

- [ ] **Step 5: Update `tests/tray_learning_tests.cs`**

Add unit tests for `GetApex6PcmSettings`, argument generation, and migration in `tray_learning_tests.cs`.

---

### Task 5: Tray WPF UI Implementation (Phase 3B)

**Files:**
- Modify: `ApexSenseBridgeTray/GameListWindow.xaml`
- Modify: `ApexSenseBridgeTray/GameListWindow.xaml.cs`
- Modify: `ApexSenseBridgeTray/Resources/Languages/en.json`
- Modify: `ApexSenseBridgeTray/Resources/Languages/ko.json` (or other language resources)
- Test: `tests/test_ui_contracts.py`

**Interfaces:**
- Consumes: User UI interaction (mode radio buttons/combobox, target peak slider, measurement button)
- Produces: Updated `TraySettings`, clear measurement status display, visual indication of effective gain multiplier.

- [ ] **Step 1: Write UI contract verification test**

Create `tests/test_ui_contracts.py` verifying that:
1. Localization keys for new UI elements exist across language JSON files (`en.json`, `ko.json`, etc.).
2. Multiplier calculation matches mathematical spec (`target / ref`, clamped to `maxGain`).
3. UI strings reflect that settings apply on next session restart and that measurement is "observed peak" not "hardware maximum".

- [ ] **Step 2: Run test script**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/test_ui_contracts.py`
Expected: PASS.

- [ ] **Step 3: Add XAML controls in `GameListWindow.xaml`**

- In Global Settings (`PnlControllerEffects` under APEX 6):
  Add mode selection (`Original`, `Companding`, `Linear`, `Normalize`).
  Add Linear Gain slider (100%..400%).
  Add Target Peak slider (10%..100%).
  Add Max Gain slider (100%..400%).
  Display calculated effective multiplier.
- In Game Detail panel (`PnlDetailContent`):
  Add `[x] Use Global Settings` toggle.
  When unchecked, show game-specific Mode selector, Measured Reference Peak display, Target Peak slider, and calculated multiplier.
  Add `[Start Measurement]` / `[Save Measurement]` / `[Reset]` buttons.
  Add Measurement Status text (showing audio channel status, active drops, valid samples).

- [ ] **Step 4: Implement event handlers and binding in `GameListWindow.xaml.cs`**

- Wire up event handlers for sliders, textboxes, and buttons.
- Update `TxtDetail...` displays dynamically when sliders move.
- Implement measurement trigger: when user clicks `Start Measurement`, trigger measurement run or parse session telemetry.
- On save, commit to `TraySettings` and persist to disk via `SettingsManager.Save(settings)`.

- [ ] **Step 5: Update language files**

Add localized strings for English (`en.json`) and Korean (`ko.json`) for all new labels, tooltips, and warnings.

---

### Task 6: Full Integration, Regression & Verification (Phase 4)

**Files:**
- Create: `tests/test_apex6_pcm_integration.py`
- Create: `docs/APEX6_PCM_NORMALIZATION_SUMMARY_KR.md`

**Interfaces:**
- Consumes: All updated components
- Produces: End-to-end automated test suite verifying all 12 test requirements from Design Document Section 8, plus documentation of hardware testing procedures and residual risks.

- [ ] **Step 1: Write full suite integration test in `tests/test_apex6_pcm_integration.py`**

Test the 12 requirements from Design Section 8:
1. Math: 30% -> 90% gives exact 3.0x (10->30%, 20->60%, 30->90%).
2. Mode bit-identical: Original & Companding match legacy outputs bit-for-bit.
3. Negative values: INT16_MIN (-32768), 0, extremes without overflow.
4. Clipping: Post-reference high amplitudes saturate cleanly to ±127 and count clips.
5. Channel balance: L=10%, R=5%, common gain=3.0x -> L=30%, R=15%.
6. Data missing: ref=0/missing falls back to Original safely.
7. Settings persistence: serialize/deserialize/migration round-trip.
8. Game key isolation: Game A settings never leak to Game B.
9. Input validation: out-of-range CLI and UI inputs rejected safely.
10. Regression: APEX 4/5, adaptive triggers, conventional rumble unaffected.
11. Telemetry integration: JSON schema valid with all expected metrics.
12. Performance: O(1) per-sample scaling without heap allocation in render loop.

- [ ] **Step 2: Run complete integration test**

Run: `& "C:\Users\User\AppData\Local\Python\bin\python.exe" tests/test_apex6_pcm_integration.py`
Expected: 100% of integration checks pass.

- [ ] **Step 3: Write user summary document `docs/APEX6_PCM_NORMALIZATION_SUMMARY_KR.md`**

Document:
- List of modified and created files.
- Exact CLI flags and usage examples.
- Tray UI guide and measurement wizard instructions.
- Test verification results.
- What has been verified via software test harnesses vs. what requires physical APEX 6 Pro hardware validation.
- Differences from prior codebase.
- Explicit confirmation that no GitHub push, PR, driver change, or firmware modification was performed.
