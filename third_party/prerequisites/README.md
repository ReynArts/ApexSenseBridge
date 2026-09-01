# Offline driver prerequisites

The setup payload is deliberately pinned to these official x64 installers:

- `USBip-0.9.7.7-x64.exe` — usbip-win2 release `v.0.9.7.7`, SHA-256
  `51620FA5F9F8BE5932BC9D786DEEE557CE06D5407A99CAB490DCFAC71F185FEA`.
- `HidHide_1.5.230_x64.exe` — HidHide release `v1.5.230.0`, SHA-256
  `F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6`.

`scripts/build-installer.ps1` refuses a missing or hash-mismatched payload. The
runtime setup also refuses an installed usbip-win2 `0.9.7.8`; that version is
not accepted as a substitute for the pinned driver.
