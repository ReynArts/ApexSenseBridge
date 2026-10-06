param(
    [string]$TrayAssembly = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridgeTray.exe")
)

# Issue #29: the home shelf leads with the latest catalogue additions.
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
$serviceType = [ApexSenseBridgeTray.Services.CloudGameListService]

function New-Catalog([object[]]$games) {
    $service = New-Object ApexSenseBridgeTray.Services.CloudGameListService
    [string]$json = (@{ games = $games } | ConvertTo-Json -Depth 4 -Compress)
    if (-not $serviceType.GetMethod("ParseAndLoadJson", $flags).Invoke($service, [object[]]@([string]$json))) { throw "Catalogue rejected" }
    return $service
}
function Game([string]$title, [string]$added) {
    $g = [ordered]@{ title = $title; normalized = ($title.ToLowerInvariant() -replace '[^a-z0-9]', ''); adaptiveTriggers = $true; hapticFeedback = $true }
    if ($added) { $g.addedAt = $added }
    return $g
}
function Day([int]$daysAgo) { [DateTime]::UtcNow.Date.AddDays(-$daysAgo).ToString("yyyy-MM-dd") }

$count = 0
$windows = New-Object System.Collections.ArrayList
try {
    $games = @()
    foreach ($i in 1..10) { $games += Game "Initial $i" (Day 120) }
    $games += Game "Fresh Arrival" (Day 2)
    $games += Game "Same Day B" (Day 5)
    $games += Game "Same Day A" (Day 5)
    $games += Game "Older Addition" (Day 45)
    $games += Game "Excluded Addition" (Day 1)
    $settings = New-Object ApexSenseBridgeTray.Models.TraySettings
    $settings.ExcludedGames.Add("excludedaddition")
    $manager = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
    $window = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @((New-Catalog $games), $settings, $null, $manager, $null, $null, "dashboard")
    [void]$windows.Add([pscustomobject]@{ Window = $window; Manager = $manager })
    $shelf = @($windowType.GetField("dashboardFeaturedGames", $flags).GetValue($window))

    $titles = @($shelf | ForEach-Object { $_.Title })
    $expectedHead = @("Fresh Arrival", "Same Day A", "Same Day B", "Older Addition")
    if (($titles[0..3] -join "|") -ne ($expectedHead -join "|")) { throw "Additions are not newest first: $($titles -join ', ')" }
    $count++
    if ($titles -contains "Excluded Addition") { throw "Excluded games must not be featured" }
    $count++
    if ($shelf.Count -ne 14 -or $titles[4] -notlike "Initial *") { throw "The shelf was not completed with the regular selection" }
    $count++
    $flagsByTitle = @{}; $shelf | ForEach-Object { $flagsByTitle[$_.Title] = $_.IsRecentlyAdded }
    if (-not $flagsByTitle["Fresh Arrival"] -or -not $flagsByTitle["Same Day A"]) { throw "Recent additions lack the New badge" }
    if ($flagsByTitle["Older Addition"]) { throw "Additions older than 30 days keep the New badge" }
    if ($flagsByTitle["Initial 1"]) { throw "The initial import must not be flagged as new" }
    $count++
    if ($windowType.GetField("TxtShelfTitle", $flags).GetValue($window).Text -ne "Recently added") { throw "Shelf title does not announce additions" }
    $count++

    # Catalogue without dates (older cache): regular featured shelf and title.
    $undated = @(); foreach ($i in 1..6) { $undated += Game "Plain $i" $null }
    $manager2 = New-Object ApexSenseBridgeTray.Services.EngineSessionManager
    $window2 = New-Object ApexSenseBridgeTray.GameListWindow -ArgumentList @((New-Catalog $undated), (New-Object ApexSenseBridgeTray.Models.TraySettings), $null, $manager2, $null, $null, "dashboard")
    [void]$windows.Add([pscustomobject]@{ Window = $window2; Manager = $manager2 })
    $shelf2 = @($windowType.GetField("dashboardFeaturedGames", $flags).GetValue($window2))
    if ($shelf2.Count -ne 6 -or ($shelf2 | Where-Object { $_.IsRecentlyAdded })) { throw "Undated catalogue shelf is wrong" }
    if ($windowType.GetField("TxtShelfTitle", $flags).GetValue($window2).Text -ne "Certified games") { throw "Undated catalogue title is wrong" }
    $count++

    Write-Output "PASS: $count home shelf scenarios (issue #29); no user settings written."
} finally {
    foreach ($pair in $windows) {
        $pair.Window.Close()
        ([Threading.Timer]$managerType.GetField("healthTimer", $flags).GetValue($pair.Manager)).Dispose()
    }
    $app.Shutdown()
}
