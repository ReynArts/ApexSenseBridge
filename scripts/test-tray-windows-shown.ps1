param(
    [string]$TrayAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridgeTray.exe")
)

# Shows the Tray windows off-screen and fails on any dispatcher exception. The other WPF
# tests build windows without showing them, so template/trigger errors only raised at
# render time (e.g. animating a frozen transform) went unnoticed and blocked the UI.
$ErrorActionPreference = "Stop"
if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") {
    throw "Run this WPF regression test with Windows PowerShell -STA."
}
$TrayAssembly = (Resolve-Path -LiteralPath $TrayAssembly).Path
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
[void][Reflection.Assembly]::LoadFrom($TrayAssembly)
$app = New-Object System.Windows.Application
$app.ShutdownMode = [System.Windows.ShutdownMode]::OnExplicitShutdown
$dict = New-Object System.Windows.ResourceDictionary
$dict.Source = New-Object Uri("/ApexSenseBridgeTray;component/App.xaml", [UriKind]::Relative)
$app.Resources.MergedDictionaries.Add($dict)
$errors = New-Object System.Collections.ArrayList
$app.add_DispatcherUnhandledException({ param($s, $e) [void]$errors.Add($e.Exception.ToString()); $e.Handled = $true })
[ApexSenseBridgeTray.Common.LocalizationManager]::Initialize("en")
[ApexSenseBridgeTray.Common.ThemeManager]::Initialize()
$flags = [Reflection.BindingFlags]"Instance,NonPublic"
$windowType = [ApexSenseBridgeTray.GameListWindow]
$managerType = [ApexSenseBridgeTray.Services.EngineSessionManager]

function Pump([int]$frames = 10) {
    for ($k = 0; $k -lt $frames; $k++) {
        $frame = New-Object Windows.Threading.DispatcherFrame
        [void][Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke(
            [Windows.Threading.DispatcherPriority]::Background, [Action]{ $frame.Continue = $false })
        [Windows.Threading.Dispatcher]::PushFrame($frame)
    }
}
function Show-OffScreen($window) {
    $window.WindowStartupLocation = "Manual"; $window.Left = -5000; $window.Top = 0; $window.ShowActivated = $false
    $window.Show(); Pump 15
}

$count = 0
$manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
try {
    # The support callout is marked as seen: showing it would save the real user settings.
    $settings = New-Object ApexSenseBridgeTray.Models.TraySettings
    $settings.SupportHintShown = $true
    $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
    [ApexSenseBridgeTray.Services.CloudGameListService].GetMethod("LoadEmbeddedDatabase", $flags).Invoke($service, $null)
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @($service, $settings, $null, $manager, $null, $null, "dashboard")
    $windowType.GetField("controllerDetection", $flags).GetValue($window).Dispose()
    Show-OffScreen $window
    $windowType.GetField("gamepadNav", $flags).GetValue($window).Stop()
    $windowType.GetField("isGamepadMode", $flags).SetValue($window, $true)
    foreach ($status in @("apex4", "apex5", "apex6", "disconnected")) {
        $windowType.GetMethod("UpdateControllerStatus", $flags).Invoke($window, @($status))
        foreach ($tab in 1, 0, 3, 2, 0) {
            $windowType.GetMethod("SetCurrentTab", $flags).Invoke($window, @([int]$tab))
            if ($tab -eq 0) { $windowType.GetMethod("SetDashboardNav", $flags).Invoke($window, @([int]1, [int]2)) }
            if ($tab -eq 1) { $windowType.GetMethod("SelectGame", $flags).Invoke($window, @([int]3)) }
            if ($tab -eq 3) { $windowType.GetMethod("SetSettingsNav", $flags).Invoke($window, @([int]1, [int]1)) }
            Pump
        }
        $count++
    }
    $window.Close()
    $picker = New-Object ApexSenseBridgeTray.LanguagePickerWindow -ArgumentList @("fr")
    Show-OffScreen $picker
    $picker.Close(); $count++

    $diagnostic = New-Object ApexSenseBridgeTray.ControllerTestWindow -ArgumentList @($settings)
    Show-OffScreen $diagnostic
    foreach ($tab in "TabVibration", "TabRgb", "TabGyro", "TabLatency", "TabMapping", "TabTriggers") {
        [ApexSenseBridgeTray.ControllerTestWindow].GetField($tab, $flags).GetValue($diagnostic).IsChecked = $true
        Pump 5
    }
    $diagnostic.Close(); $count++
    Pump

    if ($errors.Count -gt 0) { throw "Dispatcher exceptions while rendering:`n" + ($errors -join "`n----`n") }
    Write-Output "PASS: $count shown-window scenarios without dispatcher exceptions; no user settings written."
} finally {
    ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
    $app.Shutdown()
}
