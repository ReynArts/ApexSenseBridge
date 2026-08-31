# ApexSenseBridge 0.2

A small Windows proof-of-concept for talking directly to the Flydigi APEX 5 FORCEADAPT vendor HID interface.

**v0.2 is deliberately not the final DualSense bridge yet.** Its job is to validate the one hardware path everything else depends on:

`Windows app -> Flydigi vendor HID -> APEX 5 FORCEADAPT`

## Design goals

- Keep the APEX 5's normal XInput path untouched.
- No input remapping in this stage, so no added stick/button latency.
- Isolate protocol, device, and Windows HID transport layers.
- Never send commands to a generic Xbox/HID gamepad interface by guessing.
- Always try to restore both triggers to Normal after the physical test.

## Device identification

The program only considers a HID interface a candidate when all three checks match:

- Flydigi VID `0x37D7`
- controller product family `(PID >> 12) == 2`
- vendor usage page `0xFFA0`

This is based on the APEX 5 vendor-command interface documented and measured by OpenFlydigi. The protocol is used as documentation; this project's C++ implementation is original.

## FORCEADAPT packet used in v0.2

The vendor HID packet is 32 bytes and starts with:

`03 5A A5 <command> <payload length> ...`

APEX 5 live trigger effects use command `81` and one command per trigger.

The first hardware test uses a deliberately gentle `Race` resistance on **RT only**:

- start position: 70
- resistance: 30 / 255
- duration: ~1.5 s
- then LT and RT are both reset to Normal

## Build on Windows

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 Build Tools (or Visual Studio 2022)
- workload: **Desktop development with C++**
- CMake available in PATH

From PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build-windows.ps1
```

Then:

```powershell
.\build-win\Release\ApexSenseBridge.exe list
```

You should see a Flydigi candidate with VID `0x37D7`, a PID in the `0x2xxx` family and usage page `0xFFA0`.

If exactly one is found:

```powershell
.\build-win\Release\ApexSenseBridge.exe test-rt
```

If several are found, use the index printed by `list`:

```powershell
.\build-win\Release\ApexSenseBridge.exe test-rt 0
```

## Safety / recovery

The test uses an RAII reset guard and also explicitly resets LT + RT before exiting normally. `Ctrl+C` requests a clean exit so the reset path can run.

No user-space program can guarantee cleanup after a kernel crash, forced power loss or `TerminateProcess`. If a trigger remains in an effect after an abnormal termination, open Flydigi Space Station and set both triggers to **Normal**, or reconnect/restart the controller.

The test intentionally does **not** edit firmware or stored controller profiles.

## Commands

```text
list             list matching vendor HID interfaces
test-rt [index]  apply a gentle RT resistance for ~1.5 s and reset
clear [index]    reset LT + RT to Normal
dry-run          print the test packet without touching hardware
```

## Architecture

```text
src/core/                 value types + safety helpers
src/flydigi/              protocol encoder + APEX device facade
src/platform/windows/     Windows SetupAPI/HID transport
src/main.cpp              thin CLI only
```

The next milestone, **0.3**, is conditional on this test succeeding. It will add a virtual DualSense output-capture backend while leaving the physical APEX 5 input path native whenever the game allows it.

## Upstream technical references

- OpenFlydigi (`mkaliaha/openflydigi`, MIT): APEX 5 vendor protocol, VID/PID family matching, vendor usage page, command 81/82 behaviour.
- SteamlessController DualSense fork (`david419kr/steamless-controller-XB-PS-NS`, GPLv3 ecosystem): reference architecture for a Windows virtual DualSense and output feedback capture. **No Steamless/VIIPER code is included in v0.2.**


## Windows build note (v0.2.1)

The build script explicitly selects the Visual Studio 2022 generator. A normal PowerShell session must not silently fall back to NMake. Required workload: **Desktop development with C++**, including MSVC v143 and a Windows SDK.
