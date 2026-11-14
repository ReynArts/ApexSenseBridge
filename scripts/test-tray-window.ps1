param(
    [string]$TrayAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridgeTray.exe")
)

$ErrorActionPreference = "Stop"
if ([Threading.Thread]::CurrentThread.ApartmentState -ne "STA") {
    throw "Run this WPF regression test with Windows PowerShell -STA."
}
$TrayAssembly = (Resolve-Path -LiteralPath $TrayAssembly).Path
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
$trayResourceAssembly = [Reflection.Assembly]::LoadFrom($TrayAssembly)
[System.Windows.Application]::ResourceAssembly = $trayResourceAssembly
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
    $monitorType = [ApexSenseBridgeTray.Services.ProcessMonitorService]
    $ctor = $monitorType.GetConstructor($flags, $null, [Type[]]@(
        [ApexSenseBridgeTray.Services.CloudGameListService], $managerType,
        [ApexSenseBridgeTray.Services.ExecutableLearningService], [ApexSenseBridgeTray.Models.TraySettings], [bool]), $null)
    $readiness = "Virtual DualSense HID readiness verification failed after 2 attempts: The newly-created virtual DualSense firmware interface was not found."
    $manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
    $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
    $settings = New-Object ApexSenseBridgeTray.Models.TraySettings
    $monitor = $ctor.Invoke([object[]]@($service.psobject.BaseObject, $manager.psobject.BaseObject, $null, $settings.psobject.BaseObject, $false))
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
        $service, $settings, $null, $manager, $monitor, $null, "dashboard")
    try {
        $update = $windowType.GetMethod("UpdateDashboardStatus", $flags)
        $field = { param($name) $windowType.GetField($name, $flags).GetValue($window) }
        $policy = $monitorType.GetField("startRetries", $flags).GetValue($monitor)
        $record = $policy.GetType().GetMethod("RecordFailure")
        $now = [DateTime]::UtcNow
        [void]$record.Invoke($policy, @("silentgame", 9, $readiness, $now, [uint32[]]@(4101)))
        [void]$record.Invoke($policy, @("silentgame", 9, $readiness, $now.AddSeconds(11), [uint32[]]@(4102)))
        $managerType.GetField("failed", $flags).SetValue($manager, $true)
        $managerType.GetField("lastReason", $flags).SetValue($manager, $readiness)
        $managerType.GetField("activeGameTitle", $flags).SetValue($manager, "Silent Game")
        $windowType.GetField("viewInitialized", $flags).SetValue($window, $true)
        $update.Invoke($window, $null)
        if ((& $field "BtnRetryStart").Visibility -ne [Windows.Visibility]::Visible) { throw "A paused automatic start offered no Retry action." }
        if ((& $field "TxtDashboardHint").Text -ne $readiness) { throw "The paused start did not show the engine error text." }
        $monitor.RetryPausedStarts()
        $update.Invoke($window, $null)
        if ((& $field "BtnRetryStart").Visibility -ne [Windows.Visibility]::Collapsed) { throw "Retry stayed visible after resuming automatic starts." }
        $count++

        (& $field "PnlDashboardActiveCover").Visibility = [Windows.Visibility]::Visible
        (& $field "TxtDashboardGameTitle").Text = "Silent Game"
        $managerType.GetField("failed", $flags).SetValue($manager, $false)
        $managerType.GetField("lastReason", $flags).SetValue($manager, $null)
        $managerType.GetField("activeGameTitle", $flags).SetValue($manager, $null)
        $update.Invoke($window, $null)
        if ((& $field "PnlDashboardActiveCover").Visibility -ne [Windows.Visibility]::Collapsed) { throw "The game artwork stayed on the dashboard after the session ended." }
        if ((& $field "TxtDashboardGameTitle").Text -ne [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_NoActiveGame")) { throw "The dashboard kept the previous game title after the session ended." }
        if ((& $field "BtnRetryStart").Visibility -ne [Windows.Visibility]::Collapsed) { throw "An idle dashboard offered Retry." }
        $count++
    } finally {
        $window.Close()
        $monitor.Dispose()
        ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($manager)).Dispose()
    }
    $bridgeType = $managerType.Assembly.GetType("ApexSenseBridge.Common.BridgeSession")
    $mapName = "Local\ASB.WindowLostTest." + [Guid]::NewGuid().ToString("N")
    $statusMap = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($mapName + ".Status", 512)
    $progressMap = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($mapName + ".Progress", 16)
    $statusView = $statusMap.CreateViewAccessor(0, 512)
    $progressView = $progressMap.CreateViewAccessor(0, 16)
    $lostManager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
    ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($lostManager)).Dispose()
    $session = [Runtime.Serialization.FormatterServices]::GetUninitializedObject($bridgeType)
    $lostWindow = $null
    try {
        $statusView.Write([long]0, [uint32]0x53425341)
        $statusView.Write([long]4, [uint16]1)
        $statusView.Write([long]6, [uint16]2)
        $progressView.Write([long]0, [uint32]0x50534241)
        $progressView.Write([long]4, [uint32]1)
        $progressView.Write([long]8, [uint32]15)
        $bridgeType.GetField("statusView", $flags).SetValue($session, $statusView)
        $bridgeType.GetField("progressView", $flags).SetValue($session, $progressView)
        $bridgeType.GetField("process", $flags).SetValue($session, [Diagnostics.Process]::GetCurrentProcess())
        $managerType.GetField("activeSession", $flags).SetValue($lostManager, $session)
        $managerType.GetField("activeGameTitle", $flags).SetValue($lostManager, "Lost Controller Game")
        $managerType.GetField("activeProfile", $flags).SetValue($lostManager, "standard")
        $managerType.GetField("activeProfileName", $flags).SetValue($lostManager, "standard")
        $lostWindow = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @(
            $service, $settings, $null, $lostManager, $null, $null, "dashboard")
        $windowType.GetField("viewInitialized", $flags).SetValue($lostWindow, $true)
        $lostField = { param($name) $windowType.GetField($name, $flags).GetValue($lostWindow) }
        $health = $managerType.GetMethod("CheckHealth", $flags)
        $refresh = {
            $health.Invoke($lostManager, @($null))
            $windowType.GetMethod("UpdateDashboardStatus", $flags).Invoke($lostWindow, $null)
        }
        $readyText = [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_SessionReady")
        $lostText = [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_SessionControllerLost")
        $lostHint = [ApexSenseBridgeTray.Common.LocalizationManager]::Get("Loc_SessionControllerLostHint")
        if ($lostText -eq "Loc_SessionControllerLost" -or $lostHint -eq "Loc_SessionControllerLostHint" -or $lostText -eq $readyText) { throw "The controller-lost status is not localized." }
        $standbyBrush = $lostWindow.FindResource("BadgeStandbyBg")
        $activeBrush = $lostWindow.FindResource("BadgeActiveBg")

        & $refresh
        if ((& $lostField "TxtDashboardStatus").Text -ne $readyText) { throw "A healthy session did not show the ready status." }
        if ((& $lostField "TxtDashboardHint").Visibility -ne [Windows.Visibility]::Collapsed) { throw "A healthy session showed a hint." }
        if (-not [object]::ReferenceEquals((& $lostField "BadgeDashboardStatus").Background, $activeBrush)) { throw "A healthy session lost the active badge." }

        $progressView.Write([long]8, [uint32]31)
        & $refresh
        if (-not $lostManager.ControllerLost) { throw "The session manager did not report the lost controller." }
        if ((& $lostField "TxtDashboardStatus").Text -ne $lostText) { throw "A lost controller did not show the waiting status." }
        if ((& $lostField "TxtDashboardHint").Text -ne $lostHint -or (& $lostField "TxtDashboardHint").Visibility -ne [Windows.Visibility]::Visible) { throw "A lost controller did not explain that the virtual DualSense is kept." }
        if (-not [object]::ReferenceEquals((& $lostField "BadgeDashboardStatus").Background, $standbyBrush)) { throw "A lost controller kept the green active badge." }
        if ((& $lostField "PnlSessionRecovery").Visibility -ne [Windows.Visibility]::Collapsed -or $lostManager.Recovery.Pending -or $lostManager.AutomaticActivationBlocked) { throw "A lost controller offered recovery." }
        if ((& $lostField "BtnRetryStart").Visibility -ne [Windows.Visibility]::Collapsed) { throw "A lost controller offered Retry." }
        if (-not $lostManager.IsSessionActive) { throw "A lost controller ended the session." }

        $progressView.Write([long]8, [uint32]15)
        & $refresh
        if ($lostManager.ControllerLost) { throw "The session manager kept the lost flag after the controller returned." }
        if ((& $lostField "TxtDashboardStatus").Text -ne $readyText) { throw "The dashboard did not return to the ready status." }
        if ((& $lostField "TxtDashboardHint").Visibility -ne [Windows.Visibility]::Collapsed) { throw "The waiting hint stayed visible after the controller returned." }
        if (-not [object]::ReferenceEquals((& $lostField "BadgeDashboardStatus").Background, $activeBrush)) { throw "The active badge was not restored." }
        $count++
    } finally {
        if ($lostWindow) { $lostWindow.Close() }
        $managerType.GetField("activeSession", $flags).SetValue($lostManager, $null)
        $statusView.Dispose(); $progressView.Dispose(); $statusMap.Dispose(); $progressMap.Dispose()
    }
    Write-Output "PASS: $count WPF window initialization/phase/tab and session-state scenarios."
} finally {
    $app.Shutdown()
}
