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
function Call([string]$name, [object[]]$arguments = @()) { $windowType.GetMethod($name, $flags).Invoke($window, $arguments) }
function Get-Names($items) { @($items | ForEach-Object { $_.Name }) -join "," }
try {
    # The window is never shown: OnEffectSettingChanged ignores changes until IsLoaded,
    # so slider moves below never write user settings.
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
        (New-Object ApexSenseBridgeTray.Services.CloudGameListService), $settings, $null, $manager, $null, $null, "settings")
    $windowType.GetField("controllerDetection", $flags).GetValue($window).Dispose()
    $windowType.GetField("gamepadNav", $flags).GetValue($window).Stop()
    $count = 0

    # Only the identified model's capabilities are reachable with the D-pad.
    $expected = @{
        "apex4" = "RowTriggerStrength,RowVibrationStrength,RowGyroStrength,RowGyroYawStrength,RowVibrationThreshold,RowGripVibrations"
        "apex5" = "TileSettingSyncLightbar,RowTriggerStrength,RowVibrationStrength,RowVibrationThreshold,RowGripVibrations"
        "apex6" = "RowTriggerStrength,RowVibrationStrength,RowGripVibrations"
        "disconnected" = ""
    }
    foreach ($status in @("apex4", "apex5", "apex6", "disconnected")) {
        Call "UpdateControllerStatus" @($status)
        $names = Get-Names (Call "GetSettingsCol1Items" @($true))
        if ($names -ne $expected[$status]) { throw "Controller column for ${status}: $names" }
        $count++
    }

    # Without a controller the focus falls back to the general column.
    Call "SetSettingsNav" @([int]1, [int]0)
    if ($windowType.GetField("settingsCol", $flags).GetValue($window) -ne 0) { throw "Focus stayed on an empty column" }
    $count++

    # D-pad left/right adjusts the focused slider by one tick, within bounds.
    Call "UpdateControllerStatus" @("apex5")
    $slider = Get-Control "SliderTriggerStrength"
    $row = [Array]::IndexOf((Call "GetSettingsCol1Items" @($true)), (Get-Control "RowTriggerStrength"))
    Call "SetSettingsNav" @([int]1, [int]$row)
    $start = $slider.Value
    Call "OnGamepadLeft"
    if ($slider.Value -ne [Math]::Max(0, $start - 5)) { throw "Left did not lower the slider" }
    Call "OnGamepadRight"; Call "OnGamepadRight"; Call "OnGamepadRight"
    if ($slider.Value -ne 100) { throw "Right did not raise/clamp the slider: $($slider.Value)" }
    if ($windowType.GetField("settingsCol", $flags).GetValue($window) -ne 1) { throw "Slider adjustment switched column" }
    $count++

    # Left on a non-slider row still moves between columns.
    $row = [Array]::IndexOf((Call "GetSettingsCol1Items" @($true)), (Get-Control "RowGripVibrations"))
    Call "SetSettingsNav" @([int]1, [int]$row)
    Call "OnGamepadLeft"
    if ($windowType.GetField("settingsCol", $flags).GetValue($window) -ne 0) { throw "Left did not leave the controller column" }
    $count++

    # Disabling detection removes its criteria from the D-pad path.
    $settings.AutoDetectGames = $false
    Call "UpdateSettingsView"
    $names = Get-Names (Call "GetSettingsCol0Items" @($true))
    if ($names -match "TileSettingAdaptive|TileSettingHaptic") { throw "Disabled criteria are navigable: $names" }
    if ($names -notmatch "BtnOpenControllerTest" -or $names -notmatch "BtnCheckUpdates") { throw "Missing general actions: $names" }
    $count++

    # Home row 0: action tiles, replaced by recovery buttons when a session awaits a decision.
    if ((Get-Names (Call "GetDashboardActionItems" @($true))) -ne "") { throw "Hidden home tab is navigable" }
    Call "SetCurrentTab" @([int]0)
    $names = Get-Names (Call "GetDashboardActionItems" @($true))
    if ($names -ne "TileManualBridge,TileJumpGames,TileJumpLearned") { throw "Home actions: $names" }
    (Get-Control "PnlSessionRecovery").Visibility = "Visible"
    $names = Get-Names (Call "GetDashboardActionItems" @($true))
    if ($names -ne "BtnResumeSession,BtnDismissRecovery") { throw "Recovery actions: $names" }
    (Get-Control "PnlSessionRecovery").Visibility = "Collapsed"
    $count++

    # View cycles the certified-games filter.
    Call "CycleGameFilter"
    if ((Get-Control "RadAdaptive").IsChecked -ne $true) { throw "View did not advance the filter" }
    Call "CycleGameFilter"; Call "CycleGameFilter"; Call "CycleGameFilter"
    if ((Get-Control "RadAll").IsChecked -ne $true) { throw "Filter cycle did not wrap" }
    $count++

    Write-Output "PASS: $count gamepad navigation scenarios; no user settings written."
} finally {
    if ($window) { $window.Close() }
    ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
    $app.Shutdown()
}
