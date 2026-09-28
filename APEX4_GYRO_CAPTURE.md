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
Run:

```powershell
.\ApexSenseBridge.exe apex4-gyro-capture --output apex4-gyro-dongle.json
```

The diagnostic guides the tester through four five-second phases:

1. Leave the controller flat and completely still.
2. Rotate it left and right repeatedly (yaw).
3. Tilt its front edge up and down repeatedly (pitch).
4. Roll it clockwise and counterclockwise repeatedly (roll).

Do not press buttons, move either stick, or pull the triggers during the test.
The tool prints `GO` when each recording phase begins.

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
