# APEX 4 experimental motion validation — 2026-09-30

## Scope and status

The new captures contain nonzero accelerometer and native gyro channels over
both wired USB and the dongle. The decoder now uses these native channels,
not the firmware mouse deltas. This is an experimental implementation, not
confirmed in-game support or factory-calibrated sensor output.

Both files identify one APEX 4 (`k2`, DeviceType 84, firmware `0x6837`). Other
firmware versions and units have not been validated. The tester must enable
gyro mapping **Mouse**, always on, in the active Flydigi onboard profile;
the earlier captures with mapping disabled contained zero IMU fields.
ASB does not change this profile automatically.

## Capture evidence

| Capture | Reports (still / yaw / pitch / roll) | HID delivery rate |
| --- | --- | --- |
| `apex4-gyro-wired (1).json` | 2532 / 2532 / 2533 / 2533 | about 506 reports/s |
| `apex4-gyro-dongle (1).json` | 3758 / 5032 / 5032 / 5032 | about 752 then 1006 reports/s |

Each phase lasts approximately five seconds. HID delivery rate is **not**
the IMU update rate: many successive reports repeat the same sensor values.
Dongle byte 3 alternates between `00` and `80`; it is not used as a sensor
freshness flag. No capture samples were dropped.

SHA-256 of the received files:

```text
wired:  5c3992bd952ea2bbd70ea320123fd6aa74ddaf9cfbf36868c57b0a94aa847906
dongle: 03766b287dfe14e0d92a460ded79a74df6f095f3f7bbac0a51608c5c82917a5e
```

## Decoding

Offsets are zero-based in a 32-byte `04 FE` state report. All listed values
are signed 16-bit little-endian, with the explicit exception that the yaw
bytes are not adjacent.

| Physical channel | Low / high byte | ASB output |
| --- | --- | --- |
| Pitch gyro | 26 / 27 | gyro X = raw × 9 |
| Yaw gyro | 18 / 20 | gyro Y = raw × 5 |
| Roll gyro | 29 / 30 | gyro Z = −raw × 12 / 5 |
| Acceleration X | 11 / 12 | accel X = −raw × 25 / 2 |
| Acceleration Y | 13 / 14 | accel Z = raw × 25 / 2 |
| Acceleration Z | 15 / 16 | accel Y = raw × 25 / 2 |

All outputs saturate to signed 16-bit limits. Division truncates toward zero.
Bytes 17 and 19 remain sticks; they must not be consumed as yaw bytes.
Bytes 4–6 are firmware mouse channels. The packed pitch field tracks native
pitch at approximately −8×, but the previous packed yaw interpretation has
artificial discontinuities. Neither mouse channel is used by the new decoder.
The native gyro offsets also match the V1 layout in the
[official SDL Flydigi driver](https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/hidapi/SDL_hidapi_flydigi.c).
SDL's sensor gains for other V1 models are not treated as APEX 4 calibration.

At rest, physical Z averages about 799.4 counts (wired) and 799.2 (dongle),
supporting approximately 800 counts/g. Face-up gravity is mapped to ASB Y.
Pitch and yaw are zero at rest; raw roll ranges about −4 to +3 counts.
This confirms nonzero gravity without imposing a synthetic gravity value
when the firmware supplies an entirely zero IMU.

## Provisional gyro gains and orientation

The gains were estimated from the relationship between angular velocity and
the changing gravity vector, not from a known-angle calibrated rotation.
Analysis skips the first 0.3 seconds, resamples at 10 ms, smooths with an
11-sample moving average, and fits `d(g)/dt = g cross omega` for the
normalized accelerometer vector. Combined fits give approximately:

- Wired physical pitch / roll / yaw: −0.475 / −0.117 / +0.233 degrees/s/count.
- Dongle physical pitch / roll / yaw: −0.441 / −0.114 / +0.269 degrees/s/count.

The fit explains about 87% of the measured gravity-vector derivative energy.
Pitch-only estimates are around 0.43–0.44 degrees/s/count and roll around
0.114–0.116. The chosen provisional magnitudes are 0.45 / 0.25 / 0.12 for
pitch / yaw / roll, expressed in the existing ASB scale of 20 units/degree/s.
Yaw is least constrained: gravity cannot measure pure rotation about the
vertical axis. Translation, smoothing and mixed-axis movements also affect
this estimate. These values must not be presented as factory calibration.

The phase named `yaw` changes gravity strongly and excites the roll channel;
the phase named `roll` primarily excites yaw while gravity stays nearer flat.
This suggests those two gestures were interchanged, rather than proving an
axis assignment from their labels. Capture prompts now distinguish them.
The proposed Sony-frame gyro signs still need a physical direction check.

## Verification and remaining hardware checks

Protocol regression tests replay actual wired and dongle reports, including
positive/negative native values, both byte-3 variants, split-yaw stick
independence, mouse-field independence, saturation, disabled IMU, and invalid
or short reports. Diagnostic tests detect native-only motion and keep their
assertions enabled in Release builds.

On the tester's APEX 4, run from the updated engine folder:

```powershell
.\ApexSenseBridge.exe test-gyro --stream
```

Stop the stream with Ctrl+C. Alternatively, use `test-gyro --seconds 20`
without `--stream` for a timed summary.
Leave it flat for several seconds, then separately pitch, yaw and roll in
both directions. Expected: face-up acceleration is mainly Y, gyro returns
near zero at rest, and each gesture primarily changes its corresponding
gyro channel. Test USB and dongle separately. Then test the virtual DualSense
in a gyro-capable game and report inverted axes, excessive/weak sensitivity,
drift and unwanted mouse movement. `test-gyro` checks decoding, not virtual
DualSense/game integration. Do not run the bridge concurrently with capture.

For calibration follow-up, record well-separated movements with a known
angle and duration (especially yaw), plus clearly described start/end poses
or a video. No Bluetooth compatibility or automatic gyro activation is
claimed by this implementation.
