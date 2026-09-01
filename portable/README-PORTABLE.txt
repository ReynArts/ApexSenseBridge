ApexSenseBridge portable package
================================

The application files are portable: extract the whole folder anywhere and run
Start-ApexSenseBridge.cmd (or ApexSenseBridgeTray.exe directly).

Windows kernel drivers cannot be made portable. Before the first use:

1. Run Install-Drivers.cmd and accept its single administrator prompt.
2. Restart Windows.
3. Run Start-ApexSenseBridge.cmd.

The helper installs the pinned usbip-win2 0.9.7.7 and HidHide 1.5.230 drivers
only when they are absent. Healthy compatible USBip 0.9.7.5-0.9.7.7 drivers
are preserved. If an unsupported USBip version or a damaged installation is
found, it stops with a clear message instead of entering USBip's known nested-
uninstaller hang. Details are written to driver-install.log.

Keep every EXE and the Resources, Data, Drivers, and Licenses folders together.
The portable build is intended for the standalone tray application. Use the
regular ApexSenseBridge-Setup.exe when you want automatic Playnite integration,
Start-menu shortcuts, startup registration, or normal Windows uninstallation.

To remove the prerequisites later, uninstall USBip and HidHide from Windows
Settings > Apps > Installed apps.
