ApexSenseBridge portable package
================================

The application files are portable: extract the whole folder anywhere and run
Start-ApexSenseBridge.cmd (or ApexSenseBridgeTray.exe directly).

Windows kernel drivers cannot be made portable. Before the first use:

1. Run Install-Drivers.cmd and accept its single administrator prompt.
2. Restart Windows.
3. Run Start-ApexSenseBridge.cmd.

The helper installs the pinned usbip-win2 0.9.8.0 and HidHide 1.5.230 drivers
only when they are absent. An intact USBip 0.9.8.0 installation is preserved.
If an older, unsupported USBip version or a damaged installation is
found, it stops with a clear message instead of entering USBip's known nested-
uninstaller hang. Uninstall the old USBip package in Windows Settings, restart,
then run this helper again. Details are written to driver-install.log.

Keep every EXE and DLL, plus the Resources, Data, Drivers, and Licenses folders
together. libVIIPER.dll is the default backend; viiper.exe is its automatic
compatibility fallback.
The portable build is intended for the standalone tray application. Use the
regular ApexSenseBridge-Setup.exe when you want automatic Playnite integration,
Start-menu shortcuts, startup registration, or normal Windows uninstallation.

When Flydigi Space Station is installed, ApexSenseBridge keeps its verified
mapping service authorized while hiding its duplicate virtual gamepad proxies.
M1-M4 must be assigned in Space Station to standard controller buttons or
keyboard/mouse inputs; they are not independent buttons on a standard virtual
DualSense. Do not use the portable Tray automation and Playnite automation for
the same game session.

To remove the prerequisites later, uninstall USBip and HidHide from Windows
Settings > Apps > Installed apps.
