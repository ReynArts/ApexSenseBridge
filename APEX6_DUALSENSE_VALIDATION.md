# Apex 6 Pro DualSense validation (unreleased candidate)

This checklist is for hardware testing over the 2.4 GHz dongle. Keep Flydigi
Space Station closed during the session. Record whether Steam Input is enabled
for each game; when testing a game's native DualSense support, try its native
controller path first.

## Start the bridge before the game

Use Playnite's prepared launch or the Tray dashboard's **Launch a game (.exe)**
button. The Tray waits for the bridge's ready signal before starting the selected
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
4. In Zenless Zone Zero, repeat a specific action that should produce feedback.
   If nothing is felt, record the action and whether the game shows DualSense
   prompts. The new counters will distinguish non-silent PCM, trigger effects,
   and HID rumble from a stream of silent audio blocks.
5. Run at least one longer session and several clean start/stop cycles. If
   Windows says a device is unrecognized, capture the exact device instance ID
   and the event time. Note whether the physical controller, dongle, or virtual
   DualSense disappeared.

## What to include with the report

Attach the bridge summary after stopping the session. The most useful fields are
`dualsense_trigger_reports`, `dualsense_rumble_reports`,
`apex6_trigger_left_updates`, `apex6_trigger_right_updates`,
`apex6_trigger_unsupported`, `apex6_trigger_both_frames`,
`apex6_waveform_left_active`, `apex6_waveform_right_active`,
`apex6_waveform_active_rendered`, `apex6_waveform_stale_drops`,
`apex6_waveform_maximum_age_us`, `apex6_write_failures`, and
`apex6_haptic_enables`/`apex6_haptic_disables`.

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
