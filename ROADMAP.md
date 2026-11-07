# Roadmap

Next steps and ideas. No dates and no commitment: items under "To explore" are experiments and may be dropped.

## Done in 1.1.0

- APEX 6 Pro battery level and charge state are sent to the virtual DualSense, read when the session starts and refreshed every 15 seconds with self-timing safeguards.
- The APEX 6 Pro firmware versions are decoded and kept internally, without being displayed.
- A warning is printed at startup when another haptics engine (`XHaptics.exe`, `x-haptic-engine-fusion.exe`) is running.

## Next

- **APEX 6 battery refresh: validation on hardware.** The refresh is on (`kApex6PeriodicBatteryRefresh` in `src/cli/BridgeCommand.cpp`): every 15 seconds, one request, then zero-timeout polls spaced by 5 ms (8 reads at most per poll, 300 ms reply window). It times every controller call and switches itself off for the session after two calls over 5 ms or three misses in a row. The telemetry reports `battery_refresh_slow_calls`, `battery_refresh_max_call_us` and `battery_refresh_disabled`. Checked so far on a real APEX 6 Pro (dongle): the battery byte is filled and decoded (`identify` reported 60 %, after readings of 8/10 and 7/10 earlier the same day, so the scale is not 0-6); the charge flag and the "full" value are not checked, and no bridge session has run. To do: read these on a real APEX 6 during a bridge session and compare `forward_latency_us_p99` and `deadline_overruns` with the refresh on and off. Each poll submits and cancels a read; if that shows a cost, keep a read pending between passes, as the APEX 5 input source does.
- **Battery in the Tray.** Continuous icon with the percentage written inside, one colour per state (critical up to 10 %, low up to 20 %, normal, charging, full, unavailable). It needs a live channel from the bridge to the Tray (an extension of the session status block), the nine languages and the key-parity tests.
- **Firmware compatibility warning** for the APEX 6 Pro, using the stored versions, once a known incompatible version exists.

## To explore

- **Grip attenuation while a trigger is pressed.** Idea: strong grip vibration masks the trigger vibration. Implemented and switched off (`--apex6-grip-duck 0..100`, enabled by `kApex6ShapingOptionsEnabled` in `src/cli/BridgeOptions.h`). Needs A/B listening tests and the right press curve.
- **Trigger output ceiling**, possibly preceded by a gain stage. Implemented and switched off (`--apex6-trigger-ceiling 20..100`, same switch). On its own it can only lower the force.
- **Battery source arbitration.** In the XInput relay mode the generic gamepad reading overwrites the controller's own reading every 15 seconds. Also a staleness rule (45 s) and an age field in the telemetry.
- **Third-party control flag on the APEX 6**, the mechanism already used on the APEX 5, once confirmed on hardware.
- **Motor configuration check after a reconnection**, before streaming haptic frames again.
- **Embedded button remapping** (on-board profiles, issue #73). Large: it writes to the controller flash, so it needs backup, restore and checksum verification.
- **Reading buttons from the vendor input stream** if a firmware exposes them, as an alternative to the XInput fusion.

## Not planned

- Controller screen, firmware updates, racing-telemetry haptics.

## Switching the two experimental options back on

Set `kApex6ShapingOptionsEnabled` to `true` in `src/cli/BridgeOptions.h`, then restore the two options in `bridgeCommandUsage()` and in the help text of `src/cli/CliApplication.cpp`. The tests in `tests/test_bridge_options.cpp` already cover both states.
