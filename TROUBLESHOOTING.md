# ApexSenseBridge Knowledge Base & Troubleshooting Guide

This document covers common questions, diagnostic interpretations, game-specific setups, and troubleshooting steps for ApexSenseBridge.

---

## Table of Contents
1. [Understanding the "Test APEX" Diagnostic Output](#1-understanding-the-test-apex-diagnostic-output)
2. [Game Not Registering Input or Showing Xbox Button Prompts](#2-game-not-registering-input-or-showing-xbox-button-prompts)
3. [Step-by-Step Hardware & Virtual Controller Validation (`joy.cpl`)](#3-step-by-step-hardware--virtual-controller-validation-joycpl)
4. [HidHide Behavior and Controller Hiding](#4-hidhide-behavior-and-controller-hiding)
5. [Driver Installation Issues & Portable Version](#5-driver-installation-issues--portable-version)
6. [Hardware Feedback: Trigger Clicking / Motor Noise at Startup](#6-hardware-feedback-trigger-clicking--motor-noise-at-startup)
7. [Interface & System Tray Quick Reference (FR / EN)](#7-interface--system-tray-quick-reference-fr--en)

---

## 1. Understanding the "Test APEX" Diagnostic Output

When clicking **"Test APEX"** (or *"Tester l'APEX"* in the Tray/Control Panel), you might see diagnostic output similar to this:

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
  "buttons": 0,
  "warning": "APEX HID input unavailable (The APEX HID descriptor does not expose the complete X/Y/Rx/Ry/Z/Rz state required for a lossless DualSense proxy. Available value usages: page=0x1 usage=0x31 page=0x1 usage=0x30 page=0x1 usage=0x34 page=0x1 usage=0x33 page=0x1 usage=0x32 page=0x1 usage=0x39); trying XInput fallback."
}
```

### Is this an error?
> [!NOTE]
> **No, this indicates a 100% successful test of your physical controller!**

- **`"backend": "xinput-fallback"` & Warning:** On current Flydigi APEX firmware, the native direct HID descriptor omits the `Rz` axis control. ApexSenseBridge automatically and losslessly falls back to its optimized internal XInput reader to capture the full controller state. This is the expected and intended design.
- **`"received_state": true`, `"timeouts": 0`, `"parse_failures": 0`:** Confirms that the bridge opened the controller communication channel cleanly without dropped data.
- **`"state_changes": 1`:** If you held the controller still during the 2-second test window, `state_changes: 1` is completely normal (initial state capture). If you move the analog sticks or press buttons during the test, this counter will increase.
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

### Issue B: Game Only Scans for Controllers at Startup (Controller Enumeration Timing)
ApexSenseBridge's automatic game detection hooks into games right as their process is detected. However, some games (e.g. *007 First Light*, certain Unreal Engine or older DirectX titles) only enumerate connected controllers during their very first initialization frames. If the virtual DualSense attaches half a second later, the game misses it.

**Solution (Force Continuous Activation):**
1. Completely exit the game.
2. In the **ApexSenseBridge Tray**, enable **Force continuous activation** (or *Activation continue forcée*).
3. Wait until the tray tooltip / status shows **"Bridge active"**.
4. Launch the game (with Steam Input disabled).
5. The game will now find the virtual DualSense already present on boot.

---

### Issue C: Known Game-Side Bugs
Some games have known bugs with controller detection on PC.
- *Example:* *007 First Light* has a documented upstream issue where controller input is not recognized on the title/start screen (see [IO Interactive 007 Known Issues](https://007firstlight-support.zendesk.com/hc/en-us/articles/36203682496413-007-First-Light-Known-Issues)).

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
- When the game exits, ApexSenseBridge automatically restores the previous HidHide state.

### Restoring Controller Visibility After an Unexpected Crash
If the PC lost power or a game crashed unexpectedly while the controller was hidden:
- Click **"Restore controller visibility"** (*Restaurer la visibilité*) in the Tray app.
- Or run in Command Prompt / PowerShell:
  ```cmd
  ApexSenseBridge.exe restore-controller-visibility
  ```

---

## 5. Driver Installation Issues & Portable Version

### Installer stuck on "Uninstalling USBip..."
Upstream USBip installers can hang if an older incompatible driver version is already installed.
- **Solution:** Use the **Portable ZIP** package (`ApexSenseBridge-Portable.zip`).
- Extract the ZIP to your desired location.
- Right-click `Install-Drivers.cmd` and select **Run as Administrator**.
- The script checks and installs:
  - **`usbip-win2 0.9.7.7`** (Version `0.9.7.8` is explicitly rejected due to known upstream memory corruption and BSOD risks; `0.9.7.7` is WHLK-certified and safe).
  - **`HidHide 1.5.230`**.
- **Restart Windows** after driver installation.
- Launch `Start-ApexSenseBridge.cmd` or `ApexSenseBridgeTray.exe`.

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
