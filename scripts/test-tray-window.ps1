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
    $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
    [void]$service.GetType().GetMethod("LoadEmbeddedDatabase", $flags).Invoke($service, $null)
    $settings = New-Object ApexSenseBridgeTray.Models.TraySettings
    $settings.SupportHintShown = $true
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @($service, $settings)
    try {
        $games = $windowType.GetField("allGameViewModels", $flags).GetValue($window)
        $game = $games[0]
        $binding = New-Object ApexSenseBridgeTray.Models.LearnedExecutableBinding
        $binding.GameNormalized = $game.Normalized
        $binding.GameTitle = $game.Title
        $item = New-Object ApexSenseBridgeTray.LearnedItemViewModel -ArgumentList $binding
        $learned = $windowType.GetField("filteredLearned", $flags).GetValue($window)
        $learned.Add($item)
        [void]$windowType.GetMethod("SetCurrentTab", $flags).Invoke($window, @([int]2))
        [void]$windowType.GetMethod("SetLearnedNav", $flags).Invoke($window, @([int]0))
        $pane = $windowType.GetField("GameDetailPane", $flags).GetValue($window)
        $learnedPanel = $windowType.GetField("PanelLearnedExecutables", $flags).GetValue($window)
        if ($pane.Parent -ne $learnedPanel -or $window.SelectedGame -ne $game) { throw "Learned game did not expose its shared detail pane." }
        if ($item.IsSelected) { throw "Opening learned details changed the export/delete selection." }
        # A certified-list search must not hide details of the learned game.
        $windowType.GetField("filteredGames", $flags).GetValue($window).Clear()
        [void]$windowType.GetMethod("UpdateDetailPane", $flags).Invoke($window, $null)
        if ($window.SelectedGame -ne $game) { throw "Certified filtering changed the learned game's settings target." }
        [void]$windowType.GetMethod("SetCurrentTab", $flags).Invoke($window, @([int]1))
        if ($pane.Parent -eq $learnedPanel) { throw "Detail pane was lost when returning to certified games." }
        $count++
    } finally { $window.Close() }
    Write-Output "PASS: $count WPF window initialization/phase/tab and learned-detail scenarios."
} finally {
    $app.Shutdown()
}
