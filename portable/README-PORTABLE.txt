ApexSenseBridge portable package
================================

The application files are portable: extract the whole folder anywhere and run
Start-ApexSenseBridge.cmd (or ApexSenseBridgeTray.exe directly).
This is the main application for game detection, settings and diagnostics.
ApexSenseBridge.exe is the engine and command-line diagnostic tool. Opening it
without a command forwards to the main Tray application.

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

While usbip-win2 installs, Windows restarts every USB 3.0 hub: all connected USB
devices (keyboard, mouse, controller, drives) stop working for a short time and
then resume. Close file transfers and calls first, do not unplug or power off
during that step, and consider creating a Windows restore point beforehand, as
the usbip-win2 project recommends.

The helper only starts those two vendor installers. It never removes, repairs
or disables drivers, devices, services or USB controllers, and it never
terminates an installer that is still running. It copies the two installers to a
temporary folder, checks their SHA-256 again, runs them from there (so the
package can sit on a removable, network or mapped drive that an administrator
session cannot read) and copies the log back to driver-install.log.

The helper can be run again at any time. Components that are already installed
are skipped, so if HidHide could not be installed (for example because antivirus
or Windows security blocked its installer, Windows error 1223) the next run
continues with HidHide only. In that case allow Drivers\HidHide_1.5.230_x64.exe
in your security product, or right-click it and choose Run as administrator,
then run Install-Drivers.cmd once more to verify it. If the helper reports that
the -File argument does not exist, copy the whole folder to a local drive such
as C:\ApexSenseBridge-Portable and run it from there.

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

DISCONNECTION RECOVERY
After a confirmed runtime disconnection, open the Tray home page, reconnect or
wake the controller, and choose Resume bridge. Recovery keeps the game, profile
and APEX slot and rechecks the controller, virtual DualSense and HidHide isolation.
The game is never closed or relaunched automatically. Restart it if it cannot
reacquire the recreated virtual controller. Playnite-owned sessions must be
restarted from Playnite; Tray does not take over.
