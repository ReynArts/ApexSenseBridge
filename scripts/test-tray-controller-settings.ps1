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
$service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
$window = $null
function Get-Control([string]$name) { $windowType.GetField($name, $flags).GetValue($window) }
try {
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
        $service, $settings, $null, $manager, $null, $null, "settings")
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
        if ((Get-Control "TileSettingZoneEmulation").IsEnabled -ne ($status -eq "apex5")) { throw "Trigger emulation unlocked for $status" }
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
    [string]$catalog = (@{ games = @([ordered]@{ title = "Profile Test"; normalized = "profiletest"; adaptiveTriggers = $true; hapticFeedback = $true }) } | ConvertTo-Json -Depth 4 -Compress)
    if (-not [ApexSenseBridgeTray.Services.CloudGameListService].GetMethod("ParseAndLoadJson", $flags).Invoke($service, [object[]]@($catalog))) { throw "Catalogue rejected" }
    $windowType.GetMethod("LoadGames", $flags).Invoke($window, @())
    $windowType.GetMethod("SetCurrentTab", $flags).Invoke($window, @([int]1))
    $windowType.GetMethod("SelectGame", $flags).Invoke($window, @([int]0))
    $game = $windowType.GetProperty("SelectedGame").GetValue($window)
    if (-not $game) { throw "No certified game is selectable" }
    $settings.SetApexProfileSlot($game.Normalized, 2)
    $game.SelectedApexProfileSlot = 2
    $windowType.GetMethod("UpdateDetailPane", $flags).Invoke($window, @())
    if ((Get-Control "TxtDetailManualFixNote").Visibility -eq "Visible") { throw "Manual fix note is shown for a game without a manual fix" }
    $lockedText = [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_ApexProfileApex5Only")
    $hintText = [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_ApexProfileHint")
    if ($lockedText -eq "Loc_ApexProfileApex5Only" -or $lockedText -eq $hintText) { throw "APEX 5 only hint is not localized" }
    foreach ($status in @("apex4", "apex6", "unsupported", "apex5", "disconnected", "unavailable", "apex4", "apex5")) {
        $statusMethod.Invoke($window, @($status))
        $locked = $status -in @("apex4", "apex6", "unsupported")
        foreach ($button in @("BtnProfileLeft", "BtnProfileRight")) {
            if ((Get-Control $button).IsEnabled -eq $locked) { throw "Profile selector $button has the wrong state for $status" }
        }
        $hint = Get-Control "TxtApexProfileLocked"
        if (($hint.Visibility -eq "Visible") -ne $locked) { throw "APEX 5 only hint has the wrong visibility for $status" }
        if ($locked -and $hint.Text -ne $lockedText) { throw "APEX 5 only hint text is wrong for $status" }
        $expectedTip = if ($locked) { $lockedText } else { $hintText }
        if ((Get-Control "PnlApexProfile").ToolTip -ne $expectedTip) { throw "Profile selector tooltip is wrong for $status" }
        if ((Get-Control "TxtDetailApexProfile").Text -ne "Profile 2") { throw "Saved profile is not shown for $status" }
        if ($locked) {
            foreach ($name in @("OnGamepadLeft", "OnGamepadRight", "ActivateCurrentItem")) {
                $windowType.GetMethod($name, $flags).Invoke($window, @())
            }
            foreach ($name in @("OnCycleProfileLeftClick", "OnCycleProfileRightClick")) {
                $windowType.GetMethod($name, $flags).Invoke($window, @($null, $null))
            }
            if ($game.SelectedApexProfileSlot -ne 2 -or $settings.GetApexProfileSlot($game.Normalized) -ne 2) {
                throw "Locked profile selector changed the saved profile for $status"
            }
        }
        $count++
    }
    [string]$fixCatalog = (@{ games = @([ordered]@{ title = "Manual Fix Test"; normalized = "manualfixtest"; adaptiveTriggers = $true; adaptiveTriggersManualFix = $true }) } | ConvertTo-Json -Depth 4 -Compress)
    if (-not [ApexSenseBridgeTray.Services.CloudGameListService].GetMethod("ParseAndLoadJson", $flags).Invoke($service, [object[]]@($fixCatalog))) { throw "Manual fix catalogue rejected" }
    $windowType.GetMethod("LoadGames", $flags).Invoke($window, @())
    $windowType.GetMethod("SelectGame", $flags).Invoke($window, @([int]0))
    $fixGame = $windowType.GetProperty("SelectedGame").GetValue($window)
    if (-not $fixGame -or -not $fixGame.RequiresManualFix) { throw "No manual fix game is selectable" }
    $statusMethod.Invoke($window, @("apex4"))
    $fixGame.IsExcluded = $true
    $windowType.GetMethod("UpdateDetailPane", $flags).Invoke($window, @())
    if ((Get-Control "BtnDetailToggleManualFix").IsEnabled) { throw "Manual fix toggle is enabled on an excluded game" }
    $windowType.GetMethod("ActivateCurrentItem", $flags).Invoke($window, @())
    if ($fixGame.IsManualFixAllowed -or $settings.IsManualFixAllowed($fixGame.Normalized)) { throw "Select toggled the manual fix of an excluded game" }
    $count++
    $fixGame.IsExcluded = $false
    $windowType.GetMethod("UpdateDetailPane", $flags).Invoke($window, @())
    if (-not (Get-Control "BtnDetailToggleManualFix").IsEnabled) { throw "Manual fix toggle is disabled on an included game" }
    $count++
    $noteKey = "Loc_ManualFixNote"
    $englishNote = $null
    foreach ($language in [ApexSenseBridgeTray.Common.LocalizationManager]::SupportedLanguages) {
        [ApexSenseBridgeTray.Common.LocalizationManager]::SetLanguage($language)
        $windowType.GetMethod("UpdateDetailPane", $flags).Invoke($window, @())
        $note = Get-Control "TxtDetailManualFixNote"
        $noteText = [ApexSenseBridgeTray.Common.LocalizationManager]::Get($noteKey)
        if ($noteText -eq $noteKey -or [string]::IsNullOrWhiteSpace($noteText)) { throw "Manual fix note is missing for $language" }
        if ($language -eq "en") { $englishNote = $noteText } elseif ($noteText -eq $englishNote) { throw "Manual fix note is not translated for $language" }
        if ($note.Visibility -ne "Visible" -or $note.Text -ne $noteText) { throw "Manual fix note is not shown for $language" }
        $note.Measure((New-Object Windows.Size(320, [double]::PositiveInfinity)))
        if ($note.DesiredSize.Width -gt 320 -or $note.DesiredSize.Height -le 0) { throw "Manual fix note overflows for $language" }
        $count++
    }
    [ApexSenseBridgeTray.Common.LocalizationManager]::SetLanguage("en")
    Write-Output "PASS: $count controller calibration/hot-swap UI scenarios; no user settings written."
} finally {
    if ($window) { $window.Close() }
    ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
    $app.Shutdown()
}
