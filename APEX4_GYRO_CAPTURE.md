# APEX 4 gyroscope capture

`apex4-gyro-capture` is a support diagnostic used to locate the APEX 4's raw
gyroscope and accelerometer fields. It accepts only a hardware-verified APEX 4
and records its `04 FE` vendor input reports. It does not change controller
profiles, mappings, firmware, or onboard settings.

The generated JSON contains controller HID input only. It does not collect
keyboard input, mouse input, running processes, user names, or file-system
paths.

## Capture procedure

Close any active ApexSenseBridge game session, connect the APEX 4 in DInput
mode, and open a terminal in the folder containing `ApexSenseBridge.exe`.

Before running the tool, edit the active onboard profile in Flydigi Space
Station: set the gyro mapping to **Mouse**, configure it as always enabled (no
activation key), and apply the profile. The APEX 4 firmware leaves every IMU
field at zero while the profile's gyro mapping is Off. The diagnostic remains
read-only and will not change or restore this profile setting itself. The mouse
pointer may move during this standalone capture.

Run:

```powershell
.\ApexSenseBridge.exe apex4-gyro-capture --output apex4-gyro-dongle.json
```

The diagnostic guides the tester through four five-second phases:

1. Leave the controller flat and completely still.
2. Keep it flat, face up, and turn it left/right around a vertical axis without
   tilting the grips (yaw).
3. Tilt its front edge up and down repeatedly (pitch).
4. Raise one grip while lowering the other, then reverse; do not turn the
   controller left/right (roll).

Do not press buttons, move either stick, or pull the triggers during the test.
The tool prints `GO` when each recording phase begins.

If no IMU bytes are observed, the JSON is still saved for investigation but
the command exits with code 7 and asks for a new capture with gyro mapping
enabled.

If possible, repeat the procedure over wired USB:

```powershell
.\ApexSenseBridge.exe apex4-gyro-capture --output apex4-gyro-wired.json
```

Attach both JSON files to GitHub issue #10. The raw reports and per-byte
activity summaries make it possible to identify sensor offsets, byte order,
axis orientation, and scaling independently for the dongle and wired paths.

Use `--phase-seconds N` to select a duration from 2 to 30 seconds. The tool
never overwrites an existing capture. If no output path is supplied, it creates
a unique timestamped filename in the current directory.
