param(
    [string]$TrayAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridgeTray.exe"),
    [ValidateSet("", "main", "language", "diagnostic", "bugreport")]
    [string]$Window = ""
)

# Shows each Tray window off-screen (one process per window) and fails on any dispatcher exception.
$ErrorActionPreference = "Stop"
$TrayAssembly = (Resolve-Path -LiteralPath $TrayAssembly).Path
if (-not $Window) {
    foreach ($name in "main", "language", "diagnostic", "bugreport") {
        & powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File $PSCommandPath -TrayAssembly $TrayAssembly -Window $name
        if ($LASTEXITCODE -ne 0) { exit 1 }
    }
    exit 0
}
if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") {
    throw "Run this WPF regression test with Windows PowerShell -STA."
}
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
[void][Reflection.Assembly]::LoadFrom($TrayAssembly)
$app = New-Object ApexSenseBridgeTray.App
$app.InitializeComponent()
$errors = New-Object System.Collections.ArrayList
$app.add_DispatcherUnhandledException({ param($s, $e) [void]$errors.Add($e.Exception.ToString()); $e.Handled = $true })
[ApexSenseBridgeTray.Common.LocalizationManager]::Initialize("en")
[ApexSenseBridgeTray.Common.ThemeManager]::Initialize()
$flags = [Reflection.BindingFlags]"Instance,NonPublic"

function Pump([int]$frames = 10) {
    for ($k = 0; $k -lt $frames; $k++) {
        $frame = New-Object Windows.Threading.DispatcherFrame
        [void][Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke(
            [Windows.Threading.DispatcherPriority]::Background, [Action]{ $frame.Continue = $false })
        [Windows.Threading.Dispatcher]::PushFrame($frame)
    }
}
function Show-OffScreen($target) {
    $target.WindowStartupLocation = "Manual"; $target.Left = -5000; $target.Top = 0; $target.ShowActivated = $false
    $target.Show(); Pump 15
}

# Never show the support callout: it saves the real user settings.
$settings = New-Object ApexSenseBridgeTray.Models.TraySettings
$settings.SupportHintShown = $true
$manager = $null
$target = $null
$scenarios = 0
try {
    if ($Window -eq "main") {
        $windowType = [ApexSenseBridgeTray.GameListWindow]
        $manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
        $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
        [ApexSenseBridgeTray.Services.CloudGameListService].GetMethod("LoadEmbeddedDatabase", $flags).Invoke($service, $null)
        $target = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @($service, $settings, $null, $manager, $null, $null, "dashboard")
        $windowType.GetField("controllerDetection", $flags).GetValue($target).Dispose()
        Show-OffScreen $target
        $windowType.GetField("gamepadNav", $flags).GetValue($target).Stop()
        $windowType.GetField("isGamepadMode", $flags).SetValue($target, $true)
        foreach ($status in @("apex4", "apex5", "apex6", "disconnected")) {
            $windowType.GetMethod("UpdateControllerStatus", $flags).Invoke($target, @($status))
            foreach ($tab in 1, 0, 3, 2, 0) {
                $windowType.GetMethod("SetCurrentTab", $flags).Invoke($target, @([int]$tab))
                if ($tab -eq 0) { $windowType.GetMethod("SetDashboardNav", $flags).Invoke($target, @([int]1, [int]2)) }
                if ($tab -eq 1) { $windowType.GetMethod("SelectGame", $flags).Invoke($target, @([int]3)) }
                if ($tab -eq 3) { $windowType.GetMethod("SetSettingsNav", $flags).Invoke($target, @([int]1, [int]1)) }
                Pump
            }
            $scenarios++
        }
    } elseif ($Window -eq "language") {
        $target = New-Object ApexSenseBridgeTray.LanguagePickerWindow -ArgumentList @("fr")
        Show-OffScreen $target
        $move = [ApexSenseBridgeTray.LanguagePickerWindow].GetMethod("Move", $flags)
        foreach ($step in 1, 1, 1, 1, 1, 1, 1, 1, -8) { $move.Invoke($target, @([int]$step)); Pump 3 }
        $scenarios++
    } elseif ($Window -eq "bugreport") {
        $target = New-Object ApexSenseBridgeTray.BugReportWindow -ArgumentList @($settings, $null, "apex5")
        Show-OffScreen $target
        $target.FindName("TxtReportTitle").Text = "Test"; $target.FindName("ChkConsent").IsChecked = $true; Pump
        $scenarios++
    } else {
        $target = New-Object ApexSenseBridgeTray.ControllerTestWindow -ArgumentList @($settings)
        Show-OffScreen $target
        foreach ($tab in "TabVibration", "TabRgb", "TabGyro", "TabLatency", "TabMapping", "TabTriggers") {
            [ApexSenseBridgeTray.ControllerTestWindow].GetField($tab, $flags).GetValue($target).IsChecked = $true
            Pump 5
            $scenarios++
        }
    }
    Pump
    if ($errors.Count -gt 0) { throw "Dispatcher exceptions while rendering the $Window window:`n" + ($errors -join "`n----`n") }
    Write-Output "PASS: $Window window shown ($scenarios scenarios) without dispatcher exceptions."
} finally {
    if ($target) { $target.Close() }
    if ($manager) { ([Threading.Timer][ApexSenseBridgeTray.Services.EngineSessionManager].GetField("healthTimer", $flags).GetValue($manager)).Dispose() }
    $app.Shutdown()
}
