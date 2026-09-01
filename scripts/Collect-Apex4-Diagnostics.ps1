<#
.SYNOPSIS
    Collects read-only Windows, HID, PnP, and XInput diagnostics for a Flydigi APEX 4.
.DESCRIPTION
    This collector does not install a driver, change a controller setting, or send
    vendor/output reports. If ApexSenseBridge.exe is available, it only invokes the
    read-only "diagnose --json" command to obtain HID capabilities.

    The resulting ZIP is intended to contain enough information to implement and
    validate the APEX 4 transport without asking a tester to use developer tools.
.PARAMETER OutputDirectory
    Directory in which the diagnostic ZIP is created. Defaults to the Desktop.
.PARAMETER BridgeExecutable
    Optional explicit path to ApexSenseBridge.exe. When omitted, common portable,
    build, registry, and installed locations are checked.
.PARAMETER InputSampleSeconds
    Number of seconds during which connected XInput devices are sampled. During
    this time, move both sticks and press both triggers and several buttons.
.PARAMETER SkipInputSample
    Skips the interactive XInput range sample. Slot enumeration is still performed.
.PARAMETER KeepWorkingDirectory
    Keeps the uncompressed working directory next to the ZIP for troubleshooting.
#>

[CmdletBinding()]
param(
    [string]$OutputDirectory = [Environment]::GetFolderPath("Desktop"),
    [string]$BridgeExecutable = "",
    [ValidateRange(1, 30)]
    [int]$InputSampleSeconds = 8,
    [switch]$SkipInputSample,
    [switch]$KeepWorkingDirectory
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$script:CollectorVersion = "1.3.0"
$script:Warnings = New-Object System.Collections.Generic.List[string]
$script:Steps = New-Object System.Collections.Generic.List[object]

function Add-Warning([string]$Message) {
    $script:Warnings.Add($Message)
    Write-Host ("[WARNING] " + $Message) -ForegroundColor Yellow
}

function Add-Step([string]$Name, [bool]$Success, [string]$Detail) {
    $script:Steps.Add([pscustomobject][ordered]@{
        name = $Name
        success = $Success
        detail = $Detail
    })
}

function Write-Utf8File([string]$Path, [string]$Text) {
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Write-JsonFile([string]$Path, $Value, [int]$Depth = 8) {
    $json = $Value | ConvertTo-Json -Depth $Depth
    Write-Utf8File $Path ($json + [Environment]::NewLine)
}

function Convert-PropertyData($Data) {
    if ($null -eq $Data) {
        return $null
    }
    if ($Data -is [System.Array]) {
        return @($Data | ForEach-Object { [string]$_ })
    }
    return [string]$Data
}

function Test-IsAdministrator {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    }
    catch {
        return $false
    }
}

function Resolve-BridgeExecutable([string]$ExplicitPath) {
    $paths = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $paths.Add($ExplicitPath)
    }

    $paths.Add((Join-Path $PSScriptRoot "ApexSenseBridge.exe"))
    $paths.Add((Join-Path $PSScriptRoot "..\build-win\Release\ApexSenseBridge.exe"))
    $paths.Add((Join-Path $PSScriptRoot "..\build-verify\Release\ApexSenseBridge.exe"))

    try {
        $install = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\ApexSenseBridge" `
            -ErrorAction Stop
        if (-not [string]::IsNullOrWhiteSpace([string]$install.ExecutablePath)) {
            $paths.Add([string]$install.ExecutablePath)
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$install.InstallPath)) {
            $paths.Add((Join-Path ([string]$install.InstallPath) "ApexSenseBridge.exe"))
        }
    }
    catch {
        # An installation is optional.
    }

    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $paths.Add((Join-Path $env:ProgramFiles "ApexSenseBridge\ApexSenseBridge.exe"))
    }

    foreach ($path in $paths) {
        if ([string]::IsNullOrWhiteSpace($path)) {
            continue
        }
        try {
            $resolved = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
            if ([System.IO.Path]::GetFileName($resolved) -ieq "ApexSenseBridge.exe") {
                return $resolved
            }
        }
        catch {
            # Try the next location.
        }
    }
    return $null
}

function Invoke-NativeCapture(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$StandardOutputPath,
    [string]$StandardErrorPath
) {
    $stdout = @()
    $exitCode = -1
    try {
        $stdout = @(& $FilePath @Arguments 2> $StandardErrorPath)
        $exitCode = $LASTEXITCODE
        Write-Utf8File $StandardOutputPath (($stdout | ForEach-Object { [string]$_ }) -join [Environment]::NewLine)
    }
    catch {
        Write-Utf8File $StandardOutputPath ""
        Write-Utf8File $StandardErrorPath ($_.Exception.Message + [Environment]::NewLine)
    }
    return $exitCode
}

