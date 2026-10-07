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
# Load resources without running App.OnStartup or starting a bridge.
$app = New-Object ApexSenseBridgeTray.App
$app.InitializeComponent()
[ApexSenseBridgeTray.Common.LocalizationManager]::Initialize("en")
[ApexSenseBridgeTray.Common.ThemeManager]::Initialize()
$flags = [Reflection.BindingFlags]"Instance,NonPublic"
$windowType = [ApexSenseBridgeTray.GameListWindow]
$managerType = [ApexSenseBridgeTray.Services.EngineSessionManager]
$tabs = @("dashboard", "games", "learned", "settings")
$count = 0
try {
    foreach ($phase in @("Stopped", "Starting", "Failed")) {
        foreach ($tab in $tabs) {
            $manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
            $managerType.GetField("isStarting", $flags).SetValue($manager, ($phase -eq "Starting"))
            $managerType.GetField("failed", $flags).SetValue($manager, ($phase -eq "Failed"))
            $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
            $settings = New-Object ApexSenseBridgeTray.Models.TraySettings
            $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
                $service, $settings, $null, $manager, $null, $null, $tab)
            try {
                $expected = [Array]::IndexOf($tabs, $tab)
                $current = $windowType.GetField("currentTabIndex", $flags)
                if ($current.GetValue($window) -ne $expected) { throw "Initial tab was lost." }
                $initialized = $windowType.GetField("viewInitialized", $flags)
                $initialized.SetValue($window, $false)
                $windowType.GetMethod("SetCurrentTab", $flags).Invoke($window, @([int](($expected + 1) % 4)))
                $windowType.GetMethod("UpdateDashboardStatus", $flags).Invoke($window, $null)
                if ($current.GetValue($window) -ne $expected) { throw "Partial view handled navigation." }
                $initialized.SetValue($window, $true)
                $windowType.GetMethod("UpdateDashboardStatus", $flags).Invoke($window, $null)
                # Reproduce the missing named control from the reported stack,
                # including Starting/Failed rather than only an idle window.
                $titleField = $windowType.GetField("TxtDashboardGameTitle", $flags)
                $title = $titleField.GetValue($window)
                try {
                    $titleField.SetValue($window, $null)
                    $windowType.GetMethod("UpdateDashboardStatus", $flags).Invoke($window, $null)
                } finally { $titleField.SetValue($window, $title) }
                $count++
            } finally {
                $window.Close()
                ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
            }
        }
    }
    Write-Output "PASS: $count WPF window initialization/phase/tab scenarios."
} finally {
    $app.Shutdown()
}
