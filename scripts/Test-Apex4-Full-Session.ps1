<#
.SYNOPSIS
    Runs a bounded full APEX 4 to virtual DualSense validation session.
.DESCRIPTION
    Requires compatible usbip-win2 and HidHide drivers already installed.
    The helper elevates for temporary HidHide isolation, starts the integrated
    virtual DualSense backend, opens joy.cpl, and creates a small result ZIP.
#>

[CmdletBinding()]
param(
    [string]$BridgeExecutable = "",
    [string]$ViiperLibrary = "",
    [string]$OutputDirectory = [Environment]::GetFolderPath("Desktop"),
    [ValidateRange(60, 900)]
    [int]$SafetyTimeoutSeconds = 600
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$script:TestVersion = "1.5.1"

if ([string]::IsNullOrWhiteSpace($BridgeExecutable)) {
    $BridgeExecutable = Join-Path $PSScriptRoot "ApexSenseBridge.exe"
}
if ([string]::IsNullOrWhiteSpace($ViiperLibrary)) {
    $ViiperLibrary = Join-Path $PSScriptRoot "libVIIPER.dll"
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Quote-Argument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Write-Utf8File([string]$Path, [string[]]$Lines) {
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [IO.File]::WriteAllLines($Path, $Lines, $encoding)
}

function Add-Result([System.Collections.Generic.List[string]]$Lines,
                    [string]$Name,
                    [object]$Value) {
    $text = if ($null -eq $Value) { "" } else { [string]$Value }
    $Lines.Add($Name + "=" + $text.Replace("`r", " ").Replace("`n", " "))
}

function Invoke-BridgeCapture([string[]]$Arguments) {
    $previousPreference = $ErrorActionPreference
    try {
        # Preserve native stderr in the result without allowing Windows
        # PowerShell to promote it to a terminating NativeCommandError.
        $ErrorActionPreference = "Continue"
        $lines = @(& $script:BridgePath @Arguments 2>&1 |
            ForEach-Object { [string]$_ })
        $nativeExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    return [pscustomobject]@{
        Lines = $lines
        ExitCode = $nativeExitCode
    }
}

function Test-HidPathVisibility([string]$DevicePath) {
    if ($null -eq ("ApexSenseBridge.HidVisibilityProbe" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace ApexSenseBridge {
    public static class HidVisibilityProbe {
        public sealed class RawInputEventResult {
            public string DevicePath { get; set; }
            public int EventCount { get; set; }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RawInputDeviceList {
            public IntPtr Device;
            public uint Type;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RawInputDevice {
            public ushort UsagePage;
            public ushort Usage;
            public uint Flags;
            public IntPtr Target;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RawInputHeader {
            public uint Type;
            public uint Size;
            public IntPtr Device;
            public IntPtr WParam;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Point {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Message {
            public IntPtr Window;
            public uint Id;
            public UIntPtr WParam;
            public IntPtr LParam;
            public uint Time;
            public Point Cursor;
            public uint Private;
        }

        private delegate IntPtr WindowProcedure(
            IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WindowClass {
            public uint Size;
            public uint Style;
            public IntPtr WindowProcedure;
            public int ClassExtra;
            public int WindowExtra;
            public IntPtr Instance;
            public IntPtr Icon;
            public IntPtr Cursor;
            public IntPtr Background;
            public string MenuName;
            public string ClassName;
            public IntPtr SmallIcon;
        }

        private static readonly WindowProcedure EventWindowProcedure = OnWindowMessage;
        private static Dictionary<string, int> eventCounts;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr CreateFile(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputDeviceList(
            [Out] RawInputDeviceList[] devices, ref uint deviceCount,
            uint structureSize);

        [DllImport("user32.dll", EntryPoint = "GetRawInputDeviceInfoW",
            CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetRawInputDeviceInfo(
            IntPtr device, uint command, StringBuilder data, ref uint dataSize);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetRawInputData(
            IntPtr rawInput, uint command, IntPtr data,
            ref uint dataSize, uint headerSize);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool RegisterRawInputDevices(
            RawInputDevice[] devices, uint deviceCount, uint structureSize);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern ushort RegisterClassEx(ref WindowClass windowClass);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateWindowEx(
            uint extendedStyle, string className, string windowName,
            uint style, int x, int y, int width, int height,
            IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyWindow(IntPtr window);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UnregisterClass(string className, IntPtr instance);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool PeekMessage(
            out Message message, IntPtr window,
            uint minimumMessage, uint maximumMessage, uint removeMessage);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool TranslateMessage(ref Message message);

        [DllImport("user32.dll")]
        private static extern IntPtr DispatchMessage(ref Message message);

        [DllImport("user32.dll")]
        private static extern IntPtr DefWindowProc(
            IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string moduleName);

        // Enumeration is diagnostic only: being listed does not prove that
        // an ordinary application receives events from the device.
        public static int RawInputVisibility(string wantedPath) {
            uint count = 0;
            uint structureSize = (uint)Marshal.SizeOf(typeof(RawInputDeviceList));
            uint result = GetRawInputDeviceList(null, ref count, structureSize);
            if (result == UInt32.MaxValue) return LastError();
            if (count == 0) return 0;

            RawInputDeviceList[] devices = new RawInputDeviceList[count];
            result = GetRawInputDeviceList(devices, ref count, structureSize);
            if (result == UInt32.MaxValue) return LastError();

            const uint DeviceName = 0x20000007;
            for (uint index = 0; index < result; ++index) {
                uint characters = 0;
                uint nameResult = GetRawInputDeviceInfo(
                    devices[index].Device, DeviceName, null, ref characters);
                if (nameResult == UInt32.MaxValue || characters == 0) continue;
                StringBuilder name = new StringBuilder((int)characters);
                nameResult = GetRawInputDeviceInfo(
                    devices[index].Device, DeviceName, name, ref characters);
                if (nameResult == UInt32.MaxValue) continue;
                if (StringComparer.OrdinalIgnoreCase.Equals(
                        name.ToString(), wantedPath)) return 1;
            }
            return 0;
        }

        public static RawInputEventResult[] CaptureRawInputEvents(
            string[] wantedPaths, int durationMilliseconds) {
            if (wantedPaths == null) throw new ArgumentNullException("wantedPaths");
            if (durationMilliseconds < 1) {
                throw new ArgumentOutOfRangeException("durationMilliseconds");
            }

            eventCounts = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (string path in wantedPaths) {
                if (!String.IsNullOrWhiteSpace(path) && !eventCounts.ContainsKey(path)) {
                    eventCounts.Add(path, 0);
                }
            }

            IntPtr instance = GetModuleHandle(null);
            string className = "ApexSenseBridgeRawInput_" + Guid.NewGuid().ToString("N");
            WindowClass windowClass = new WindowClass {
                Size = (uint)Marshal.SizeOf(typeof(WindowClass)),
                WindowProcedure = Marshal.GetFunctionPointerForDelegate(EventWindowProcedure),
                Instance = instance,
                ClassName = className
            };
            if (RegisterClassEx(ref windowClass) == 0) {
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "RegisterClassEx failed");
            }

            IntPtr window = IntPtr.Zero;
            try {
                IntPtr messageOnlyWindow = new IntPtr(-3);
                window = CreateWindowEx(0, className, "", 0,
                    0, 0, 0, 0, messageOnlyWindow, IntPtr.Zero,
                    instance, IntPtr.Zero);
                if (window == IntPtr.Zero) {
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "CreateWindowEx failed");
                }

                const uint inputSink = 0x00000100;
                RawInputDevice[] registrations = new RawInputDevice[] {
                    new RawInputDevice { UsagePage = 0x01, Usage = 0x05,
                        Flags = inputSink, Target = window },
                    new RawInputDevice { UsagePage = 0x01, Usage = 0x04,
                        Flags = inputSink, Target = window },
                    new RawInputDevice { UsagePage = 0x01, Usage = 0x02,
                        Flags = inputSink, Target = window }
                };
                if (!RegisterRawInputDevices(registrations,
                        (uint)registrations.Length,
                        (uint)Marshal.SizeOf(typeof(RawInputDevice)))) {
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "RegisterRawInputDevices failed");
                }

                Stopwatch timer = Stopwatch.StartNew();
                Message message;
                while (timer.ElapsedMilliseconds < durationMilliseconds) {
                    while (PeekMessage(out message, window, 0, 0, 1)) {
                        TranslateMessage(ref message);
                        DispatchMessage(ref message);
                    }
                    Thread.Sleep(1);
                }

                RawInputEventResult[] results =
                    new RawInputEventResult[eventCounts.Count];
                int index = 0;
                foreach (KeyValuePair<string, int> item in eventCounts) {
                    results[index++] = new RawInputEventResult {
                        DevicePath = item.Key,
                        EventCount = item.Value
                    };
                }
                return results;
            }
            finally {
                if (window != IntPtr.Zero) DestroyWindow(window);
                UnregisterClass(className, instance);
                eventCounts = null;
            }
        }

        private static IntPtr OnWindowMessage(
            IntPtr window, uint message, UIntPtr wParam, IntPtr lParam) {
            const uint rawInputMessage = 0x00FF;
            if (message == rawInputMessage && eventCounts != null) {
                uint size = 0;
                uint headerSize = (uint)Marshal.SizeOf(typeof(RawInputHeader));
                uint result = GetRawInputData(lParam, 0x10000003,
                    IntPtr.Zero, ref size, headerSize);
                if (result != UInt32.MaxValue && size >= headerSize) {
                    IntPtr buffer = Marshal.AllocHGlobal((int)size);
                    try {
                        result = GetRawInputData(lParam, 0x10000003,
                            buffer, ref size, headerSize);
                        if (result != UInt32.MaxValue) {
                            RawInputHeader header =
                                (RawInputHeader)Marshal.PtrToStructure(
                                    buffer, typeof(RawInputHeader));
                            string path = DevicePath(header.Device);
                            if (path != null && eventCounts.ContainsKey(path)) {
                                eventCounts[path] = eventCounts[path] + 1;
                            }
                        }
                    }
                    finally {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            }
            return DefWindowProc(window, message, wParam, lParam);
        }

        private static string DevicePath(IntPtr device) {
            const uint deviceName = 0x20000007;
            uint characters = 0;
            uint result = GetRawInputDeviceInfo(
                device, deviceName, null, ref characters);
            if (result == UInt32.MaxValue || characters == 0) return null;
            StringBuilder name = new StringBuilder((int)characters);
            result = GetRawInputDeviceInfo(
                device, deviceName, name, ref characters);
            return result == UInt32.MaxValue ? null : name.ToString();
        }

        private static int LastError() {
            int error = Marshal.GetLastWin32Error();
            return error == 0 ? -1 : -error;
        }
    }
}
"@
    }

    # Zero desired access still traverses HidHide's create gate but avoids the
    # exclusive-read rules Windows applies to ordinary mouse collections.
    $desiredAccess = [uint32]0
    $shareReadWriteDelete = [uint32]0x00000007
    $openExisting = [uint32]3
    $handle = [ApexSenseBridge.HidVisibilityProbe]::CreateFile(
        $DevicePath, $desiredAccess, $shareReadWriteDelete,
        [IntPtr]::Zero, $openExisting, [uint32]0, [IntPtr]::Zero)
    if ($handle -ne [IntPtr](-1)) {
        [void][ApexSenseBridge.HidVisibilityProbe]::CloseHandle($handle)
        $directState = "visible"
        $directError = 0
    }
    else {
        $directError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $directState = if ($directError -eq 5) { "hidden" } else { "probe_error" }
    }

    $rawResult = [ApexSenseBridge.HidVisibilityProbe]::RawInputVisibility($DevicePath)
    if ($rawResult -eq 1) {
        $rawState = "visible"
        $rawError = 0
    }
    elseif ($rawResult -eq 0) {
        $rawState = "hidden"
        $rawError = 0
    }
    else {
        $rawState = "probe_error"
        $rawError = -$rawResult
    }
    return [pscustomobject]@{
        State = $rawState
        WindowsError = $rawError
        DirectState = $directState
        DirectWindowsError = $directError
    }
}

if (-not (Test-IsAdministrator)) {
    try {
        Write-Host "Cette session complete demande une elevation temporaire pour HidHide."
        $powerShell = Join-Path $env:SystemRoot `
            "System32\WindowsPowerShell\v1.0\powershell.exe"
        $arguments = @(
            "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", (Quote-Argument $PSCommandPath),
            "-BridgeExecutable", (Quote-Argument $BridgeExecutable),
            "-ViiperLibrary", (Quote-Argument $ViiperLibrary),
            "-OutputDirectory", (Quote-Argument $OutputDirectory),
            "-SafetyTimeoutSeconds", [string]$SafetyTimeoutSeconds
        )
        $elevated = Start-Process -FilePath $powerShell -ArgumentList $arguments `
            -Verb RunAs -Wait -PassThru
        exit $elevated.ExitCode
    }
    catch {
        Write-Error ("Elevation annulee ou impossible : " + $_.Exception.Message)
        exit 10
    }
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$resultLines = New-Object System.Collections.Generic.List[string]
Add-Result $resultLines "test_version" $script:TestVersion
Add-Result $resultLines "started_at" ([DateTimeOffset]::Now.ToString('o'))
Add-Result $resultLines "safety_timeout_seconds" $SafetyTimeoutSeconds

$session = $null
$sessionStarted = $false
$exitCode = 0
$workingDirectory = $null
$stdoutPath = $null
$stderrPath = $null
$telemetryPath = $null
$zipPath = $null
try {
    if ([string]::IsNullOrWhiteSpace($env:TEMP)) {
        throw "La variable TEMP de Windows est vide."
    }
    $workingDirectory = Join-Path $env:TEMP `
        ("Apex4-Full-Session-Test-" + $stamp + "-" + [guid]::NewGuid().ToString("N"))
    [void](New-Item -ItemType Directory -Path $workingDirectory -ErrorAction Stop)
    $stdoutPath = Join-Path $workingDirectory "03-session.stdout.txt"
    $stderrPath = Join-Path $workingDirectory "04-session.stderr.txt"
    $telemetryPath = Join-Path $workingDirectory "05-telemetry.json"

    $script:BridgePath =
        (Resolve-Path -LiteralPath $BridgeExecutable -ErrorAction Stop).Path
    $script:LibraryPath =
        (Resolve-Path -LiteralPath $ViiperLibrary -ErrorAction Stop).Path
    Add-Result $resultLines "bridge" $script:BridgePath
    if ([IO.Path]::GetFileName($script:BridgePath) -ine "ApexSenseBridge.exe") {
        throw "BridgeExecutable doit pointer vers ApexSenseBridge.exe."
    }
    if ([IO.Path]::GetFileName($script:LibraryPath) -ine "libVIIPER.dll") {
        throw "ViiperLibrary doit pointer vers libVIIPER.dll."
    }
    if ((Split-Path -Parent $script:BridgePath) -ine
        (Split-Path -Parent $script:LibraryPath)) {
        throw "ApexSenseBridge.exe et libVIIPER.dll doivent etre dans le meme dossier."
    }

    $usbipUdeService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude"
    $usbipFilterService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_filter"
    $hidHideService = "HKLM:\SYSTEM\CurrentControlSet\Services\HidHide"
    $usbipKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1"
    $hidHideKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF}"
    if (-not (Test-Path -LiteralPath $usbipUdeService) -or
        -not (Test-Path -LiteralPath $usbipFilterService) -or
        -not (Test-Path -LiteralPath $hidHideService)) {
        throw "Pilotes manquants : installer d'abord usbip-win2 et HidHide avec le paquet Portable, redemarrer Windows, puis relancer ce test."
    }
    $usbip = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
    $hidHide = Get-ItemProperty -LiteralPath $hidHideKey -ErrorAction SilentlyContinue
    $usbipVersion = if ($null -ne $usbip) {
        ([string]$usbip.DisplayVersion).Trim()
    } else { "" }
    $hidHideVersion = if ($null -ne $hidHide) {
        ([string]$hidHide.DisplayVersion).Trim()
    } else { "" }
    Add-Result $resultLines "usbip_version" $usbipVersion
    Add-Result $resultLines "hidhide_version" $hidHideVersion
    if ($usbipVersion -notin @("0.9.7.5", "0.9.7.6", "0.9.7.7")) {
        throw "Version usbip-win2 absente ou non prise en charge ($usbipVersion). Utiliser uniquement 0.9.7.5 a 0.9.7.7."
    }
    if ($hidHideVersion -ne "1.5.230") {
        throw "Version HidHide absente ou non validee ($hidHideVersion). La version attendue est 1.5.230."
    }

    Clear-Host
    Write-Host "ApexSenseBridge - session complete APEX 4" -ForegroundColor Green
    Write-Host ""
    Write-Host "Preparation :"
    Write-Host "  1. Utiliser le cable ou le dongle 2,4 GHz en mode DInput."
    Write-Host "  2. Fermer Flydigi Space Station."
    Write-Host "  3. Pour un jeu Steam, desactiver Steam Input pour ce jeu."
    Write-Host "  4. La session s'arrete seule apres $SafetyTimeoutSeconds secondes au maximum."
    [void](Read-Host "Appuyer sur Entree quand la manette est prete")

    $listResult = Invoke-BridgeCapture @("list")
    $listCode = $listResult.ExitCode
    Write-Utf8File (Join-Path $workingDirectory "01-list.txt") $listResult.Lines
    Add-Result $resultLines "list_exit_code" $listCode
    if ($listCode -ne 0) {
        throw "L'interface APEX 4 n'a pas ete detectee en DInput."
    }

    # bridge-triggers performs and retains the identity verification itself.
    # A separate identify process is unreliable with the rate-limited APEX 4
    # dongle and would discard the authorization immediately on exit.
    Add-Result $resultLines "identity_preflight" "deferred_to_bridge_process"

    $arguments = @(
        "bridge-triggers",
        "--seconds", [string]$SafetyTimeoutSeconds,
        "--virtual-backend", "integrated",
        "--verify-virtual-input",
        "--rumble",
        "--haptic-threshold", "12",
        "--telemetry-json", (Quote-Argument $telemetryPath)
    )
    Write-Host ""
    Write-Host "Demarrage du DualSense virtuel..." -ForegroundColor Cyan
    $session = Start-Process -FilePath $script:BridgePath `
        -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $script:BridgePath) `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath `
        -WindowStyle Hidden -PassThru
    $sessionStarted = $true

    $virtualSeen = $false
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        $session.Refresh()
        if ($session.HasExited) { break }
        $hidJson = @(& $script:BridgePath diagnose --all-hid --json 2>$null) -join "`n"
        if ($hidJson -match '"vendor_id_hex"\s*:\s*"0x054C"' -and
            $hidJson -match '"product_id_hex"\s*:\s*"0x0CE6"') {
            $virtualSeen = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    $virtualSeenText = if ($virtualSeen) { "yes" } else { "no" }
    Add-Result $resultLines "virtual_dualsense_seen" $virtualSeenText

    $session.Refresh()
    if ($session.HasExited) {
        throw "La session s'est arretee pendant son initialisation. Consulter 04-session.stderr.txt."
    }
    if (-not $virtualSeen) {
        throw "Le DualSense virtuel 054C:0CE6 n'est pas apparu sous 20 secondes."
    }

    # The bridge executable is whitelisted in HidHide, so it can still discover
    # the physical collections. This PowerShell process is deliberately not
    # whitelisted. Raw Input enumeration is recorded for diagnosis only; the
    # decisive check below listens for actual physical events.
    $diagnoseResult = Invoke-BridgeCapture @("diagnose", "--all-hid", "--json")
    if ($diagnoseResult.ExitCode -ne 0) {
        throw "Impossible d'enumerer les interfaces HID pour verifier leur masquage."
    }
    try {
        $diagnose = ($diagnoseResult.Lines -join "`n") | ConvertFrom-Json
    }
    catch {
        throw "Le diagnostic HID JSON est invalide : $($_.Exception.Message)"
    }
    $physicalInputs = @($diagnose.devices | Where-Object {
        [int]$_.vendor_id -eq 0x04B4 -and
        [int]$_.product_id -eq 0x2412 -and
        [int]$_.usage_page -eq 1 -and
        (([int]$_.usage -eq 5 -and [string]$_.instance_id -match '&MI_00') -or
         ([int]$_.usage -eq 2 -and [string]$_.instance_id -match '&MI_01'))
    })
    $probeLines = New-Object System.Collections.Generic.List[string]
    $probeLines.Add("test_version=$script:TestVersion")
    $probeLines.Add("raw_input_enumeration=diagnostic_only")
    $probeLines.Add("expected=no MI_00 or MI_01 event received by this non-whitelisted process")
    foreach ($physicalInput in $physicalInputs) {
        $probe = Test-HidPathVisibility ([string]$physicalInput.device_path)
        $interface = [string]$physicalInput.interface_number
        $probeLines.Add(
            "interface=$interface usage=$($physicalInput.usage) raw_enumeration=$($probe.State) raw_error=$($probe.WindowsError) direct_open=$($probe.DirectState) direct_error=$($probe.DirectWindowsError)")
    }
    Add-Result $resultLines "physical_input_interfaces_found" $physicalInputs.Count
    if ($physicalInputs.Count -ne 2) {
        Add-Result $resultLines "physical_input_isolation" "inconclusive"
        Write-Utf8File (Join-Path $workingDirectory "02-isolation-probe.txt") $probeLines.ToArray()
        throw "Les interfaces physiques MI_00/MI_01 attendues n'ont pas toutes ete trouvees ; jeu non lance."
    }

    Write-Host ""
    Write-Host "Verification anti-double-entree :" -ForegroundColor Cyan
    Write-Host "  Apres avoir appuye sur Entree, bouger les deux sticks et presser"
    Write-Host "  les boutons et les deux gachettes pendant 6 secondes."
    [void](Read-Host "Appuyer sur Entree pour lancer les 6 secondes de verification")

    $rawEventLeakCount = 0
    $rawEventStatus = "no_leak_observed"
    try {
        $wantedPaths = [string[]]@($physicalInputs | ForEach-Object {
            [string]$_.device_path
        })
        $eventResults = @(
            [ApexSenseBridge.HidVisibilityProbe]::CaptureRawInputEvents(
                $wantedPaths, 6000)
        )
        foreach ($eventResult in $eventResults) {
            $matchingInput = @($physicalInputs | Where-Object {
                [string]$_.device_path -ieq [string]$eventResult.DevicePath
            } | Select-Object -First 1)
            $interface = if ($matchingInput.Count -gt 0) {
                [string]$matchingInput[0].interface_number
            } else {
                "unknown"
            }
            $eventCount = [int]$eventResult.EventCount
            $rawEventLeakCount += $eventCount
            $probeLines.Add(
                "interface=$interface raw_events_during_probe=$eventCount")
        }
        if ($rawEventLeakCount -gt 0) {
            $rawEventStatus = "failed"
        }
    }
    catch {
        $rawEventStatus = "inconclusive"
        $probeLines.Add(
            "raw_event_probe_error=$($_.Exception.Message.Replace("`r", " ").Replace("`n", " "))")
        Write-Host "Le controle Raw Input n'a pas pu terminer ; la session continue et le ZIP contiendra l'erreur." -ForegroundColor Yellow
    }
    Add-Result $resultLines "physical_input_event_leaks" $rawEventLeakCount
    Add-Result $resultLines "physical_input_isolation" $rawEventStatus
    Write-Utf8File (Join-Path $workingDirectory "02-isolation-probe.txt") $probeLines.ToArray()
    if ($rawEventLeakCount -gt 0) {
        throw "Des evenements APEX 4 physiques ont atteint un processus non autorise ; jeu non lance pour eviter une double entree."
    }
    Write-Host "Aucune fuite d'evenement physique observee." -ForegroundColor Green

    Write-Host ""
    Write-Host "Session active : le panneau des manettes Windows va s'ouvrir." -ForegroundColor Green
    Write-Host "Dans 'Wireless Controller > Proprietes', tester sticks, boutons, croix et gachettes."
    Write-Host "La manette Flydigi physique ne doit pas creer de double entree."
    Write-Host "Pour valider les retours, lancer maintenant un jeu compatible DualSense."
    Write-Host "Quand les tests sont termines, revenir ici et appuyer sur Entree."
    Start-Process -FilePath (Join-Path $env:SystemRoot "System32\control.exe") `
        -ArgumentList "joy.cpl"
    [void](Read-Host "Appuyer sur Entree pour arreter proprement la session")

    $stopLines = @(& $script:BridgePath stop-active-sessions 2>&1 |
        ForEach-Object { [string]$_ })
    Write-Utf8File (Join-Path $workingDirectory "06-stop.txt") $stopLines
    if (-not $session.WaitForExit(30000)) {
        throw "La session n'a pas repondu a l'arret propre sous 30 secondes."
    }
    # Windows PowerShell can leave ExitCode unpopulated after only the timed
    # overload. The parameterless call is immediate now that exit is confirmed
    # and also drains redirected stdout/stderr before telemetry is packaged.
    $session.WaitForExit()
    $session.Refresh()
    $sessionExitCode = [int]$session.ExitCode
    Add-Result $resultLines "session_exit_code" $sessionExitCode
    $exitCode = $sessionExitCode

    Add-Result $resultLines "virtual_controls_ok" `
        (Read-Host "Tous les controles fonctionnaient sur Wireless Controller ? [oui/non]")
    Add-Result $resultLines "double_input_seen" `
        (Read-Host "Une double entree ou la manette physique visible a ete observee ? [oui/non]")
    Add-Result $resultLines "game_tested" `
        (Read-Host "Nom du jeu teste, ou aucun")
    Add-Result $resultLines "adaptive_triggers_felt" `
        (Read-Host "Gachettes adaptatives ressenties dans le jeu ? [oui/non/non teste]")
    Add-Result $resultLines "haptics_felt" `
        (Read-Host "Vibrations ou retours haptiques ressentis dans le jeu ? [oui/non/non teste]")
}
catch {
    $exitCode = 1
    Add-Result $resultLines "fatal_error" $_.Exception.Message
    Write-Host ""
    Write-Host ("ERREUR : " + $_.Exception.Message) -ForegroundColor Red
}
finally {
    if ($sessionStarted -and $null -ne $session) {
        try {
            $session.Refresh()
            if (-not $session.HasExited) {
                try { & $script:BridgePath stop-active-sessions 2>&1 | Out-Null }
                catch { }
                [void]$session.WaitForExit(30000)
            }
        }
        catch { }
    }

    Add-Result $resultLines "finished_at" ([DateTimeOffset]::Now.ToString('o'))

    if ([string]::IsNullOrWhiteSpace($workingDirectory) -or
        -not (Test-Path -LiteralPath $workingDirectory -PathType Container)) {
        try {
            $workingDirectory = Join-Path $PSScriptRoot `
                ("Apex4-Full-Session-Emergency-" + $stamp + "-" +
                 [guid]::NewGuid().ToString("N"))
            [void](New-Item -ItemType Directory -Path $workingDirectory `
                -ErrorAction Stop)
            Add-Result $resultLines "result_storage" "package_directory_fallback"
        }
        catch {
            $exitCode = 2
            Write-Host ""
            Write-Host ("ERREUR : impossible de creer le diagnostic : " +
                $_.Exception.Message) -ForegroundColor Red
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($workingDirectory) -and
        (Test-Path -LiteralPath $workingDirectory -PathType Container)) {
        try {
            Write-Utf8File (Join-Path $workingDirectory "00-result.txt") `
                $resultLines.ToArray()

            $outputCandidates = New-Object System.Collections.Generic.List[string]
            if (-not [string]::IsNullOrWhiteSpace($OutputDirectory)) {
                $outputCandidates.Add($OutputDirectory)
            }
            $currentDesktop = [Environment]::GetFolderPath("Desktop")
            if (-not [string]::IsNullOrWhiteSpace($currentDesktop) -and
                -not ($outputCandidates -contains $currentDesktop)) {
                $outputCandidates.Add($currentDesktop)
            }
            if (-not ($outputCandidates -contains $PSScriptRoot)) {
                $outputCandidates.Add($PSScriptRoot)
            }

            foreach ($candidateDirectory in $outputCandidates) {
                try {
                    if (-not (Test-Path -LiteralPath $candidateDirectory `
                            -PathType Container)) {
                        [void](New-Item -ItemType Directory `
                            -Path $candidateDirectory -Force -ErrorAction Stop)
                    }
                    $candidateZip = Join-Path $candidateDirectory `
                        ("Apex4-Full-Session-Test-" + $stamp + ".zip")
                    Write-Utf8File (Join-Path $workingDirectory "00-result.txt") `
                        $resultLines.ToArray()
                    Compress-Archive -Path (Join-Path $workingDirectory "*") `
                        -DestinationPath $candidateZip `
                        -CompressionLevel Optimal -Force -ErrorAction Stop
                    $zipPath = $candidateZip
                    break
                }
                catch {
                    Add-Result $resultLines "archive_warning" `
                        ($candidateDirectory + " : " + $_.Exception.Message)
                }
            }
        }
        catch {
            $exitCode = 2
            Add-Result $resultLines "archive_error" $_.Exception.Message
            try {
                Write-Utf8File (Join-Path $workingDirectory "00-result.txt") `
                    $resultLines.ToArray()
            }
            catch { }
        }

        if (-not [string]::IsNullOrWhiteSpace($zipPath)) {
            try {
                if (-not [string]::IsNullOrWhiteSpace($env:TEMP)) {
                    $resolvedTemp =
                        [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
                    $resolvedWorking = [IO.Path]::GetFullPath($workingDirectory)
                    if ($resolvedWorking.StartsWith(
                            $resolvedTemp,
                            [StringComparison]::OrdinalIgnoreCase)) {
                        Remove-Item -LiteralPath $resolvedWorking `
                            -Recurse -Force -ErrorAction Stop
                    }
                }
            }
            catch {
                Write-Host ("Avertissement : dossier temporaire conserve : " +
                    $_.Exception.Message) -ForegroundColor Yellow
            }

            Write-Host ""
            Write-Host "Renvoyer ce fichier :" -ForegroundColor Green
            Write-Host $zipPath -ForegroundColor Green
        }
        else {
            $exitCode = 2
            Write-Host ""
            Write-Host "ERREUR : le ZIP n'a pas pu etre cree." -ForegroundColor Red
            Write-Host "Le diagnostic brut a ete conserve ici :" -ForegroundColor Yellow
            Write-Host $workingDirectory -ForegroundColor Yellow
        }
    }

    if ($exitCode -ne 0) {
        Write-Host ""
        [void](Read-Host "L'erreur a ete conservee. Appuyer sur Entree pour fermer")
    }
}

exit $exitCode