function Get-PnpPropertyMap([string[]]$InstanceIds, [string[]]$KeyNames) {
    $map = @{}
    if ($InstanceIds.Count -eq 0) {
        return $map
    }

    # Windows 10's PnP provider can reject one large multi-device request with
    # WBEM_E_QUOTA_VIOLATION. Small batches avoid that provider-wide failure.
    $properties = New-Object System.Collections.Generic.List[object]
    $batchSize = 12
    for ($offset = 0; $offset -lt $InstanceIds.Count; $offset += $batchSize) {
        $last = [Math]::Min($offset + $batchSize - 1, $InstanceIds.Count - 1)
        $batch = @($InstanceIds[$offset..$last])
        try {
            $batchProperties = if ($KeyNames.Count -eq 0) {
                @(Get-PnpDeviceProperty -InstanceId $batch -ErrorAction Stop)
            }
            else {
                @(Get-PnpDeviceProperty -InstanceId $batch -KeyName $KeyNames -ErrorAction Stop)
            }
            foreach ($property in $batchProperties) {
                $properties.Add($property)
            }
        }
        catch {
            # A broken device must not discard the rest of the controller topology.
            foreach ($instanceId in $batch) {
                try {
                    $singleProperties = if ($KeyNames.Count -eq 0) {
                        @(Get-PnpDeviceProperty -InstanceId $instanceId -ErrorAction Stop)
                    }
                    else {
                        @(Get-PnpDeviceProperty -InstanceId $instanceId -KeyName $KeyNames -ErrorAction Stop)
                    }
                    foreach ($property in $singleProperties) {
                        $properties.Add($property)
                    }
                }
                catch {
                    # Preserve every other readable interface.
                }
            }
        }
    }

    foreach ($property in $properties) {
        $instanceProperty = $property.PSObject.Properties["InstanceId"]
        $keyProperty = $property.PSObject.Properties["KeyName"]
        if ($null -eq $instanceProperty -or $null -eq $keyProperty) {
            continue
        }
        $id = [string]$instanceProperty.Value
        $key = [string]$keyProperty.Value
        if ([string]::IsNullOrWhiteSpace($id) -or [string]::IsNullOrWhiteSpace($key)) {
            continue
        }
        $dataProperty = $property.PSObject.Properties["Data"]
        $data = if ($null -eq $dataProperty) { $null } else { $dataProperty.Value }
        if (-not $map.ContainsKey($id)) {
            $map[$id] = @{}
        }
        $map[$id][$key] = Convert-PropertyData $data
    }
    return $map
}

function Get-ConnectedModelAssessment([string]$HidJsonPath, $PnpResult) {
    $result = [ordered]@{
        status = "inconclusive"
        expected_model = "Flydigi APEX 4"
        reported_products = @()
        evidence = @()
        apex4_bluetooth_visible = $false
        apex4_usb_or_hid_visible = $false
        xinput_045e_028e_visible = $false
        apex4_xinput_mode_likely = $false
        other_flydigi_model_visible = $false
    }
    try {
        $hid = Get-Content -LiteralPath $HidJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $flydigiDevices = @($hid.devices | Where-Object {
            $_.manufacturer -match "(?i)(flydigi|apex|vader)" -or
            $_.product -match "(?i)(flydigi|apex|vader)" -or
            $_.device_path -match "(?i)vid_(045e&pid_028e|04b4&pid_2412)"
        })
        if (@($flydigiDevices | Where-Object {
            $_.device_path -match "(?i)vid_045e&pid_028e"
        }).Count -gt 0) {
            $result.xinput_045e_028e_visible = $true
        }
        $products = @($flydigiDevices | ForEach-Object { [string]$_.product } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Sort-Object -Unique)
        $evidence = @($flydigiDevices | ForEach-Object {
            "HID {0}:{1} {2}" -f $_.vendor_id_hex, $_.product_id_hex, $_.product
        })

        if ($null -ne $PnpResult -and $PnpResult.available) {
            foreach ($device in @($PnpResult.devices)) {
                $friendly = [string]$device.friendly_name
                $instance = [string]$device.instance_id
                $busDescription = ""
                $busProperty = @($device.properties | Where-Object {
                    $_.key -eq "DEVPKEY_Device_BusReportedDeviceDesc"
                } | Select-Object -First 1)
                if ($busProperty.Count -gt 0) {
                    $busDescription = [string]$busProperty[0].value
                }
                $label = (($friendly, $busDescription | Where-Object {
                    -not [string]::IsNullOrWhiteSpace($_)
                }) -join " / ")
                if ($label -match "(?i)(flydigi|apex|vader|direwolf)" -or
                    $instance -match "(?i)(vid_045e&pid_028e|vid_04b4&pid_2412|vid&0304b4_pid&2412)") {
                    if (-not [string]::IsNullOrWhiteSpace($label)) {
                        $products += $label
                    }
                    $evidence += "PnP $instance [$label]"
                }
                if ($label -match "(?i)APEX[ _-]*4") {
                    if ($instance -match "(?i)^BTH") {
                        $result.apex4_bluetooth_visible = $true
                    }
                    elseif ($instance -match "(?i)^(USB|HID)\\") {
                        $result.apex4_usb_or_hid_visible = $true
                    }
                }
                if ($instance -match "(?i)^(USB|HID)\\VID_04B4&PID_2412") {
                    $result.apex4_usb_or_hid_visible = $true
                }
                if ($instance -match "(?i)^(USB|HID)\\VID_045E&PID_028E") {
                    $result.xinput_045e_028e_visible = $true
                }
            }
        }

        $products = @($products | Sort-Object -Unique)
        $evidence = @($evidence | Sort-Object -Unique)
        $joined = ($products -join " ")
        $hasApex4 = $joined -match "(?i)APEX[ _-]*4"
        $hasOther = $joined -match "(?i)(VADER|DIREWOLF|APEX[ _-]*(2|3|5|6))"
        # APEX 4 firmware in PC/XInput mode can expose the generic Xbox 360
        # identity 045E:028E and the legacy USB product string "Flydigi VADER3".
        # When the APEX 4 is independently visible over BLE, treat that pairing
        # as an XInput-mode candidate rather than claiming that a second pad is
        # definitely present. DInput mode exposes the model-specific 04B4:2412
        # interfaces needed for protocol work.
        $apex4XInputCandidate = (
            $hasApex4 -and
            $result.xinput_045e_028e_visible -and
            -not $result.apex4_usb_or_hid_visible -and
            $joined -notmatch "(?i)(DIREWOLF|APEX[ _-]*(2|3|5|6)|VADER[ _-]*(2|4|5))"
        )
        $result.apex4_xinput_mode_likely = $apex4XInputCandidate
        $result.other_flydigi_model_visible = ($hasOther -and -not $apex4XInputCandidate)
        $result.reported_products = $products
        $result.evidence = $evidence
        if ($apex4XInputCandidate) {
            $result.status = "expected_model_xinput_mode"
        }
        elseif ($hasApex4 -and $hasOther) {
            $result.status = "expected_and_other_models_reported"
        }
        elseif ($hasApex4) {
            $result.status = "expected_model_reported"
        }
        elseif ($hasOther) {
            $result.status = "different_model_reported"
        }
    }
    catch {
        $result.evidence = @("Assessment error: " + $_.Exception.Message)
    }
    return [pscustomobject]$result
}

