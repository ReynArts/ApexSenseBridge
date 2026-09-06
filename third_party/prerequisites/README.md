# Offline driver prerequisites

The setup payload is deliberately pinned to these official x64 installers:

- `USBip-0.9.8.0-x64.exe` — OSSign x64 release installer built from upstream
  commit `83bd1f781d57ed6efdf15530c55710cf5d4482bc`, Microsoft-attestation-signed,
  SHA-256 `81F426741F7EE2ED991FEBE24A22DACA8400B6AE2F171054E3FB404897E15D39`.
- `HidHide_1.5.230_x64.exe` — HidHide release `v1.5.230.0`, SHA-256
  `F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6`.

`scripts/build-installer.ps1` and both runtime driver helpers refuse a missing
or hash-mismatched payload. Existing USBip versions other than exact `0.9.8.0`
are not accepted as substitutes for the pinned driver.
