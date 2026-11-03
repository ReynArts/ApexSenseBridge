# ApexSenseBridge Knowledge Base & Troubleshooting Guide

This document covers common questions, diagnostic interpretations, game-specific setups, and troubleshooting steps for ApexSenseBridge.

---

## Table of Contents
1. [Understanding the "Test APEX" Diagnostic Output](#1-understanding-the-test-apex-diagnostic-output)
2. [Game Not Registering Input or Showing Xbox Button Prompts](#2-game-not-registering-input-or-showing-xbox-button-prompts)
3. [Step-by-Step Hardware & Virtual Controller Validation (`joy.cpl`)](#3-step-by-step-hardware--virtual-controller-validation-joycpl)
4. [HidHide Behavior and Controller Hiding](#4-hidhide-behavior-and-controller-hiding)
5. [Driver Installation Issues, USBip Stability & Portable Version](#5-driver-installation-issues-usbip-stability--portable-version)
5.1. [APEX 4 Gyro Setup for DualSense Aiming](#51-apex-4-gyro-setup-for-dualsense-aiming)
6. [Hardware Feedback: Trigger Clicking / Motor Noise at Startup](#6-hardware-feedback-trigger-clicking--motor-noise-at-startup)
7. [Interface & System Tray Quick Reference (FR / EN)](#7-interface--system-tray-quick-reference-fr--en)
8. [What Does "Manual fix" Mean?](#8-what-does-manual-fix-mean)
9. [What to Attach to a Bug Report](#9-what-to-attach-to-a-bug-report)

---

## 1. Understanding the "Test APEX" Diagnostic Output

When clicking **"Test APEX"** (or *"Tester l'APEX"* in the Tray/Control Panel),
the expected backend depends on the connected model. APEX 4 must be connected
over USB or the 2.4 GHz receiver in DInput mode with Flydigi Space Station
closed; its successful backend is `apex4-v1-hid-event`. APEX 5 can produce
diagnostic output similar to this:

```json
{
  "backend": "xinput-fallback",
  "event_driven": false,
  "received_state": true,
  "reports": 8,
  "state_changes": 1,
  "timeouts": 0,
  "parse_failures": 0,
  "lx": 128,
  "ly": 127,
  "rx": 128,
  "ry": 127,
  "l2": 0,
  "r2": 0,
  "dpad": 0,
  "dpad_name": "neutral",
  "seen_dpad": 0,
  "seen_dpad_directions": "neutral",
  "buttons": 0,
  "warning": "APEX HID input unavailable (The APEX HID descriptor does not expose the complete X/Y/Rx/Ry/Z/Rz state required for a lossless DualSense proxy. Available value usages: page=0x1 usage=0x31 page=0x1 usage=0x30 page=0x1 usage=0x34 page=0x1 usage=0x33 page=0x1 usage=0x32 page=0x1 usage=0x39); trying XInput fallback."
}
```

### Is this an error?
> [!NOTE]
> **No, this indicates a 100% successful test of your physical controller!**

- **`"backend": "apex4-v1-hid-event"`:** This is the expected APEX 4 result. The bridge reads its complete 32-byte `04 FE` state stream directly and does not need the compact DInput game interface.
- **`"backend": "xinput-fallback"` & Warning:** This is the expected APEX 5 result on current firmware. Its native direct HID descriptor omits the `Rz` axis control, so ApexSenseBridge automatically and losslessly uses its optimized internal XInput reader.
- **`"received_state": true`, `"timeouts": 0`, `"parse_failures": 0`:** Confirms that the bridge opened the controller communication channel cleanly without dropped data.
- **`"state_changes": 1`:** If you held the controller still during the 2-second test window, `state_changes: 1` is completely normal (initial state capture). If you move the analog sticks or press buttons during the test, this counter will increase.
- **`"dpad"` is a bit mask:** `1` is Up, `2` Down, `4` Left and `8` Right. Values combine for diagonals, so `5` means Up+Left. `dpad_name` spells out the final value, while `seen_dpad` and `seen_dpad_directions` combine every direction observed during the test window.
- **Physical Test Only:** The "Test APEX" button *only* verifies reading from the physical controller. It does **not** create the virtual DualSense or start the active game bridge session.

---

## 2. Game Not Registering Input or Showing Xbox Button Prompts

If you launch a game and it does not detect any controller, or shows Xbox button glyphs (A, B, X, Y) instead of PlayStation glyphs (✕, ○, □, △) and lacks adaptive triggers:

### Issue A: Steam Input is Overriding the DualSense (Most Common)
By default, Steam intercepts DualSense controllers and converts them into standard Xbox 360 (XInput) controllers. This completely disables PlayStation features, adaptive triggers, and audio haptics.

**Solution:**
1. Open your **Steam Library**.
2. Right-click the game ➔ select **Properties...**
3. Navigate to the **Controller** tab.
4. Under *Override for [Game]*, select **Disable Steam Input**.
5. Restart the game.

---

### Issue B: Game Only Scans for Controllers at Startup / Late Controller Detection (Controller Enumeration Timing)
ApexSenseBridge's automatic game detection hooks into games as soon as their executable process is detected in Windows. However, some games and launchers (e.g. *007 First Light*, certain Unreal Engine, Unity, Frostbite, or older DirectX titles) only enumerate connected input devices during their very first startup frames. If the game launches faster than the virtual DualSense USB device finishes its Windows plug-and-play attachment, the game may miss the controller entirely or fallback to keyboard/mouse.

**Symptoms:**
- The game launches but ignores all controller inputs, or only responds to keyboard/mouse.
- In-game prompts remain keyboard keys even when the bridge shows as active in the Tray.
- Plugging in a physical controller or restarting the game while the bridge is already running makes it work.

**Recommended Solution (Force Continuous Activation before Launch):**
1. Exit the game completely.
2. In the **ApexSenseBridge Tray** (right-click the taskbar tray icon), enable **Force continuous activation** (or *Activation continue forcée*).
3. Wait 1–2 seconds until the tray status/notification confirms **"Bridge active"**.
4. *(Optional verification)* Press <kbd>Win</kbd> + <kbd>R</kbd>, run `joy.cpl`, and confirm **Wireless Controller** is visible.
5. Launch your game (ensuring Steam Input is disabled).
6. The game will now detect the virtual DualSense already fully plugged in from frame 1!

---

### Issue C: Known Game-Side Bugs
Some games have known bugs with controller detection on PC.
- *Example:* *007 First Light* has a documented upstream issue where controller input is not recognized on the title/start screen (see [IO Interactive 007 Known Issues](https://007firstlight-support.zendesk.com/hc/en-us/articles/36203682496413-007-First-Light-Known-Issues)).

### Issue D: A game executable is not being learned

Executable learning starts only after the Tray automatically identifies a
supported game and the bridge remains healthy for 30 seconds. **Force continuous
activation** cannot create a learned association by itself because that mode
does not identify a specific game.

Version 0.6.2 tracks concurrent child/render processes independently, does not
restart the stability timer for repeated sightings of the same process, and can
resolve most elevated game paths using limited process-query rights. Keep the
game open beyond the validation window, then check **Learned executables** in
the Tray dashboard. Detection diagnostics are stored in:

```text
%LocalAppData%\ApexSenseBridge\logs\tray_detection.log
```

The log and exported learned-executable list omit local absolute paths when
shared through the dashboard export workflow.

---

### Issue E: Steam Shows Both the Physical Controller and the Virtual DualSense

**Symptoms:** a Steam game registers D-pad or button presses twice, Windows keeps announcing a new controller, or Steam lists both the Flydigi pad and a DualSense. Restarting Steam makes it go away.

**Cause:** Steam was already running when the bridge started and had opened the physical APEX before HidHide began hiding it. HidHide blocks *new* connections to the controller; it cannot take back one that Steam already holds, so Steam keeps seeing both pads until it is restarted. ApexSenseBridge never restarts Steam by itself, because that could interrupt a running game or download. Since 1.0.1 the Tray shows one notification per Steam session that points to the steps below.

**Start the bridge first, then restart Steam:**
1. Exit Steam completely (**Steam** ➔ **Exit**) and make sure its icon is gone from the notification area.
2. In the Tray, enable **Force continuous activation** and wait for **Bridge ready** / **Bridge active**.
3. Start Steam again and keep **Steam Input disabled** for the game (see Issue A).
4. Launch the game. Turn **Force continuous activation** off when you are done.

If the duplicate input still appears after this exact order, attach the automatic report and a screenshot of `joy.cpl` taken while the session is active (see [section 9](#9-what-to-attach-to-a-bug-report)). One user also reported the duplicate returning every time the controller disconnected and reconnected while Steam was running; the cause there has not been confirmed.

---

### Issue F: The Bridge Turns On and Off Repeatedly While the Game Starts

**Symptoms:** while a game is starting, the Tray status flips several times between active and idle, the virtual DualSense connects and disconnects, and in-game prompts or button labels flicker between DualSense and Xbox styles. One user saw it calm down once the game had fully loaded and had focus, and also saw the physical and virtual controllers behave as if both were active (LT not working, laggy camera).

**What is known:** the loop was reported with Automatic detection on some Unreal Engine titles. The exact cause has not been confirmed; the workaround below avoids relying on Automatic detection.

**Workaround one user reported as effective (not guaranteed):**
1. In the Tray Dashboard, select the game. Under **Game executable (optional)**, choose the game's actual executable instead of **Automatic detection**: pick it in the list if it appears there (*learned*), or use **Choose an .exe file…**.
2. Unreal Engine games typically start from a small `<Game>.exe` in the install folder, while the real game process is `<Game>-Win64-Shipping.exe` in a `Binaries\Win64` subfolder. To find the real process, start the game, open Task Manager ➔ **Details**, right-click the game's process ➔ **Open file location**.
3. Restart the game.

Enabling **Force continuous activation** before launching the game (Issue B) keeps the virtual DualSense plugged in during startup and is another way to avoid the flip-flopping.

**If it still loops, attach:** `tray_bridge.log` and `tray_detection.log` covering one launch, the names of the game's processes in Task Manager ➔ **Details** during startup, a screenshot of the Tray status while it flips, and the automatic report (see [section 9](#9-what-to-attach-to-a-bug-report)).

---

## 3. Step-by-Step Hardware & Virtual Controller Validation (`joy.cpl`)

If a game is not responding, follow this quick diagnostic flow to determine whether the issue is with ApexSenseBridge/Windows or isolated to the game itself:

1. **Start the Virtual Controller:**
   - In ApexSenseBridge Tray, enable **Force continuous activation**.
   - Ensure the tray says **Bridge active**.
2. **Open the Windows Game Controller Control Panel:**
   - Press <kbd>Win</kbd> + <kbd>R</kbd>, type **`joy.cpl`**, and press <kbd>Enter</kbd>.
3. **Verify the Device List:**
   - You should see **Wireless Controller** (the virtual DualSense device).
   - *(Note: Your physical Flydigi controller should be hidden by HidHide while the bridge is active).*
4. **Test Inputs:**
   - Click on **Wireless Controller** ➔ **Properties**.
   - Move your APEX sticks and press buttons.
   - **Result Analysis:**
     - **If inputs move in `joy.cpl`:** ApexSenseBridge, the drivers, and Windows are operating perfectly. The issue is inside the game (e.g., Steam Input enabled, in-game controller settings disabled, or game launch timing).
     - **If inputs do not move in `joy.cpl`:** Verify that no other software (like Flydigi Space Station in exclusive mode) is locking the physical device.

---

## 4. HidHide Behavior and Controller Hiding

### Why is "Enable device hiding" unchecked in the HidHide Configuration Client?
> [!NOTE]
> **This is normal and intentional!**

- When no bridge session is active, ApexSenseBridge leaves "Enable device hiding" unchecked so you can use your APEX as a standard Xbox/XInput controller in Windows or non-DualSense games.
- When an active bridge session starts, ApexSenseBridge **automatically enables HidHide** to hide the physical controller from the game (preventing double-input) and maps it exclusively through the virtual DualSense.
- If Flydigi Space Station is installed, ApexSenseBridge keeps its installed
  `SpaceStationService.exe` mapping service authorized through HidHide. Rear-button
  keyboard/mouse shortcuts configured in Space Station therefore remain available
  while the physical gamepad and Space Station's duplicate DualSense/XInput
  gamepad proxies stay hidden from the game.
- Run either the standalone Tray automation or the Playnite automation for a
  game. ApexSenseBridge prevents two simultaneous engine sessions, but using a
  single owner avoids duplicate notifications and ambiguous lifecycle events.
- When the game exits, ApexSenseBridge automatically restores the previous HidHide state.

### Why M1–M4 do not appear as separate buttons in a game

The virtual controller is a standard DualSense, whose input report has no four
independent Flydigi rear-button fields. HidHide whitelisting lets Space Station
continue reading the physical APEX, but it does not add new controls to the
virtual DualSense descriptor.

Assign M1–M4 in Flydigi Space Station to standard controller buttons or
keyboard/mouse inputs. Those configured mappings remain usable in game while
ApexSenseBridge is active. Leaving them as unassigned independent "extra
buttons" will not make them appear in a game's binding menu.

### Rear Buttons (M1–M4) Do Nothing in an Emulator or PC Port

**What the bridge forwards** (checked in the source):
- Sticks, triggers, D-pad, face and shoulder buttons, stick clicks, View and Menu come from the controller's *mapped* gamepad state. A rear button that Space Station assigns to one of these standard buttons therefore reaches the virtual DualSense as that button.
- The bridge decodes no rear-button field from the Flydigi vendor stream it also reads (motion, battery, PS button), so M1–M4 never appear as separate buttons.
- A rear button assigned to a keyboard key, mouse button or macro is not handled by the bridge. Flydigi's `SpaceStationService.exe` produces those inputs itself and stays authorized in HidHide so it can keep reading the controller.

**Decision tree:**
1. Open Space Station and check what the rear button does.
   - *Nothing, or its default:* there is nothing to forward. Assign it a gamepad button (step 2) or a key (step 3).
   - *A gamepad button:* go to step 2.
   - *A keyboard key, mouse button or macro:* go to step 3.
2. **Gamepad-button mapping.**
   1. Enable **Force continuous activation**, wait for **Bridge active**, open `joy.cpl` ➔ **Wireless Controller** ➔ **Properties** and press the rear button.
   2. If a button lights up, the bridge forwards it. If the game or emulator still ignores it, open its own binding screen and make sure it reads the controller as a DualSense (not through Steam Input or an XInput layer) and supports that button.
   3. If nothing lights up, the rear button is not producing a standard gamepad button. Check that the assignment is saved in the profile that is active on the controller, then test again. Then close Space Station and test once more: the source treats the controller's own mapped state as authoritative, so the result should not change, but this has not been verified for every mapping. Tell us if it does change.
3. **Keyboard or mouse mapping.**
   1. Space Station must be running (`SpaceStationService.exe` in Task Manager), because it generates the keystrokes. Test in Notepad: the key should appear when you press the rear button.
   2. The game must accept keyboard or mouse input while a controller is in use. Many emulators and PC ports bind the keyboard separately, switch every prompt to the keyboard after a key press, or ignore the keyboard while a gamepad is the active device. A game that reads only the DualSense never sees these keys: use a gamepad-button mapping (step 2) instead.
   3. Bind the key in the game's own keyboard bindings.
4. Space Station's duplicate DualSense and XInput gamepads are hidden from games on purpose, so they are not a workaround.

**When reporting a rear-button problem, tell us:**
- The game, emulator or port and its version, and how it reads controllers (SDL, DirectInput, XInput, Steam Input).
- What each rear button is assigned to in Space Station (a screenshot of the mapping page) and which onboard profile is active.
- Whether the same assignment works in that game with the bridge off (physical pad visible).
- What `joy.cpl` shows with the bridge active (step 2.1) and whether Space Station was running.
- Cable or receiver, plus `tray_bridge.log` and the automatic report ([section 9](#9-what-to-attach-to-a-bug-report)).

### Minecraft/Controlify shows duplicate or frozen controllers

Version 0.6.2 isolates Space Station's verified GeniTech gamepad root together
with its DualSense and XInput children, including proxies created after the
bridge starts. It also prevents the Tray and Playnite extension from replacing
each other's running engine session.

1. Restart Windows after installing USBip 0.9.8.0.
2. Use either the standalone Tray or Playnite as the automation owner, not both.
3. Enable **Force continuous activation** before starting CurseForge or
   ATLauncher, then wait for **Bridge active**.
4. In `joy.cpl`, verify that **Wireless Controller** responds before Minecraft
   starts. Controlify should receive only the ApexSenseBridge virtual DualSense.
5. If input still freezes, preserve the ApexSenseBridge log and record the
   controller list shown by Controlify before and after Minecraft loads.

### `Opening the HidHide control device` / Windows error 5

ApexSenseBridge retries short-lived access conflicts automatically. If access is
still denied, close the HidHide Configuration Client, restart Windows, and try
again. If the same error returns immediately after the restart, repair or
reinstall HidHide 1.5.230 before changing its device list manually.

### Restoring Controller Visibility After an Unexpected Crash
If the PC lost power or a game crashed unexpectedly while the controller was hidden:
- Click **"Restore controller visibility"** (*Restaurer la visibilité*) in the Tray app.
- Or run in Command Prompt / PowerShell:
  ```cmd
  ApexSenseBridge.exe restore-controller-visibility
  ```

---

## 5. Driver Installation Issues, USBip Stability & Portable Version

### Keyboard or mouse input after uninstalling ApexSenseBridge / USBip

USBip installs a device-specific USB filter driver. Removing that package can
temporarily interrupt USB devices and an incomplete upstream uninstall can
leave the Windows USB stack in an inconsistent state even though the keyboard
and mouse still work in firmware/BIOS.

- ApexSenseBridge never removes USBip, including during silent uninstall. If it
  must be removed, do so separately through Windows Settings with a recovery
  input path available, then restart Windows.
- HidHide is preserved by default. Interactive removal is a separate,
  default-No choice offered only when its fresh-install provenance, exact
  version, service and product registration are verified.
- Settings, logs, learned associations and Playnite profiles are preserved by
  default. `/REMOVEUSERDATA` is the only silent deletion opt-in and never
  authorizes driver removal.
- If HidHide removal was explicitly selected, restart Windows before judging
  device state. Do not manually delete OEM driver packages with `pnputil`.
- If keyboard/mouse input works in BIOS but not Windows, use Windows Recovery
  to enter Safe Mode or System Restore and repair/remove USBip from Windows
  Settings. Keep the ApexSenseBridge and USBip logs for diagnosis.

### Installer reports that USBip failed although `usbip-install.log` says success

Versions 0.6.0 and 0.6.1 could query an incorrectly escaped uninstall-registry
key after the bundled USBip installer returned success. This produced a false
failure and could be reported as unregistered driver remnants on the next run.
The registry verification is corrected in 0.6.2 and later.

- Do not repeatedly run the older setup or remove driver packages manually.
- If USBip 0.9.7.x is installed, uninstall it from Windows Settings and restart.
- Then run the current installer, which validates the registered 0.9.8.0 package
  and both `usbip2_ude`/`usbip2_filter` services against the correct key.

### Known Upstream `usbip-win2` Kernel Issue (Not Caused by ApexSenseBridge)
ApexSenseBridge is an entirely user-space application that communicates with the system via standard Windows APIs. To emulate a virtual DualSense controller over USB, it relies on the third-party open-source driver **`usbip-win2`**.

> [!WARNING]
> **Important clarification regarding USBip instability:**
> - Any blue screen crash (e.g. `DPC_WATCHDOG_VIOLATION (0x133)`) or kernel fault that occurs during heavy USB/IP I/O takes place strictly inside the third-party `usbip2_ude.sys` kernel driver.
> - **ApexSenseBridge does not run in kernel mode and cannot cause or prevent kernel-level memory corruption in external drivers.** This is a documented upstream issue in `usbip-win2` (see [vadimgrn/usbip-win2 issue #172](https://github.com/vadimgrn/usbip-win2/issues/172)).
> - **Why version `0.9.8.0` is used:** `0.9.7.7` can deadlock in `usbip2_ude!send`; `0.9.7.8` removes that spinlock but has a second critical memory-corruption bug. ApexSenseBridge pins the Microsoft-attestation-signed OSSign `0.9.8.0` build from commit `83bd1f78`, which contains both upstream fixes, including `4139f44`.
> - Attestation signing permits normal installation with Secure Boot but does not certify runtime stability. Keep new minidumps if the 0.9.8.0 build produces any kernel crash.
> - If you ever encounter an unexpected Windows crash, restart your PC, check `C:\Windows\Minidump`, and ensure no conflicting virtual USB tools (e.g. USB forwarding tools, old virtual bus drivers) are interfering.

---

### Installer stuck on "Uninstalling USBip..."
Upstream USBip installers can hang if an older incompatible driver version is already installed.
- **Solution:** Use the **Portable ZIP** package (`ApexSenseBridge-Portable.zip`).
- Extract the ZIP to your desired location.
- Right-click `Install-Drivers.cmd` and select **Run as Administrator**.
- The script checks and installs:
  - **`usbip-win2 0.9.8.0`**, Microsoft-attestation-signed by OSSign from upstream commit `83bd1f78`.
  - **`HidHide 1.5.230`**.
- **Restart Windows** after driver installation.
- If USBip 0.9.7.x is already installed, uninstall it from Windows Settings and restart before running the helper. Automatic in-place upgrades remain disabled because the upstream nested uninstaller can hang.
- Launch `Start-ApexSenseBridge.cmd` or `ApexSenseBridgeTray.exe`.

---

### USB or Driver Trouble After Installing the Drivers

ApexSenseBridge relies on two third-party kernel components: USBip (the `usbip2_ude` driver and the `usbip2_filter` USB filter driver) for the virtual DualSense, and HidHide (`HidHide`) to hide the physical controller. These are the service names the installer and the portable driver helper verify.

- **See what is registered.** The *Installation* block of the automatic report ([section 9](#9-what-to-attach-to-a-bug-report)) lists the registered USBip and HidHide versions and the three service keys, normally `registered (Start=3)`. In PowerShell, without administrator rights:
  ```powershell
  Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude, HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_filter, HKLM:\SYSTEM\CurrentControlSet\Services\HidHide | Select-Object PSChildName, Start
  ```
  `Start` 3 is Windows' "start on demand" value. A missing key is reported as an error.
- **Read the driver log.** The installer writes `%ProgramData%\ApexSenseBridge\usbip-install.log`; the portable helper writes `driver-install.log` next to `Install-Drivers.cmd`. A line such as *The operation was canceled by the user* for one component means Windows reported the launch of that component's installer as cancelled, typically a declined elevation prompt or security software blocking it. Allow it and run the installation again.
- **"The `-File` path does not exist."** If PowerShell says the path of `Install-Drivers.ps1` does not exist, the administrator session could not see the folder. One possible cause is that Windows keeps drive mappings per logon session, so mapped network drives are typically not visible to elevated programs. Extract the ZIP to a local folder (for example `C:\ApexSenseBridge-Portable`) and run `Install-Drivers.cmd` as administrator again.
- **Restart Windows** after installing or removing a driver before judging device state.
- **If USB devices stop responding:** keep a second input path (another keyboard or mouse on a different port, or touch) before changing anything, and do not disable USB controllers or delete driver packages by hand. Uninstall USBip, and HidHide if needed, in Windows Settings ➔ Apps ➔ Installed apps, then restart. ApexSenseBridge itself never removes either driver. If no input works in Windows at all, use Windows Recovery: **Troubleshoot** ➔ **Advanced options** ➔ **Startup Settings** ➔ **Restart**, then press <kbd>4</kbd> or <kbd>F4</kbd> for Safe Mode (the path documented by Microsoft), or use System Restore to a point before the driver installation. Whether USBip can be uninstalled from Safe Mode has not been verified. Keep the logs above for diagnosis.

---

### FACEIT Anti-Cheat Refuses to Start Because of the USBip Driver

**Symptom:** FACEIT Anti-Cheat (used for FACEIT matches in Counter-Strike 2) does not start and its message names the USBip driver and asks you to uninstall it.

**What is known:**
- A user reported exactly this message after installing ApexSenseBridge's drivers. FACEIT decides which drivers its anti-cheat blocks; the criteria are not documented in this repository, and FACEIT's own article could not be consulted when this entry was written, so treat the details below as unverified.
- ApexSenseBridge needs USBip to create the virtual DualSense. It cannot bypass, hide or work around an anti-cheat check, and it does not inject into or modify any game.
- Not verified: whether FACEIT objects to the driver being installed or only loaded, and whether a later FACEIT or USBip release changes the outcome. Excluding the game in ApexSenseBridge, or leaving the bridge idle, does not remove the driver, so it is not expected to help.

**Options (your choice):**
1. Uninstall USBip (and HidHide if you wish) in Windows Settings ➔ Apps ➔ Installed apps, restart Windows and play FACEIT. Run the ApexSenseBridge setup, or `Install-Drivers.cmd` for the portable package, again when you want to use the bridge. Follow the precautions in the previous entry before removing USBip.
2. Ask FACEIT support whether this driver can be allowed. ApexSenseBridge has no influence on that decision.

---

## 5.1. APEX 4 Gyro Setup for DualSense Aiming

### APEX 4 gyro aiming does nothing in games

**Symptom:** gyro aiming is enabled in the game, but the camera does not follow
APEX 4 motion. The bridge log may show `APEX 4 motion sensors have reported no
data yet`.

**Cause:** the APEX 4 only includes motion data in its reports while the gyro
of the active Flydigi profile is enabled and active. With the gyro off, every
motion byte is zero, so the virtual DualSense has no gyro data to forward.

**Fix:** in Flydigi Space Station, open the active profile's **Gyro** tab and
enable the gyro (for example *Map to: Right Joystick*), save it, then start a
new bridge session. The game's own gyro settings (sensitivity, gyro aiming
on/off) then apply as on a DualSense.

**Note:** *Way to turn on* decides when motion data is sent. With *Press and
hold to power on* (for example on LT), gyro aiming only works while that button
is held, which suits aim-down-sights gyro. If no gyro button is held when a
session starts, the warning above is expected and can be ignored.

---

## 6. Hardware Feedback: Trigger Clicking / Motor Noise at Startup

### Why do the triggers click or make a motor noise when opening the app?
> [!TIP]
> **This is a positive indicator that hardware communication is working!**

When `ApexSenseBridgeTray.exe` or the bridge engine initializes, it sends a handshake via the Flydigi FORCEADAPT protocol over the 2.4GHz dongle / USB connection to calibrate and set up the motorized trigger gears. Hearing the trigger motors click or reset confirms that ApexSenseBridge is communicating directly with your APEX hardware.

---

## 7. Interface & System Tray Quick Reference (FR / EN)

| Menu Option (French) | English Equivalent | Description |
|---|---|---|
| **Ouvrir l'interface...** | Open Dashboard... | Opens the main window showing compatible games and status. |
| **Activation continue forcée** | Force Continuous Activation | Keeps the virtual DualSense active at all times (useful for games requiring pre-launch controller detection). |
| **Tester l'APEX** | Test APEX Controller | Runs a 2-second physical input test on the Flydigi controller. |
| **Restaurer la visibilité** | Restore Controller Visibility | Unhides the physical controller in HidHide if an abnormal shutdown occurred. |
| **Quitter** | Exit | Safely closes the bridge, detaches the virtual DualSense, and unhides the physical controller. |

---

## 8. What Does "Manual fix" Mean?

Some games in the Tray game list carry an amber **Manual fix** badge (*Correctif manuel* in French). When the fix is allowed, the badge turns into a green **Manual fix applied**.

### What a manual fix is

- The game catalogue is generated from PCGamingWiki's list of games with DualSense support. For some titles PCGamingWiki classes the feature as "hackable": the game does not provide it by itself, but a third-party modification, tool or setting can add it. The badge is set per feature (adaptive triggers, haptic feedback), and the Tray notification shows the address of the game's PCGamingWiki page, which is where to look for the workaround (the page content is not checked by ApexSenseBridge).
- **You apply it.** ApexSenseBridge does not download, install, enable or check any manual fix, and it does not bundle one.
- **Why the bridge stays off by default.** Activating the bridge hides your physical controller with HidHide and offers a virtual DualSense instead. A game that cannot use a DualSense and has no working fix would then see no usable controller. For a manual-fix game whose requested features all need the fix, the Tray therefore does not activate the bridge, leaves the physical controller available and shows *Manual setup required*.

### What "Allow automatic activation" does

- It only records that you consider the fix applied for this game. From the next launch, the bridge starts automatically for it like for any other game, which includes hiding the physical controller.
- It does **not** install or verify the fix, and it does **not** make the game accept the virtual DualSense.
- It is disabled while the game is excluded; exclusion always wins.
- **Leave it off** if you have not applied the fix yourself, if you are not sure the fix works together with a virtual DualSense, or if the game already works with your physical controller. If the game shows no controller or cannot be controlled after you enabled it, turn it off again and restart the game.

### Example: Forza Horizon and Forza Motorsport 7

In the catalogue, Forza Horizon 4, 5 and 6 and Forza Motorsport 7 are marked as manual fix for adaptive triggers only (no haptic feedback), each with its PCGamingWiki page as the reference. The community utility [Forza-Horizon-DualSense-Python](https://github.com/HamzaYslmn/Forza-Horizon-DualSense-Python) is an external fix that users mention for these games: according to its own README, it adds adaptive-trigger feedback from the game's telemetry and drives a real DualSense. ApexSenseBridge does not bundle it, has not validated it together with its virtual DualSense and does not replace it.

One user reported being unable to move the car after enabling **Allow automatic activation** for a Forza Horizon game, and getting normal control back by re-enabling Steam Input and using that utility. The likely explanation (a hypothesis, not verified) is that the game did not use the virtual DualSense on its own and, with Steam Input disabled as ApexSenseBridge normally requires, no layer translated it. If you see this, turn the switch off.

---

## 9. What to Attach to a Bug Report

Open the Tray ➔ **Settings** ➔ **Controller Diagnostics & Test** ➔ **Report a bug**. The window prepares an anonymized installation report that you can preview, and GitHub lets you review the issue before publishing. Then check this list; each item removes a round of questions:

- [ ] **The automatic installation report** (system, ApexSenseBridge version, driver registration, bridge state, recent log lines).
- [ ] **The full `tray_bridge.log` and `tray_detection.log`** from `%LocalAppData%\ApexSenseBridge\logs\`. The report only contains the latest lines. Add `tray_crash.log` or `tray_startup_error.log` if the Tray crashed or did not open. For a bridge that does not become ready, note the last `Startup phase:` line of `tray_bridge.log`.
- [ ] **Cable or receiver:** USB cable, or the 2.4 GHz receiver; the controller model and, for APEX 4, DInput mode.
- [ ] **Flydigi Space Station:** running or fully closed (check Task Manager for `SpaceStationService.exe`), and the mapping involved if rear buttons are concerned.
- [ ] **The game:** title, executable name, store or launcher (Steam, Epic, Game Pass, Playnite, the Tray's **Launch a game (.exe)**), and whether Steam Input is disabled for it.
- [ ] **What you did and what you saw:** the steps, a screenshot of the Tray status or error, and whether **Wireless Controller** in `joy.cpl` responds while the bridge is active.

Attach files on the GitHub issue page itself: replying to a GitHub notification e-mail drops attachments. Read the logs before sharing them; they can contain game names and install paths.
