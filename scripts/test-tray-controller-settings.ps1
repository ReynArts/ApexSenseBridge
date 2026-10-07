param(
    [string]$TrayAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridgeTray.exe")
)

$ErrorActionPreference = "Stop"
if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") {
    throw "Run this WPF regression test with Windows PowerShell -STA."
}
$TrayAssembly = (Resolve-Path -LiteralPath $TrayAssembly).Path
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
[void][Reflection.Assembly]::LoadFrom($TrayAssembly)
$app = New-Object ApexSenseBridgeTray.App
$app.InitializeComponent()
[ApexSenseBridgeTray.Common.LocalizationManager]::Initialize("en")
[ApexSenseBridgeTray.Common.ThemeManager]::Initialize()
$flags = [Reflection.BindingFlags]"Instance,NonPublic"
$windowType = [ApexSenseBridgeTray.GameListWindow]
$managerType = [ApexSenseBridgeTray.Services.EngineSessionManager]
$manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
$settings = New-Object ApexSenseBridgeTray.Models.TraySettings
$window = $null
function Get-Control([string]$name) { $windowType.GetField($name, $flags).GetValue($window) }
try {
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
        (New-Object ApexSenseBridgeTray.Services.CloudGameListService), $settings, $null, $manager, $null, $null, "settings")
    # No bridge, no real hotplug polling, no UI shown, no saved user settings.
    $windowType.GetField("controllerDetection", $flags).GetValue($window).Dispose()
    $statusMethod = $windowType.GetMethod("UpdateControllerStatus", $flags)
    $count = 0
    foreach ($status in @("disconnected", "unavailable", "unsupported", "apex4", "apex5", "apex6", "disconnected")) {
        $statusMethod.Invoke($window, @($status))
        $connected = $status -in @("apex4", "apex5", "apex6")
        $expectedMaximum = if ($status -in @("apex4", "apex5")) { 200 } else { 100 }
        if ((Get-Control "SliderVibrationStrength").Maximum -ne $expectedMaximum) { throw "Wrong vibration gain range for $status" }
        foreach ($control in @("SliderTriggerStrength", "SliderVibrationStrength", "ChkGripVibrations")) {
            if ((Get-Control $control).IsEnabled -ne $connected) { throw "$status enabled incorrect control: $control" }
        }
        foreach ($control in @("SliderApex4GyroStrength", "SliderApex4GyroYawStrength")) {
            if ((Get-Control $control).IsEnabled -ne ($status -eq "apex4")) { throw "Gyro tuning unlocked for $status" }
        }
        if ((Get-Control "TileSettingSyncLightbar").IsEnabled -ne ($status -eq "apex5")) { throw "RGB unlocked for $status" }
        if ((Get-Control "SliderVibrationThreshold").IsEnabled -ne ($connected -and $status -ne "apex6")) {
            throw "Incorrect threshold availability for $status"
        }
        if ($status -eq "apex6" -and (Get-Control "SliderVibrationThreshold").Value -ne 0) { throw "APEX 6 threshold is not zero" }
        if (-not $connected) {
            # Explicit handler invocation must also refuse offline changes.
            $before = $settings.GetControllerCalibration("apex5").SyncLightbar
            $windowType.GetMethod("OnSettingSyncLightbarToggled", $flags).Invoke($window, @($null, $null))
            $windowType.GetMethod("OnGripVibrationsChanged", $flags).Invoke($window, @($null, $null))
            if ($settings.GetControllerCalibration("apex5").SyncLightbar -ne $before) { throw "Offline handler changed RGB" }
        }
        if (-not (Get-Control "TileSettingNotifications").IsEnabled) { throw "General preference locked offline" }
        $count++
    }
    $settings.GetControllerCalibration("apex5").TriggerStrengthPercent = 35
    $settings.GetControllerCalibration("apex6").TriggerStrengthPercent = 80
    $settings.GetControllerCalibration("apex5").VibrationStrengthPercent = 175
    $settings.GetControllerCalibration("apex6").VibrationStrengthPercent = 200
    foreach ($model in @("apex5", "apex6", "apex5")) {
        $statusMethod.Invoke($window, @($model))
        $expected = if ($model -eq "apex5") { 35 } else { 80 }
        if ((Get-Control "SliderTriggerStrength").Value -ne $expected) { throw "UI did not restore $model calibration" }
        $expectedVibration = if ($model -eq "apex5") { 175 } else { 100 }
        if ((Get-Control "SliderVibrationStrength").Value -ne $expectedVibration) { throw "UI lost the independent gain for $model" }
        $count++
    }
    Write-Output "PASS: $count controller calibration/hot-swap UI scenarios; no user settings written."
} finally {
    if ($window) { $window.Close() }
    ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
    $app.Shutdown()
}
