# Apex 6 Pro DualSense validation — 1.0.0-beta.10 candidate

This checklist is for hardware testing over the 2.4 GHz dongle. Keep Flydigi
Space Station closed during the session. Record whether Steam Input is enabled
for each game; when testing a game's native DualSense support, try its native
controller path first.

Beta.10 does not require installing Flydigi's beta firmware or X-Haptics engine.
Keep both the Space Station and X-Haptics engine closed while using ASB. Record
controller/dongle firmware versions without changing them for this comparison.
Use the same connection, scene and 100% strength as your beta.9 reference.

## Identity and Windows audio configuration

- Run `ApexSenseBridge.exe --help` and confirm `Release: 1.0.0-beta.10`.
- Run `ApexSenseBridge.exe identify`. The regular Pro (`DeviceType 150/0x96`)
  and Phantom Blade Zero (`152/0x98`) are supported when both motor capabilities
  are advertised. Attach the exact refusal message for any other identity;
  ASB will not enable its motors. No firmware update is required to test this.
- Start the bridge with grip haptics enabled before launching the game. The
  startup log now reports `apex6_audio_format`, mix-channel count and channel
  masks. `quadraphonic`, four channels and mask `51` (`0x33`) are the expected
  layout; a stereo/missing/unknown result is not a successful audio check.
- If configuration is needed, leave the bridge running. Press `Win + R`, run
  `mmsys.cpl`, and select the **new virtual Wireless Controller** on the Playback
  tab, then **Configure > Quadraphonic**. Do not change your normal speakers or
  make the controller the default audio output. If similarly named real devices
  exist, identify the endpoint that appears when ASB starts; do not guess.
- The log is a startup snapshot, not a live monitor of subsequent manual changes.
  Record the Windows configuration too. After changing it, restart the game while
  leaving ASB running. Recheck configuration after a new bridge session because
  a recreated endpoint may not retain it. ASB does not write these properties.

## Start the bridge before the game

Use Playnite's prepared launch or the Tray controller diagnostics' prepared
`.exe` launch. The Tray waits for the bridge's ready signal before starting the selected
executable. Its manual session stays active after a launcher exits; turn off
**Force continuous activation** when the game is finished. Record whether the
game shows PlayStation or Xbox prompts. An Xbox prompt is useful evidence about
the input path, not proof that haptic translation failed.

## In-game checks

1. Confirm face buttons, sticks, touchpad click, LT, RT, and LT+RT work. Hold LT
   while repeatedly pressing RT, then reverse the order.
2. In Arknights: Endfield, compare one repeatable menu movement, one short
   impact, and the zipline trigger effect with a genuine DualSense if available.
   Note whether each effect is absent, weak, delayed, too long, or buzzy.
   For the zipline, repeat at least ten cycles: LT alone, RT alone, then LT+RT
   with LT pressed first and again with RT pressed first. Fully release between
   cycles, then hold both for several seconds. Compare the first and last cycle
   and report whether weakening returns. Include `apex6_trigger_both_frames`,
   left/right frame counts, the complete raw effect trace and firmware versions.
   `0x22` Bow should no longer be rejected when it matches the captured valid
   payload. A VCM provides a draw texture/snap, not DualSense static resistance.
3. Test left and right grip events separately when the game offers them. Check
   that a quiet channel does not vibrate merely because the other is active.
4. In Zenless Zone Zero, first confirm that entry into gameplay completes.
   If it freezes, stop the bridge and report the last screen reached, elapsed
   time, and whether the game recovers. Do not keep repeating a frozen session.
   If gameplay works, repeat a specific action that should produce feedback.
   If nothing is felt, record the action and whether the game shows DualSense
   prompts. The new counters will distinguish non-silent PCM, trigger effects,
   and HID rumble from a stream of silent audio blocks.
   Include the quadriphonic configuration and all four raw channel peaks.
   Silence on the two haptic channels must not be confused with weak output.
5. Run at least one longer session and several clean start/stop cycles. If
   Windows says a device is unrecognized, capture the exact device instance ID
   and the event time. Note whether the physical controller, dongle, or virtual
   DualSense disappeared.
   Close the game before the normal bridge restart test, then start ASB first
   and relaunch the game. If you also test restarting ASB with the game left
   running, report that separately: live input/audio reacquisition is not fixed
   or guaranteed, and the game may need restarting. Do not repeat a freeze.
6. After closing both game and bridge, retest the physical controller alone in
   the same game and in a simple rumble tester. Report these separately: a
   working rumble tester does not establish that native game haptics recovered.
   `apex_original_restored=yes` confirms visibility/isolation restoration, not
   a read-back verification of the controller's internal motor configuration.

## What to include with the report

Attach the bridge summary after stopping the session. The most useful fields are
`dualsense_trigger_reports`, `dualsense_rumble_reports`,
`apex6_trigger_left_updates`, `apex6_trigger_right_updates`,
`apex6_trigger_unsupported`, `apex6_trigger_both_frames`,
`apex6_waveform_left_active`, `apex6_waveform_right_active`,
`apex6_waveform_active_rendered`, `apex6_waveform_stale_drops`,
`apex6_waveform_maximum_age_us`, `apex6_write_failures`, and
`apex6_haptic_enables`/`apex6_haptic_disables`.

Include the `apex6_last_active_*`, `apex6_last_rejected_*`, and
`apex6_trigger_trace_*` lines (or the complete telemetry JSON). They preserve
the original 11-byte effects after the game sends an off command. A zero zone
mask is now a valid stop; `apex6_trigger_malformed` distinguishes invalid native
parameters, while `apex6_trigger_unsupported` remains the total rejected count.

For silent or weak haptics, include `virtual_backend`,
`apex6_raw_audio_measured_blocks`, `apex6_raw_audio_frames`, all four
`apex6_raw_*_peak` values, both `apex6_raw_haptic_*_rms` values,
`apex6_waveform_*_active_rms`, `apex6_waveform_silent_blocks`,
`apex6_waveform_*_thresholded` and `apex6_waveform_active_drops`.
The raw measurements are before the existing 48 kHz to 1 kHz filter; waveform
measurements are after it but before the user threshold/strength settings.
Raw RMS includes silence; filtered active RMS excludes wholly silent blocks,
so these RMS values do not have identical denominators. Zero measured blocks
means raw diagnostics are unavailable, not that the incoming audio was silent.
Record trigger/vibration strength and audio threshold as well; use 100% strength
for comparison. Beta.10 preserves native PCM regardless of the activation
threshold: an optional 12% versus 0% comparison of the same scene should no
longer remove quiet native details. The threshold still applies to envelope
fallback, not native PCM or ordinary HID rumble. The historical PCM threshold
counters now stay zero; include `apex6_waveform_threshold_policy` and
`apex6_bow_breaks`, plus the audio preflight fields or complete telemetry JSON.

Retest PRAGMATA's weapon break as a regression reference, and Endfield's zipline
specifically to validate the corrected native vibration-frequency byte. Do not
interpret additional counters as a confirmed fix for the ZZZ freeze.

The `virtual_input_*` readings are measurements only when
`virtual_input_monitor=enabled`. A zero with the monitor disabled does not mean
that the game received no controller input. The old `translated_effects` and
`audio_haptics_processed` counters belong to the Apex 4/5 bridges and are no
longer printed for Apex 6.

## Release gate

Do not treat automated tests as physical validation. The candidate needs a
confirmed prepared-launch session, independent LT/RT behavior, short weapon
break effects without a held buzz, non-silent left/right PCM in a known native
haptics scene, clean shutdown/restart, and no unexplained device disconnect.