function Get-MapValue($Map, [string]$InstanceId, [string]$KeyName) {
    if ($Map.ContainsKey($InstanceId) -and $Map[$InstanceId].ContainsKey($KeyName)) {
        return $Map[$InstanceId][$KeyName]
    }
    return $null
}

function Get-PnpDiagnostics([string]$WorkingDirectory) {
    $marker = "(?i)(flydigi|apex[ _-]*4|vid_04b4&pid_2412|vid_045e&pid_028e)"
    $result = [ordered]@{
        available = $false
        seed_count = 0
        related_count = 0
        devices = @()
        error = ""
    }

    try {
        $allDevices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
        $ids = @($allDevices | ForEach-Object { [string]$_.InstanceId })
        $relationKeys = @(
            "DEVPKEY_Device_ContainerId",
            "DEVPKEY_Device_Parent",
            "DEVPKEY_Device_BusReportedDeviceDesc",
            "DEVPKEY_Device_Manufacturer"
        )
        $relations = Get-PnpPropertyMap $ids $relationKeys

        $seeds = @($allDevices | Where-Object {
            $text = @(
                [string]$_.InstanceId,
                [string]$_.FriendlyName,
                [string]$_.Class,
                [string](Get-MapValue $relations ([string]$_.InstanceId) "DEVPKEY_Device_BusReportedDeviceDesc"),
                [string](Get-MapValue $relations ([string]$_.InstanceId) "DEVPKEY_Device_Manufacturer")
            ) -join " `n"
            $text -match $marker
        })

        $relatedIds = @{}
        $seedContainers = @{}
        foreach ($seed in $seeds) {
            $id = [string]$seed.InstanceId
            $relatedIds[$id] = $true
            $container = [string](Get-MapValue $relations $id "DEVPKEY_Device_ContainerId")
            $placeholderContainer = ($container -match "(?i)^\{?0{8}-0{4}-0{4}-(0{4}|f{4})-(0{12}|f{12})\}?$") -or
                ($container -ieq "{9F4B56F0-1DF6-11E0-AC64-0800200C9A66}")
            if (-not [string]::IsNullOrWhiteSpace($container) -and -not $placeholderContainer) {
                $seedContainers[$container] = $true
            }
        }

        # A Windows device container is the narrow, stable boundary joining a
        # controller's USB, HID, XInput, keyboard, and mouse collections. Do not
        # recursively follow parent/child relations: reaching a USB host controller
        # would otherwise pull unrelated hardware into the privacy-sensitive report.
        foreach ($device in $allDevices) {
            $id = [string]$device.InstanceId
            $container = [string](Get-MapValue $relations $id "DEVPKEY_Device_ContainerId")
            if ((-not [string]::IsNullOrWhiteSpace($container)) -and
                $seedContainers.ContainsKey($container)) {
                $relatedIds[$id] = $true
            }
        }

        $related = @($allDevices | Where-Object { $relatedIds.ContainsKey([string]$_.InstanceId) })
        $relatedInstanceIds = @($related | ForEach-Object { [string]$_.InstanceId })
        $allProperties = Get-PnpPropertyMap $relatedInstanceIds @()

        $normalized = foreach ($device in $related) {
            $id = [string]$device.InstanceId
            $properties = @()
            if ($allProperties.ContainsKey($id)) {
                $properties = @($allProperties[$id].GetEnumerator() |
                    Sort-Object Name |
                    ForEach-Object {
                        [pscustomobject][ordered]@{
                            key = [string]$_.Name
                            value = $_.Value
                        }
                    })
            }
            [pscustomobject][ordered]@{
                instance_id = $id
                status = [string]$device.Status
                class = [string]$device.Class
                friendly_name = [string]$device.FriendlyName
                problem = [string]$device.Problem
                container_id = Get-MapValue $relations $id "DEVPKEY_Device_ContainerId"
                parent = Get-MapValue $relations $id "DEVPKEY_Device_Parent"
                properties = $properties
            }
        }

        $result.available = $true
        $result.seed_count = $seeds.Count
        $result.related_count = $related.Count
        $result.devices = @($normalized)
        Write-JsonFile (Join-Path $WorkingDirectory "pnp-devices.json") $result 10

        $text = New-Object System.Text.StringBuilder
        [void]$text.AppendLine("APEX 4 / Flydigi PnP devices")
        [void]$text.AppendLine(("Seeds: {0}; related devices: {1}" -f $seeds.Count, $related.Count))
        foreach ($device in $normalized) {
            [void]$text.AppendLine("")
            [void]$text.AppendLine(("[{0}]" -f $device.friendly_name))
            [void]$text.AppendLine(("  Instance:  {0}" -f $device.instance_id))
            [void]$text.AppendLine(("  Class:     {0}" -f $device.class))
            [void]$text.AppendLine(("  Status:    {0}" -f $device.status))
            [void]$text.AppendLine(("  Container: {0}" -f $device.container_id))
            [void]$text.AppendLine(("  Parent:    {0}" -f $device.parent))
            foreach ($property in $device.properties) {
                $value = if ($property.value -is [System.Array]) {
                    ($property.value -join "; ")
                }
                else {
                    [string]$property.value
                }
                [void]$text.AppendLine(("  {0}: {1}" -f $property.key, $value))
            }
        }
        Write-Utf8File (Join-Path $WorkingDirectory "pnp-devices.txt") $text.ToString()
        return [pscustomobject]$result
    }
    catch {
        $result.error = $_.Exception.Message
        Write-JsonFile (Join-Path $WorkingDirectory "pnp-devices.json") $result
        Write-Utf8File (Join-Path $WorkingDirectory "pnp-devices.txt") `
            ("PnP cmdlet collection failed: " + $_.Exception.Message + [Environment]::NewLine)
        Add-Warning "The detailed PnP cmdlets were unavailable; the collector will use pnputil and registry fallbacks."
        return [pscustomobject]$result
    }
}

function Get-PnpUtilFallback([string]$WorkingDirectory) {
    $path = Join-Path $env:SystemRoot "System32\pnputil.exe"
    $outputPath = Join-Path $WorkingDirectory "pnputil-relevant.txt"
    $errorPath = Join-Path $WorkingDirectory "pnputil-relevant.stderr.txt"
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Utf8File $outputPath "pnputil.exe is unavailable."
        return $false
    }

    try {
        $raw = @(& $path /enum-devices /connected /deviceids /drivers 2> $errorPath |
            ForEach-Object { [string]$_ })
        $blocks = (($raw -join [Environment]::NewLine) -split "(?:\r?\n){2,}")
        $relevant = @($blocks | Where-Object {
            $_ -match "(?i)(flydigi|apex[ _-]*4|vid_04b4&pid_2412|vid_045e&pid_028e)"
        })
        $header = "Only blocks matching Flydigi/APEX 4 or known APEX 4 VID/PID markers are retained."
        $blockSeparator = [Environment]::NewLine + [Environment]::NewLine
        Write-Utf8File $outputPath (($header, "", ($relevant -join $blockSeparator)) -join [Environment]::NewLine)
        return ($relevant.Count -gt 0)
    }
    catch {
        Write-Utf8File $outputPath ("pnputil fallback failed: " + $_.Exception.Message)
        return $false
    }
}

function Get-RegistryFallback([string]$WorkingDirectory) {
    $reg = Join-Path $env:SystemRoot "System32\reg.exe"
    $path = Join-Path $WorkingDirectory "registry-relevant.txt"
    if (-not (Test-Path -LiteralPath $reg)) {
        Write-Utf8File $path "reg.exe is unavailable."
        return
    }

    $queries = @(
        @("HKLM\SYSTEM\CurrentControlSet\Enum\USB", "APEX4"),
        @("HKLM\SYSTEM\CurrentControlSet\Enum\USB", "Flydigi"),
        @("HKLM\SYSTEM\CurrentControlSet\Enum\USB", "VID_04B4&PID_2412"),
        @("HKLM\SYSTEM\CurrentControlSet\Enum\USB", "VID_045E&PID_028E"),
        @("HKLM\SYSTEM\CurrentControlSet\Enum\HID", "APEX4"),
        @("HKLM\SYSTEM\CurrentControlSet\Enum\HID", "Flydigi")
    )
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($query in $queries) {
        $lines.Add(("===== {0} /f {1} =====" -f $query[0], $query[1]))
        try {
            $found = @(& $reg query $query[0] /s /f $query[1] 2>&1 |
                ForEach-Object { [string]$_ })
            foreach ($line in $found) {
                $lines.Add($line)
            }
        }
        catch {
            $lines.Add($_.Exception.Message)
        }
        $lines.Add("")
    }
    Write-Utf8File $path ($lines -join [Environment]::NewLine)
}

function Initialize-XInputProbe {
    if ("Apex4Diagnostics.XInputProbe" -as [type]) {
        return
    }

    $source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace Apex4Diagnostics
{
    public static class XInputProbe
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_GAMEPAD
        {
            public ushort Buttons;
            public byte LeftTrigger;
            public byte RightTrigger;
            public short ThumbLX;
            public short ThumbLY;
            public short ThumbRX;
            public short ThumbRY;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_STATE
        {
            public uint PacketNumber;
            public XINPUT_GAMEPAD Gamepad;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_VIBRATION
        {
            public ushort LeftMotorSpeed;
            public ushort RightMotorSpeed;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_CAPABILITIES
        {
            public byte Type;
            public byte SubType;
            public ushort Flags;
            public XINPUT_GAMEPAD Gamepad;
            public XINPUT_VIBRATION Vibration;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct XINPUT_CAPABILITIES_EX
        {
            public XINPUT_CAPABILITIES Capabilities;
            public ushort VendorId;
            public ushort ProductId;
            public ushort ProductVersion;
            public ushort Unknown1;
            public uint Unknown2;
        }

        [DllImport("xinput1_4.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern uint XInputGetState(uint userIndex, out XINPUT_STATE state);

        [DllImport("xinput1_4.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern uint XInputGetCapabilities(uint userIndex, uint flags, out XINPUT_CAPABILITIES capabilities);

        [DllImport("xinput1_4.dll", EntryPoint = "#108", CallingConvention = CallingConvention.StdCall)]
        private static extern uint XInputGetCapabilitiesEx(uint reserved, uint userIndex, uint flags, out XINPUT_CAPABILITIES_EX capabilities);

        public sealed class DeviceInfo
        {
            public int Slot { get; set; }
            public int SubType { get; set; }
            public int Flags { get; set; }
            public int VendorId { get; set; }
            public int ProductId { get; set; }
            public int ProductVersion { get; set; }
            public bool ExtendedCapabilitiesAvailable { get; set; }
        }

        public sealed class InputSample
        {
            public int Slot { get; set; }
            public long Polls { get; set; }
            public long StateChanges { get; set; }
            public int ButtonsSeen { get; set; }
            public int LeftTriggerMin { get; set; }
            public int LeftTriggerMax { get; set; }
            public int RightTriggerMin { get; set; }
            public int RightTriggerMax { get; set; }
            public int ThumbLXMin { get; set; }
            public int ThumbLXMax { get; set; }
            public int ThumbLYMin { get; set; }
            public int ThumbLYMax { get; set; }
            public int ThumbRXMin { get; set; }
            public int ThumbRXMax { get; set; }
            public int ThumbRYMin { get; set; }
            public int ThumbRYMax { get; set; }
        }

        public static DeviceInfo[] Enumerate()
        {
            List<DeviceInfo> devices = new List<DeviceInfo>();
            for (uint slot = 0; slot < 4; ++slot)
            {
                XINPUT_CAPABILITIES basic;
                if (XInputGetCapabilities(slot, 0, out basic) != 0)
                    continue;

                DeviceInfo info = new DeviceInfo();
                info.Slot = (int)slot;
                info.SubType = basic.SubType;
                info.Flags = basic.Flags;

                try
                {
                    XINPUT_CAPABILITIES_EX extended;
                    uint status = XInputGetCapabilitiesEx(1, slot, 0, out extended);
                    if (status != 0)
                        status = XInputGetCapabilitiesEx(0, slot, 0, out extended);
                    if (status == 0)
                    {
                        info.VendorId = extended.VendorId;
                        info.ProductId = extended.ProductId;
                        info.ProductVersion = extended.ProductVersion;
                        info.ExtendedCapabilitiesAvailable = true;
                    }
                }
                catch (EntryPointNotFoundException) { }
                catch (DllNotFoundException) { }

                devices.Add(info);
            }
            return devices.ToArray();
        }

        private static void InitializeSample(InputSample sample, XINPUT_GAMEPAD pad)
        {
            sample.ButtonsSeen = pad.Buttons;
            sample.LeftTriggerMin = sample.LeftTriggerMax = pad.LeftTrigger;
            sample.RightTriggerMin = sample.RightTriggerMax = pad.RightTrigger;
            sample.ThumbLXMin = sample.ThumbLXMax = pad.ThumbLX;
            sample.ThumbLYMin = sample.ThumbLYMax = pad.ThumbLY;
            sample.ThumbRXMin = sample.ThumbRXMax = pad.ThumbRX;
            sample.ThumbRYMin = sample.ThumbRYMax = pad.ThumbRY;
        }

        private static void UpdateSample(InputSample sample, XINPUT_GAMEPAD pad)
        {
            sample.ButtonsSeen |= pad.Buttons;
            sample.LeftTriggerMin = Math.Min(sample.LeftTriggerMin, pad.LeftTrigger);
            sample.LeftTriggerMax = Math.Max(sample.LeftTriggerMax, pad.LeftTrigger);
            sample.RightTriggerMin = Math.Min(sample.RightTriggerMin, pad.RightTrigger);
            sample.RightTriggerMax = Math.Max(sample.RightTriggerMax, pad.RightTrigger);
            sample.ThumbLXMin = Math.Min(sample.ThumbLXMin, pad.ThumbLX);
            sample.ThumbLXMax = Math.Max(sample.ThumbLXMax, pad.ThumbLX);
            sample.ThumbLYMin = Math.Min(sample.ThumbLYMin, pad.ThumbLY);
            sample.ThumbLYMax = Math.Max(sample.ThumbLYMax, pad.ThumbLY);
            sample.ThumbRXMin = Math.Min(sample.ThumbRXMin, pad.ThumbRX);
            sample.ThumbRXMax = Math.Max(sample.ThumbRXMax, pad.ThumbRX);
            sample.ThumbRYMin = Math.Min(sample.ThumbRYMin, pad.ThumbRY);
            sample.ThumbRYMax = Math.Max(sample.ThumbRYMax, pad.ThumbRY);
        }

        public static InputSample[] Sample(int durationMilliseconds)
        {
            Dictionary<int, InputSample> samples = new Dictionary<int, InputSample>();
            Dictionary<int, uint> lastPackets = new Dictionary<int, uint>();
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(durationMilliseconds);

            while (DateTime.UtcNow < deadline)
            {
                for (uint slot = 0; slot < 4; ++slot)
                {
                    XINPUT_STATE state;
                    if (XInputGetState(slot, out state) != 0)
                        continue;

                    InputSample sample;
                    if (!samples.TryGetValue((int)slot, out sample))
                    {
                        sample = new InputSample();
                        sample.Slot = (int)slot;
                        InitializeSample(sample, state.Gamepad);
                        samples.Add((int)slot, sample);
                        lastPackets.Add((int)slot, state.PacketNumber);
                    }
                    else
                    {
                        if (lastPackets[(int)slot] != state.PacketNumber)
                        {
                            sample.StateChanges++;
                            lastPackets[(int)slot] = state.PacketNumber;
                        }
                        UpdateSample(sample, state.Gamepad);
                    }
                    sample.Polls++;
                }
                Thread.Sleep(4);
            }

            InputSample[] result = new InputSample[samples.Count];
            samples.Values.CopyTo(result, 0);
            Array.Sort(result, delegate(InputSample left, InputSample right) {
                return left.Slot.CompareTo(right.Slot);
            });
            return result;
        }
    }
}
'@

    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Get-ButtonNames([int]$Mask) {
    $buttons = [ordered]@{
        DPadUp = 0x0001
        DPadDown = 0x0002
        DPadLeft = 0x0004
        DPadRight = 0x0008
        Start = 0x0010
        Back = 0x0020
        LeftStick = 0x0040
        RightStick = 0x0080
        LeftShoulder = 0x0100
        RightShoulder = 0x0200
        Guide = 0x0400
        A = 0x1000
        B = 0x2000
        X = 0x4000
        Y = 0x8000
    }
    return @($buttons.GetEnumerator() | Where-Object {
        ($Mask -band [int]$_.Value) -ne 0
    } | ForEach-Object { [string]$_.Name })
}

function Get-XInputDiagnostics([string]$WorkingDirectory, [int]$SampleSeconds, [bool]$SkipSample) {
    $result = [ordered]@{
        available = $false
        devices = @()
        samples = @()
        error = ""
    }
    try {
        Initialize-XInputProbe
        $devices = @([Apex4Diagnostics.XInputProbe]::Enumerate() | ForEach-Object {
            [pscustomobject][ordered]@{
                slot = $_.Slot
                subtype = $_.SubType
                flags = $_.Flags
                vendor_id = $_.VendorId
                vendor_id_hex = ("0x{0:X4}" -f $_.VendorId)
                product_id = $_.ProductId
                product_id_hex = ("0x{0:X4}" -f $_.ProductId)
                product_version = $_.ProductVersion
                extended_capabilities_available = $_.ExtendedCapabilitiesAvailable
            }
        })

        $samples = @()
        if (-not $SkipSample -and $devices.Count -gt 0) {
            Write-Host ""
            Write-Host ("For the next {0} seconds: move both sticks fully, press both triggers, and press several buttons." -f $SampleSeconds) -ForegroundColor Cyan
            Write-Host ("Pendant les {0} prochaines secondes : bougez les deux sticks, pressez les deux gachettes et plusieurs boutons." -f $SampleSeconds) -ForegroundColor Cyan
            $samples = @([Apex4Diagnostics.XInputProbe]::Sample($SampleSeconds * 1000) |
                ForEach-Object {
                    [pscustomobject][ordered]@{
                        slot = $_.Slot
                        polls = $_.Polls
                        state_changes = $_.StateChanges
                        buttons_seen_mask = $_.ButtonsSeen
                        buttons_seen_hex = ("0x{0:X4}" -f $_.ButtonsSeen)
                        buttons_seen = @(Get-ButtonNames $_.ButtonsSeen)
                        left_trigger = [ordered]@{ min = $_.LeftTriggerMin; max = $_.LeftTriggerMax }
                        right_trigger = [ordered]@{ min = $_.RightTriggerMin; max = $_.RightTriggerMax }
                        left_stick_x = [ordered]@{ min = $_.ThumbLXMin; max = $_.ThumbLXMax }
                        left_stick_y = [ordered]@{ min = $_.ThumbLYMin; max = $_.ThumbLYMax }
                        right_stick_x = [ordered]@{ min = $_.ThumbRXMin; max = $_.ThumbRXMax }
                        right_stick_y = [ordered]@{ min = $_.ThumbRYMin; max = $_.ThumbRYMax }
                    }
                })
            Write-Host "XInput sample complete. / Echantillonnage XInput termine." -ForegroundColor Green
        }

        $result.available = $true
        $result.devices = $devices
        $result.samples = $samples
        Write-JsonFile (Join-Path $WorkingDirectory "xinput.json") $result 8
        return [pscustomobject]$result
    }
    catch {
        $result.error = $_.Exception.Message
        Write-JsonFile (Join-Path $WorkingDirectory "xinput.json") $result
        Add-Warning ("XInput probing failed: " + $_.Exception.Message)
        return [pscustomobject]$result
    }
}

function Get-ProcessDiagnostics([string]$WorkingDirectory) {
    $pattern = "(?i)(flydigi|space.?station|steam|dsx|dualsensex|rewasd|hidhide|apexsense|viiper)"
    try {
        $processes = @(Get-Process -ErrorAction Stop |
            Where-Object { $_.ProcessName -match $pattern } |
            Sort-Object ProcessName |
            Select-Object ProcessName, Id)
        Write-JsonFile (Join-Path $WorkingDirectory "possibly-conflicting-processes.json") $processes
        return @($processes | ForEach-Object { [string]$_.ProcessName })
    }
    catch {
        Write-JsonFile (Join-Path $WorkingDirectory "possibly-conflicting-processes.json") @()
        return @()
    }
}

function Get-ServiceDiagnostics([string]$WorkingDirectory) {
    $pattern = "(?i)(hidhide|usbip|vigem|vhf|flydigi|apexsense)"
    try {
        $services = @(Get-Service -ErrorAction Stop |
            Where-Object { $_.Name -match $pattern -or $_.DisplayName -match $pattern } |
            Sort-Object Name |
            ForEach-Object {
                [pscustomobject][ordered]@{
                    name = $_.Name
                    display_name = $_.DisplayName
                    status = [string]$_.Status
                    start_type = [string]$_.StartType
                }
            })
        Write-JsonFile (Join-Path $WorkingDirectory "relevant-services.json") $services
    }
    catch {
        Write-JsonFile (Join-Path $WorkingDirectory "relevant-services.json") @()
    }
}

function Compress-DiagnosticDirectory([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) {
        throw "Refusing to overwrite an existing archive: $Destination"
    }
    if (Get-Command Compress-Archive -ErrorAction SilentlyContinue) {
        Compress-Archive -Path (Join-Path $Source "*") -DestinationPath $Destination `
            -CompressionLevel Optimal -ErrorAction Stop
        return
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $Source,
        $Destination,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)
}

