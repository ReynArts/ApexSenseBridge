# ApexSenseBridge 1.1.0 — Major Update Release Notes

ApexSenseBridge 1.1.0 is a major update, even though the version number only moves from 1.0 to 1.1. It extends the APEX 6 Pro support, adds a controller picker and makes automatic activation far more robust. The complete list is in `CHANGELOG.md`.

**Trigger effects have been reworked on the APEX 6 Pro and the APEX 5** so that each DualSense effect follows what it means. The APEX 4 triggers are unchanged.

---

## 1. Flydigi APEX 6 Pro

- **Motion sensors:** gyroscope and accelerometer are forwarded to the virtual DualSense; `test-gyro` supports the APEX 6 Pro.
- **Battery:** real level and charge state instead of a fixed 100 %, refreshed every 15 seconds with self-timing safeguards.
- **Onboard profile slots 1–4:** `--apex-profile` and `test-profile-switch`, with the original slot restored and verified.
- **Trigger effects translated into vibration by meaning:** a burst at the break point for weapon effects, a draw and release burst for Bow, smooth zone transitions for progressive resistance and vibration, rhythms for Machine and Galloping.
- **Twelve weapon presets** in the Tray trigger test and `test-trigger --mode <preset>`; `test-trigger` plays the real native effect and follows your trigger.
- **Cleaner release:** the trigger hum kept by newer firmware after a release is stopped ([#74](https://github.com/ReynArts/ApexSenseBridge/issues/74)).

## 2. All controllers

- **Controller picker:** with several APEX controllers connected, choose the one to bridge from the Tray top bar (or View on a gamepad). One controller is bridged at a time; couch co-op is not supported. Sessions no longer fail with several controllers connected ([#70](https://github.com/ReynArts/ApexSenseBridge/issues/70)).
- **Power-cycle recovery (APEX 4 and APEX 5):** the virtual DualSense stays connected with neutral input while the controller is off, asleep or unplugged, for up to 10 minutes. Validated on hardware on the APEX 5; not yet on the APEX 4.
- **Robust automatic activation:** elevated and anti-cheat protected games are no longer taken for closed ([#67](https://github.com/ReynArts/ApexSenseBridge/issues/67)); repeated start failures pause automatic starts instead of looping.
- **Per-game tuning and reset buttons**, a **new report window** (categories, full logs, anonymous sending), **manual-fix notes** in nine languages, an Alan Wake 2 touchpad profile and `--force-always-on`.
- **Competing engine warning** when Flydigi X-Haptics is running; nothing is stopped.

## 3. APEX 5 and APEX 4

- **APEX 5:** per-zone and rhythmic trigger emulation, on by default (**Trigger effect emulation** switch or `--no-zone-emulation` to turn it off); Bow releases at its end zone; lightbar sync keeps the Space Station brightness, with a max-brightness slider.
- **APEX 4:** rumble updates spaced 10 ms instead of 25 ms (`ASB_APEX4_RUMBLE_MS=25` restores the previous cadence); far fewer identity requests.

## 4. Known limits

- The APEX 6 Pro does not ride out a controller power cycle, and RGB lightbar sync is still refused on it.
- Bridging two controllers at once is not supported.
