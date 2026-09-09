# ApexSenseBridge 0.6.2

ApexSenseBridge 0.6.2 is a driver-safety, controller-isolation and launcher
reliability release for Flydigi APEX 4 and APEX 5 on Windows.

## Reported issues addressed

- [#1 — APEX rear buttons](https://github.com/ReynArts/ApexSenseBridge/issues/1):
  documents that M1–M4 must be assigned through Flydigi Space Station. Their
  mapped controller or keyboard/mouse actions remain usable, but a standard
  virtual DualSense cannot expose four independent Flydigi-only buttons.
- [#2 — Touchpad click and false game detection](https://github.com/ReynArts/ApexSenseBridge/issues/2):
  keeps `View` mapped directly to touchpad click, protects rapid consecutive
  presses, prevents Tray/Playnite session replacement and rejects weak
  short-title matches such as Controlify → Control.
- [#3 — Frozen DualSense and duplicate controllers](https://github.com/ReynArts/ApexSenseBridge/issues/3):
  isolates the verified Space Station/GeniTech gamepad bus and its late-created
  DualSense/XInput children, while enforcing one bridge-session owner.
- [#4 — False USBip installation failure](https://github.com/ReynArts/ApexSenseBridge/issues/4):
  corrects the post-install registry lookup that rejected an installation even
  after the prerequisite wrapper returned success.

## Highlights

- Upgrades the pinned USB/IP dependency to the Microsoft-attestation-signed
  OSSign `usbip-win2 0.9.8.0` x64 build from upstream commit `83bd1f78`.
- Adds USBip 0.9.8.0 attach-ABI support to the integrated
  `libVIIPER v0.7.0-asb7` backend and sidecar fallback.
- Adds per-game APEX 5 onboard profile selection (slots 1–4) to Playnite and
  the standalone Tray, with exact profile restoration after each session.
- Preserves verified Flydigi Space Station keyboard/mouse and standard-button
  mappings while isolating its duplicate virtual controllers.
- Hides the verified GeniTech virtual gamepad root and existing or late-created
  DualSense/XInput proxies during a bridge session.
- Prevents the Tray and Playnite extension from terminating or replacing each
  other's active bridge/VIIPER session.
- Fixes false `Control` detection caused by utilities such as Controlify.
- Improves executable learning for elevated and multi-process games: concurrent
  observations no longer cancel each other, repeated sightings do not restart
  the stability window, and validated associations are persisted at shutdown.
- Keeps the standard APEX `View` button mapped directly to an immediate
  DualSense touchpad click, including rapid consecutive presses.
- Retries transient HidHide control-device conflicts and verifies the effective
  HidHide configuration before reporting the bridge ready.
- Fixes the installer bug that reported a successful USBip installation as
  failed because the uninstall-registry product ID was incorrectly escaped.

## Important upgrade instructions

USBip 0.9.7.x is not upgraded in place. Its upstream installer can hang while
running a nested uninstall, and those versions do not contain the complete set
of fixes required by this release.

1. Uninstall the existing USBip package from **Windows Settings > Apps >
   Installed apps**.
2. Restart Windows.
3. Run `ApexSenseBridge-Setup.exe` 0.6.2.
4. Restart again if setup requests it.

Versions 0.6.0 and 0.6.1 could display `usbip-win2 did not install
successfully` even when `usbip-install.log` showed exit code 0. Do not keep
rerunning the older setup or manually delete driver packages with `pnputil`.
The 0.6.2 installer queries the correct registration and validates both
`usbip2_ude` and `usbip2_filter`.

## Flydigi Space Station and rear buttons

M1–M4 must be assigned in Flydigi Space Station to standard controller buttons
or keyboard/mouse inputs. They cannot appear as four independent buttons on the
standard virtual DualSense. ApexSenseBridge authorizes the verified
`SpaceStationService.exe` mapping service through HidHide, so configured rear
button mappings remain available while duplicate Space Station gamepad proxies
are hidden.

## Minecraft and Controlify

The new GeniTech bus isolation and session-ownership protection target reports
where Minecraft Java/Controlify displayed multiple controllers and the virtual
DualSense froze during game startup. For launchers that enumerate controllers
early, enable **Force continuous activation** and wait for **Bridge active**
before starting CurseForge or ATLauncher. Use either the Tray or Playnite as the
automation owner for that game session.

Force Continuous Activation does not identify a game and therefore cannot
learn an executable on its own. Learning requires a supported game to be
automatically detected and remain in a healthy bridge session for 30 seconds.
Detection diagnostics are written to
`%LocalAppData%\ApexSenseBridge\logs\tray_detection.log`.

## Known limitations and requested retests

- M1–M4 are supported through Space Station assignments, not as independent
  buttons in the virtual standard DualSense descriptor.
- The exact Minecraft Java + Controlify launcher transition from issue #3
  still requires confirmation on the reporter's configuration after updating.
- The installer fix was compiled and its registry contract was tested, but a
  clean-system installation is still recommended before closing issue #4.

## Validation

- Native, Windows integration, installer-contract and Tray learning suites were
  exercised during 0.6.2 development.
- Tray and Playnite Release builds completed successfully during validation.
- A physical APEX 5 session sustained approximately 800 Hz verified virtual
  input with zero lost reports and restored the original controller visibility.
- HidHide enforcement was verified with an unwhitelisted process during the
  active session, followed by successful visibility restoration.

See [CHANGELOG.md](CHANGELOG.md) for the complete history and
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) for recovery and diagnostic guidance.
