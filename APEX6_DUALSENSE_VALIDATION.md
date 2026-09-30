# Apex 6 Pro DualSense validation (unreleased candidate)

This checklist is for hardware testing over the 2.4 GHz dongle. Keep Flydigi
Space Station closed during the session. Record whether Steam Input is enabled
for each game; when testing a game's native DualSense support, try its native
controller path first.

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
3. Test left and right grip events separately when the game offers them. Check
   that a quiet channel does not vibrate merely because the other is active.
4. In Zenless Zone Zero, first confirm that entry into gameplay completes.
   If it freezes, stop the bridge and report the last screen reached, elapsed
   time, and whether the game recovers. Do not keep repeating a frozen session.
   If gameplay works, repeat a specific action that should produce feedback.
   If nothing is felt, record the action and whether the game shows DualSense
   prompts. The new counters will distinguish non-silent PCM, trigger effects,
   and HID rumble from a stream of silent audio blocks.
5. Run at least one longer session and several clean start/stop cycles. If
   Windows says a device is unrecognized, capture the exact device instance ID
   and the event time. Note whether the physical controller, dongle, or virtual
   DualSense disappeared.
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
for comparison, and repeat a weak event at 0% threshold to identify gating.

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