Write-Host "=== Flydigi APEX 4 diagnostic collector ===" -ForegroundColor Cyan
Write-Host "Read-only collection: no driver install, no controller setting change, no effect/output report." -ForegroundColor DarkGray

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = [System.IO.Path]::GetTempPath()
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$shortId = [Guid]::NewGuid().ToString("N").Substring(0, 8)
$workingDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("Apex4-Diagnostics-{0}-{1}" -f $timestamp, $shortId)
$archivePath = Join-Path $OutputDirectory ("Apex4-Diagnostics-{0}-{1}.zip" -f $timestamp, $shortId)
New-Item -ItemType Directory -Path $workingDirectory -ErrorAction Stop | Out-Null

$completed = $false
try {
    Write-Host "[1/7] Collecting system information..."
    $windows = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" `
        -ErrorAction SilentlyContinue
    $system = [ordered]@{
        collector_version = $script:CollectorVersion
        collected_at_utc = (Get-Date).ToUniversalTime().ToString("o")
        windows_product_name = [string]$windows.ProductName
        windows_display_version = [string]$windows.DisplayVersion
        windows_build = [string]$windows.CurrentBuildNumber
        windows_ubr = [string]$windows.UBR
        operating_system_64_bit = [Environment]::Is64BitOperatingSystem
        process_64_bit = [Environment]::Is64BitProcess
        powershell_version = $PSVersionTable.PSVersion.ToString()
        culture = [Globalization.CultureInfo]::CurrentCulture.Name
        administrator = Test-IsAdministrator
    }
    Write-JsonFile (Join-Path $workingDirectory "system.json") $system
    Add-Step "system" $true "Windows and PowerShell versions collected."

    Write-Host "[2/7] Enumerating HID interfaces..."
    $bridge = Resolve-BridgeExecutable $BridgeExecutable
    $bridgeInfo = [ordered]@{
        available = ($null -ne $bridge)
        file_name = ""
        file_version = ""
        product_version = ""
        diagnostic_exit_code = $null
    }
    if ($null -ne $bridge) {
        $file = Get-Item -LiteralPath $bridge
        $bridgeInfo.file_name = $file.Name
        $bridgeInfo.file_version = [string]$file.VersionInfo.FileVersion
        $bridgeInfo.product_version = [string]$file.VersionInfo.ProductVersion
        $bridgeInfo.diagnostic_exit_code = Invoke-NativeCapture $bridge `
            @("diagnose", "--json") `
            (Join-Path $workingDirectory "hid-relevant.json") `
            (Join-Path $workingDirectory "hid-relevant.stderr.txt")
        if ($bridgeInfo.diagnostic_exit_code -eq 0) {
            Add-Step "hid" $true "Relevant HID interfaces collected with ApexSenseBridge.exe."
        }
        else {
            Add-Warning ("ApexSenseBridge HID diagnostics exited with code " + $bridgeInfo.diagnostic_exit_code + ".")
            Add-Step "hid" $false "ApexSenseBridge.exe returned a non-zero exit code."
        }
    }
    else {
        Write-Utf8File (Join-Path $workingDirectory "hid-relevant.json") `
            "{`n  `"count`": 0,`n  `"devices`": []`n}`n"
        Write-Utf8File (Join-Path $workingDirectory "hid-relevant.stderr.txt") `
            "ApexSenseBridge.exe was not found. Place it next to this script and run again for HID report lengths and usage data.`n"
        Add-Warning "ApexSenseBridge.exe was not found; HID report lengths and usage data may be missing."
        Add-Step "hid" $false "ApexSenseBridge.exe unavailable."
    }
    Write-JsonFile (Join-Path $workingDirectory "bridge.json") $bridgeInfo

    Write-Host "[3/7] Collecting Plug and Play topology and driver properties..."
    $pnp = Get-PnpDiagnostics $workingDirectory
    $pnpFallbackFound = Get-PnpUtilFallback $workingDirectory
    Get-RegistryFallback $workingDirectory
    if ($pnp.available) {
        Add-Step "pnp" $true ("{0} matching seeds and {1} related devices collected." -f $pnp.seed_count, $pnp.related_count)
    }
    elseif ($pnpFallbackFound) {
        Add-Step "pnp" $true "pnputil fallback found matching devices."
    }
    else {
        Add-Step "pnp" $false "No matching PnP device was found."
    }

    $modelAssessment = Get-ConnectedModelAssessment `
        (Join-Path $workingDirectory "hid-relevant.json") $pnp
    Write-JsonFile (Join-Path $workingDirectory "model-assessment.json") $modelAssessment
    if ($modelAssessment.status -eq "expected_model_xinput_mode") {
        Add-Warning "The APEX 4 is likely connected in XInput mode, where it can report the generic 045E:028E / Flydigi VADER3 identity. Switch the controller to DInput (hold FN + A for about 3 seconds, or use its LCD connection menu) and collect again."
    }
    elseif ($modelAssessment.status -eq "expected_and_other_models_reported") {
        Add-Warning "An APEX 4 and another Flydigi model are both present. Disconnect every other Flydigi controller/receiver and collect again."
    }
    elseif ($modelAssessment.status -eq "different_model_reported") {
        $reported = if ($modelAssessment.reported_products.Count -gt 0) {
            $modelAssessment.reported_products -join ", "
        }
        else {
            "another Flydigi model"
        }
        Add-Warning ("The connected controller reports itself as '" + $reported +
            "', not as an APEX 4. Connect the intended APEX 4 and collect again.")
    }
    elseif ($modelAssessment.status -eq "inconclusive") {
        Add-Warning "No connected interface explicitly reported the APEX 4 model name; verify the controller before sharing this report."
    }
    if ($modelAssessment.apex4_bluetooth_visible -and
        -not $modelAssessment.apex4_usb_or_hid_visible -and
        -not $modelAssessment.apex4_xinput_mode_likely) {
        Add-Warning "The APEX 4 is visible only through Bluetooth. For protocol work, reconnect it by USB cable or its own 2.4 GHz receiver and collect again."
    }

    Write-Host "[4/7] Enumerating and sampling XInput in read-only mode..."
    $xinput = Get-XInputDiagnostics $workingDirectory $InputSampleSeconds $SkipInputSample.IsPresent
    if ($xinput.available -and $xinput.devices.Count -gt 0) {
        Add-Step "xinput" $true ("{0} connected XInput slot(s) found." -f $xinput.devices.Count)
    }
    elseif ($xinput.available) {
        Add-Warning "No connected XInput controller was visible during collection."
        Add-Step "xinput" $false "No connected XInput slot found."
    }
    else {
        Add-Step "xinput" $false "XInput probe unavailable."
    }

    Write-Host "[5/7] Checking software that can claim or hide the controller..."
    $processNames = @(Get-ProcessDiagnostics $workingDirectory)
    Get-ServiceDiagnostics $workingDirectory
    $conflicts = @($processNames | Where-Object {
        $_ -match "(?i)(flydigi|space.?station|steam|dsx|dualsensex|rewasd)"
    })
    if ($conflicts.Count -gt 0) {
        Add-Warning ("Potential controller-claiming software was running: " + (($conflicts | Sort-Object -Unique) -join ", ") + ". Repeat with it closed if detection is incomplete.")
    }
    Add-Step "software" $true "Relevant process names and bridge driver services collected."

    Write-Host "[6/7] Building the report..."
    $warningsForReport = $script:Warnings.ToArray()
    $stepsForReport = $script:Steps.ToArray()
    $summary = [ordered]@{
        collector_version = $script:CollectorVersion
        collected_at_utc = $system.collected_at_utc
        bridge = $bridgeInfo
        model_assessment = $modelAssessment
        pnp_available = $pnp.available
        pnp_seed_count = $pnp.seed_count
        pnp_related_count = $pnp.related_count
        pnputil_match_found = $pnpFallbackFound
        xinput_available = $xinput.available
        xinput_device_count = $xinput.devices.Count
        warnings = $warningsForReport
        steps = $stepsForReport
    }
    Write-JsonFile (Join-Path $workingDirectory "summary.json") $summary 8

    $warningText = if ($warningsForReport.Count -eq 0) {
        "None."
    }
    else {
        ($warningsForReport | ForEach-Object { "- " + $_ }) -join [Environment]::NewLine
    }

    $readme = @"
Flydigi APEX 4 diagnostic report
================================

Collector version: $($script:CollectorVersion)
Collection time (UTC): $($system.collected_at_utc)

Safety
------
This collector is read-only. It did not install a driver, change a controller
setting, enable/disable a device, write a vendor HID report, or activate motors
or adaptive triggers.

Contents
--------
- summary.json: collection status and warnings
- system.json: Windows/PowerShell versions (no computer or user name)
- hid-relevant.json: relevant HID paths, usage pages, report lengths, and topology
- model-assessment.json: expected-model and connection-mode check based on HID/PnP interfaces
- pnp-devices.json/txt: matching PnP devices, container relationships, and drivers
- pnputil-relevant.txt: filtered Windows PnP fallback
- registry-relevant.txt: filtered APEX 4/Flydigi enumeration fallback
- xinput.json: XInput slots plus min/max input ranges and button mask seen
- possibly-conflicting-processes.json: process names only, no paths or command lines
- relevant-services.json: HidHide/USBip/virtual-controller service state

Privacy
-------
The report deliberately omits the Windows user name, computer name, IP address,
files, game library, full process list, and command lines. Hardware instance IDs,
container IDs, HID paths, and controller serial fields can be unique; they are
retained because they are needed to correlate the APEX 4 interfaces. Inspect the
text/JSON files before sharing if this is a concern.

Warnings
--------
$warningText
"@
    Write-Utf8File (Join-Path $workingDirectory "README.txt") ($readme + [Environment]::NewLine)

    Write-Host "[7/7] Creating ZIP archive..."
    Compress-DiagnosticDirectory $workingDirectory $archivePath
    Add-Step "archive" $true "ZIP archive created."
    $completed = $true

    if ($KeepWorkingDirectory) {
        $keptPath = [System.IO.Path]::ChangeExtension($archivePath, $null)
        if (Test-Path -LiteralPath $keptPath) {
            $keptPath = $keptPath + "-files"
        }
        Move-Item -LiteralPath $workingDirectory -Destination $keptPath -ErrorAction Stop
        Write-Host ("Uncompressed files kept at: " + $keptPath) -ForegroundColor DarkGray
    }
    else {
        Remove-Item -LiteralPath $workingDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host ""
    Write-Host "Diagnostic archive created successfully:" -ForegroundColor Green
    Write-Host $archivePath -ForegroundColor Green
    Write-Host "Send this ZIP to the ApexSenseBridge developer after reviewing README.txt."
    Write-Host "Envoyez ce ZIP au developpeur d'ApexSenseBridge apres avoir consulte README.txt."
}
catch {
    Write-Host ""
    Write-Host ("Collection failed: " + $_.Exception.Message) -ForegroundColor Red
    Write-Host ("Partial files were kept at: " + $workingDirectory) -ForegroundColor Yellow
    exit 1
}

if ($completed) {
    exit 0
}
exit 1
