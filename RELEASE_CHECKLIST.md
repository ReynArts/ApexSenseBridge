# ApexSenseBridge Release Checklist

Use this checklist for every public installer release. Driver lifecycle tests
must run in disposable Windows virtual machines with a snapshot and a recovery
input path; do not test USB filter removal on a development workstation.

## Automated release gates

- Tag is exactly `v<major>.<minor>.<patch>` and matches every source manifest:
  `powershell -File .\scripts\verify-version-consistency.ps1 -ExpectedVersion vX.Y.Z`.
- Release build completes with signing required:
  `powershell -File .\scripts\build-installer.ps1 -RequireSigning`.
- All native and installer contract tests pass:
  `ctest --test-dir .\build-win -C Release --output-on-failure`.
- Final artifacts, SHA-256 file and Authenticode publisher agree:
  `powershell -File .\scripts\verify-version-consistency.ps1 -CheckArtifacts -RequireSignatures`.
- `dist` contains exactly one versioned Playnite package, the setup, portable
  ZIP and checksum file—no loose executables or expanded portable directory.
- The only local runnable Tray build is
  `build-win\Release\ApexSenseBridgeTray.exe`; no copy remains in the project
  `bin\Release` folder or as an executable under `obj\Release`.

## Clean-VM install and uninstall matrix

- No prerequisites installed: setup installs pinned USBip and HidHide, records
  verified HidHide provenance, and requests a restart.
- Exact prerequisites already installed: setup reuses them and never claims
  ownership; uninstall leaves both untouched.
- Upgrade from the previous ApexSenseBridge release: settings and Playnite
  profiles survive, and legacy ownership state cannot authorize driver removal.
- Normal interactive uninstall, choose **No** for both prompts: application and
  extension code are removed; USBip, HidHide and user data remain.
- Silent uninstall: USBip, HidHide and user data remain.
- Silent uninstall with `/REMOVEUSERDATA`: only user data is additionally
  removed; both drivers remain.
- Interactive uninstall with verified HidHide provenance, choose **Yes** only
  for HidHide: USBip remains, HidHide is removed, and Windows requests restart.
- Existing mismatched, incomplete or orphaned USBip/HidHide state: setup stops
  with recovery guidance and does not attempt an in-place driver replacement.
- After every uninstall scenario and restart, verify keyboard and mouse input in
  Windows as well as controller enumeration in `joy.cpl`.

## Update-path checks

- Tray, Playnite and PowerShell updaters accept only
  `ApexSenseBridge-Setup.exe` from the official repository release path.
- Unsigned, untrusted, wrong-version, wrong-product and different-publisher
  installers are rejected before elevation or execution.
- A valid signed installer for the new version launches successfully from each
  supported updater.

Record VM image, Windows build, existing prerequisite versions, choices made,
restart result and logs for every matrix row before publishing the tag.
