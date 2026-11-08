# ApexSenseBridge Release Checklist

Use this checklist for every public installer release. Driver lifecycle tests
must run in disposable Windows virtual machines with a snapshot and a recovery
input path; do not test USB filter removal on a development workstation.

## Self-signed release (1.1.0 only)

- Run the "Release ApexSenseBridge" workflow manually from `dev` with the tag
  (for example `v1.1.0`). Manual runs always use the self-signed
  `test-signing` policy. Run it once with "publish" unchecked, then again with
  "publish" checked to create the release.
- The workflow creates the tag itself; a tag created that way does not start the
  tag-triggered run. Never push a `v*` tag by hand before the SignPath production
  certificate is imported: that run uses `release-signing` and fails without it.
- An installed version whose signature is missing or not trusted (the 1.1.0
  self-signed build) accepts an update when every file in it is trusted by
  Windows and all files share one signer; the download source and product name
  checks still apply. Once the installed build is trusted, only the same
  publisher is accepted.

## Automated release gates

- The SignPath account is created, the OSS organization invitation is accepted,
  and only then is the CI user email confirmation completed.
- The SignPath GitHub App has access to this repository and the SignPath project
  is linked to the predefined GitHub.com trusted build system.
- SignPath contains the three artifact configurations from
  `signpath/payload-artifact-configuration.xml` and
  `signpath/uninstaller-artifact-configuration.xml` and
  `signpath/installer-artifact-configuration.xml`. Record their slugs in the
  repository variables listed below.
- GitHub Actions secret `SIGNPATH_API_TOKEN` contains the CI user's SignPath API
  token. Repository variables `SIGNPATH_ORGANIZATION_ID`,
  `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_TEST_SIGNING_POLICY_SLUG`,
  `SIGNPATH_RELEASE_SIGNING_POLICY_SLUG`,
  `SIGNPATH_PAYLOAD_ARTIFACT_CONFIGURATION_SLUG` and
  `SIGNPATH_UNINSTALLER_ARTIFACT_CONFIGURATION_SLUG` and
  `SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION_SLUG` match the SignPath project.
- A manual `workflow_dispatch` run succeeds with the self-signed test policy.
  SignPath must approve the project and provision the production certificate
  before `SIGNPATH_RELEASE_SIGNING_POLICY_SLUG` is used by a release tag.
- Tag is exactly `v<major>.<minor>.<patch>` and matches every source manifest:
  `powershell -File .\scripts\verify-version-consistency.ps1 -ExpectedVersion vX.Y.Z`.
- The workflow signs the six Windows payload files, Inno Setup's generated
  uninstaller, and then the final installer through SignPath. No release is
  published if any signing request fails.
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
