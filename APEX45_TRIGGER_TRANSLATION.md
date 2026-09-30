# APEX 4/5 native trigger translation — issue #22

The APEX 4/5 bridge now decodes native DualSense effects instead of recognizing
individual byte patterns differently for LT and RT. The wire format follows
[Nielk1's trigger effect generators](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db).

| DualSense mode | Decoded fields | FORCEADAPT approximation |
|---|---|---|
| Feedback `0x21` | Ten-bit zone mask; ten packed three-bit strengths plus one | Race at the first enabled zone, using the highest enabled strength |
| Weapon `0x25` | Two zone bits; low three strength bits plus one | SniperBreak at the first zone, with resistance length equal to the second zone's stroke minus the first zone's stroke |
| Vibration `0x26` | Ten-bit zone mask; packed amplitudes plus one; frequency at effect byte 9 | RecoilRattle at the first enabled zone, using peak enabled amplitude and the requested frequency |

Zones are mapped to the documented Space Station stroke range 0–192 by
rounding `zone * 192 / 10`. Resistance and weapon strength use eight bounded
levels from 8 to 64; vibration amplitude uses eight levels from 15 to 120.
These retain conservative force ceilings comparable to the previous native
translations. Their physical feel still needs validation: they are conversion
choices, not a claim that Flydigi and DualSense force units are equivalent.

The APEX 4/5 SetForceTrigger command used here has no field for ten independently
controlled zone strengths in the inspected Space Station SDK. This is not proof
that every firmware or undiscovered command lacks that capability. The separate
K6 SDK has a ten-segment mapping command, gated to K6 devices, not APEX 4/5.
The approximation does not reproduce a sparse mask's gaps or its complete
strength curve. Inactive zone strengths do not affect the selected peak.
No native trigger depends on grip-rumble bytes. Effect byte 10 is reserved and
does not provide the vibration frequency.

Space Station's breakthrough control is a resistance length, despite the
protobuf field being named `End`. For zones 2 and 7, the mapped positions are
38 and 134, so the command must use `{38, 96, strength, 0, 0}`, not an absolute
endpoint of 134. The official UI and SDK support this correction; the linear
zone-to-stroke conversion and force scales still require physical calibration.
See [the command inventory and evidence](APEX45_FORCEADAPT_RESEARCH.md)
for the protocol distinctions and the proposed validation sequence.

An empty native mask, zero vibration frequency, or explicit `0x05` off sends
Normal. A zero packed strength in an enabled zone means level 1, not off.
Absent LT/RT enable bits never change that side. Mode `0x00`, unknown modes,
invalid high zone bits, and weapon masks with other than two bits retain the
existing no-update behavior; no speculative reset policy is introduced.
Legacy modes `0x01`, `0x02`, and `0x06` retain their existing translation.
The third translation argument is retained for caller compatibility and ignored.
The APEX 6 native voice-coil decoder and renderer are unchanged.

Regression tests cover both sides, all relevant payload fields, stops followed
by identical active effects, independent enable flags, rumble independence,
deduplication, write failures, and all 45 ordered two-zone weapon masks accepted
by the translator. Simulated verified APEX 4 and APEX 5
transports verify the actual HID packet layout without driving physical motors.
Assertions remain enabled in Release builds.

Before releasing the next beta, validate aiming, aim-and-fire, weapon changes,
release, menus, and session shutdown in Horizon Zero Dawn Remastered and
Horizon Forbidden West on APEX 5. Check APEX 4 and previously working games for
changes in force or break position. These tests establish protocol behavior;
they do not establish that the two game-specific reports are fully resolved.

Guided APEX 5 dongle captures on 2026-09-30 qualitatively confirmed that a
breakthrough length of 96 felt longer than 39 at the same start and force.
Vibration at amplitude 30 was not perceived, while 60 was, with other requested
parameters held constant. This identifies a calibration need, not a universal
minimum amplitude or a proven Horizon fix. No force floor has been introduced;
see the research report for the captures, user observations, and measurement limits.
