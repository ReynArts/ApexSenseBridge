<#
.SYNOPSIS
    Fetches PC games that support DualSense Adaptive Triggers and Haptic Feedback from PCGamingWiki,
    resolves Steam cover thumbnails, and generates data/supported_games.json.
#>

param(
    [string]$OutputPath = "$PSScriptRoot\..\data\supported_games.json"
)

$ErrorActionPreference = "Stop"

Write-Host "Fetching DualSense games data from PCGamingWiki..." -ForegroundColor Cyan

function Get-PcgwGamesFromPage {
    param([string]$PageTitle)

    $encodedTitle = [System.Uri]::EscapeDataString($PageTitle)
    $url = "https://www.pcgamingwiki.com/w/api.php?action=parse&page=$encodedTitle&prop=text&format=json"

    $headers = @{
        "User-Agent" = "ApexSenseBridge-Updater/1.0 (https://github.com/ReynArts/ApexSenseBridge)"
    }

    try {
        $response = Invoke-RestMethod -Uri $url -Headers $headers -Method Get
        if (-not $response.parse -or -not $response.parse.text) {
            Write-Warning "No parse output for page: $PageTitle"
            return @()
        }

        $html = $response.parse.text.'*'
        $regex = '<tr>\s*<td><a href="[^"]*" title="([^"]*)">([^<]*)<\/a><\/td>'
        $matches = [regex]::Matches($html, $regex)

        $titles = @()
        foreach ($m in $matches) {
            $rawTitle = $m.Groups[1].Value
            $decoded = [System.Net.WebUtility]::HtmlDecode($rawTitle)
            if (-not [string]::IsNullOrWhiteSpace($decoded)) {
                $titles += $decoded.Trim()
            }
        }
        return $titles
    }
    catch {
        Write-Error "Failed to fetch from PCGW ($PageTitle): $_"
        return @()
    }
}

function Normalize-Title {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return "" }
    $sb = [System.Text.StringBuilder]::new()
    foreach ($char in $Value.ToCharArray()) {
        if ([char]::IsLetterOrDigit($char)) {
            [void]$sb.Append([char]::ToLowerInvariant($char))
        }
    }
    return $sb.ToString()
}

function Get-SpecialProfile {
    param([string]$Normalized)
    if ($Normalized -like "*spiderman2*") {
        return "spider-man-2"
    }
    if ($Normalized -like "*milesmorales*") {
        return "miles-morales"
    }
    if ($Normalized -like "*ghostoftsushima*") {
        return "ghost-of-tsushima"
    }
    if ($Normalized -eq "warframe" -or $Normalized.StartsWith("warframe")) {
        return "warframe"
    }
    return "standard"
}

function Resolve-SteamIdentity {
    param([string]$Title)
    try {
        $cleanTitle = $Title -replace '[\u2122\u00AE\u00A9]', ''
        $encoded = [System.Uri]::EscapeDataString($cleanTitle.Trim())
        $url = "https://store.steampowered.com/api/storesearch/?term=$encoded&l=english&cc=US"
        $headers = @{ "User-Agent" = "ApexSenseBridge-Updater/1.0" }
        $res = Invoke-RestMethod -Uri $url -Headers $headers -Method Get -TimeoutSec 6
        $expected = Normalize-Title $cleanTitle
        $exactIds = @($res.items | Where-Object {
            (Normalize-Title ($_.name -replace '[\u2122\u00AE\u00A9]', '')) -eq $expected -and
            [int]$_.id -gt 0
        } | ForEach-Object { [int]$_.id } | Select-Object -Unique)
        if ($exactIds.Count -eq 1) {
            $appId = $exactIds[0]
            $img = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/$appId/library_600x900.jpg"
            return @{ Status = "verified"; SteamAppId = $appId; IconUrl = $img }
        }
        return @{ Status = "no_match"; SteamAppId = 0; IconUrl = "" }
    } catch {
        return @{ Status = "error"; SteamAppId = 0; IconUrl = ""; Error = $_.Exception.Message }
    }
}

function Get-ExecutableName {
    param([object]$Value)
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) { return "" }
    $name = (($Value.Trim() -replace '\\', '/') -split '/')[-1].Trim()
    if (-not $name.EndsWith(".exe", [System.StringComparison]::OrdinalIgnoreCase)) { return "" }
    if ($name -match '[<>:"/\\|?*\x00-\x1f]') { return "" }
    return $name
}

function Test-AncillaryExecutable {
    param([string]$Name)
    $normalized = $Name.ToLowerInvariant()
    if ($normalized -eq "content manager.exe" -or $normalized -eq "crashreportclient.exe") {
        return $true
    }
    foreach ($marker in @(
        "launcher", "servermanager", "showroom", "editor", "benchmark",
        "crashreport", "crashpad", "configurator", "configurationtool",
        "updater", "uninstaller", "diagnostic")) {
        if ($normalized.Contains($marker)) { return $true }
    }
    return $false
}

function Get-CachedExecutables {
    param([object]$Cached)
    $names = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    if ($Cached -and $Cached.executables) {
        foreach ($value in $Cached.executables) {
            $name = Get-ExecutableName $value
            if ($name) { [void]$names.Add($name) }
        }
    }
    return @($names | Sort-Object)
}

function Get-DiscordExecutableIndex {
    $headers = @{
        "User-Agent" = "ApexSenseBridge-Updater/1.0 (https://github.com/ReynArts/ApexSenseBridge)"
        "Accept" = "application/json"
    }
    try {
        $applications = Invoke-RestMethod `
            -Uri "https://discord.com/api/v9/applications/detectable" `
            -Headers $headers -Method Get -TimeoutSec 60
        $index = @{}
        foreach ($application in $applications) {
            $steamIds = @($application.third_party_skus | Where-Object {
                $_.distributor -and $_.distributor.ToString().Equals(
                    "steam", [System.StringComparison]::OrdinalIgnoreCase) -and
                $_.id -and $_.id.ToString() -match '^\d+$' -and [long]$_.id -gt 0
            } | ForEach-Object { [int]$_.id } | Select-Object -Unique)
            if ($steamIds.Count -eq 0) { continue }

            $names = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::OrdinalIgnoreCase)
            foreach ($executable in $application.executables) {
                if ($executable.os -ne "win32" -or $executable.is_launcher -eq $true) { continue }
                $name = Get-ExecutableName $executable.name
                if ($name -and -not (Test-AncillaryExecutable $name)) {
                    [void]$names.Add($name)
                }
            }
            if ($names.Count -eq 0) { continue }

            foreach ($steamId in $steamIds) {
                if (-not $index.ContainsKey($steamId)) {
                    $index[$steamId] = [System.Collections.Generic.HashSet[string]]::new(
                        [System.StringComparer]::OrdinalIgnoreCase)
                }
                foreach ($name in $names) { [void]$index[$steamId].Add($name) }
            }
        }
        Write-Host "Indexed Discord executables for $($index.Count) Steam AppIDs." -ForegroundColor Green
        return $index
    } catch {
        Write-Warning "Discord executable enrichment unavailable: $_. Keeping cached names."
        return $null
    }
}

# 1. Load existing cache
$existingCache = @{}
if (Test-Path $OutputPath) {
    try {
        $oldJson = Get-Content -Path $OutputPath -Raw | ConvertFrom-Json
        foreach ($g in $oldJson.games) {
            if ($g.normalized) {
                $existingCache[$g.normalized] = $g
            }
        }
    } catch {}
}

$adaptiveGames = Get-PcgwGamesFromPage -PageTitle "List of games that support PlayStation adaptive triggers"
Write-Host "Found $($adaptiveGames.Count) games with Adaptive Triggers." -ForegroundColor Green

$hapticGames = Get-PcgwGamesFromPage -PageTitle "List of games that support Dualsense haptic feedback"
Write-Host "Found $($hapticGames.Count) games with Haptic Feedback." -ForegroundColor Green

$gameDict = [System.Collections.Generic.Dictionary[string, hashtable]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($title in $adaptiveGames) {
    $norm = Normalize-Title $title
    $cached = $existingCache[$norm]
    $appId = if ($cached -and $cached.steamAppId) { [int]$cached.steamAppId } else { 0 }
    $appIdVerified = if ($cached -and $cached.steamAppIdVerified -eq $true) { $true } else { $false }
    $icon = if ($appId -gt 0) { "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/$appId/library_600x900.jpg" } else { "" }

    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $true
            hapticFeedback = $false
            profile = (Get-SpecialProfile $norm)
            iconUrl = $icon
            steamAppId = $appId
            steamAppIdVerified = $appIdVerified
            executables = @(Get-CachedExecutables $cached)
        }
    } else {
        $gameDict[$norm].adaptiveTriggers = $true
    }
}

foreach ($title in $hapticGames) {
    $norm = Normalize-Title $title
    $cached = $existingCache[$norm]
    $appId = if ($cached -and $cached.steamAppId) { [int]$cached.steamAppId } else { 0 }
    $appIdVerified = if ($cached -and $cached.steamAppIdVerified -eq $true) { $true } else { $false }
    $icon = if ($appId -gt 0) { "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/$appId/library_600x900.jpg" } else { "" }

    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $false
            hapticFeedback = $true
            profile = (Get-SpecialProfile $norm)
            iconUrl = $icon
            steamAppId = $appId
            steamAppIdVerified = $appIdVerified
            executables = @(Get-CachedExecutables $cached)
        }
    } else {
        $gameDict[$norm].hapticFeedback = $true
    }
}

# Ensure verified built-in titles are present even if not yet on PCGW
$builtin = @(
    @{ title = "Marvel's Spider-Man 2"; normalized = "marvelsspiderman2"; profile = "spider-man-2"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 0; steamAppIdVerified = $false; iconUrl = "" },
    @{ title = "Marvel's Spider-Man: Miles Morales"; normalized = "marvelsspidermanmilesmorales"; profile = "miles-morales"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 1817190; steamAppIdVerified = $true; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1817190/library_600x900.jpg" },
    @{ title = "Ghost of Tsushima DIRECTOR'S CUT"; normalized = "ghostoftsushimadirectorscut"; profile = "ghost-of-tsushima"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 2215430; steamAppIdVerified = $true; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2215430/library_600x900.jpg" },
    @{ title = "Warframe"; normalized = "warframe"; profile = "warframe"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 230410; steamAppIdVerified = $true; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/230410/library_600x900.jpg" },
    @{ title = "Call of Duty: Modern Warfare 4 Beta"; normalized = "callofdutymodernwarfare4beta"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 1938090; steamAppIdVerified = $false; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1938090/library_600x900.jpg" },
    @{ title = "Grand Theft Auto V"; normalized = "grandtheftautov"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 271590; steamAppIdVerified = $true; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/271590/library_600x900.jpg" }
)

foreach ($b in $builtin) {
    if (-not $gameDict.ContainsKey($b.normalized)) {
        $gameDict[$b.normalized] = $b
    } else {
        if ($b.profile -ne "standard") {
            $gameDict[$b.normalized].profile = $b.profile
        }
        if ($b.iconUrl) {
            $gameDict[$b.normalized].iconUrl = $b.iconUrl
        }
        if ($b.steamAppId) {
            $gameDict[$b.normalized].steamAppId = $b.steamAppId
            $gameDict[$b.normalized].steamAppIdVerified = $b.steamAppIdVerified
        }
    }
}

# Audit every legacy/unverified AppID once. Future runs reuse verified identities.
$unverifiedGames = @($gameDict.Values | Where-Object { $_.steamAppIdVerified -ne $true })
if ($unverifiedGames.Count -gt 0) {
    Write-Host "Verifying exact Steam identities for $($unverifiedGames.Count) games..." -ForegroundColor Cyan
    $verifiedCount = 0
    $unresolvedCount = 0
    $requestErrors = 0
    foreach ($g in $unverifiedGames) {
        $steamInfo = Resolve-SteamIdentity -Title $g.title
        if ($steamInfo.Status -eq "verified") {
            $g.iconUrl = $steamInfo.IconUrl
            $g.steamAppId = $steamInfo.SteamAppId
            $g.steamAppIdVerified = $true
            $verifiedCount++
        } elseif ($steamInfo.Status -eq "no_match") {
            if ([string]::IsNullOrWhiteSpace($g.iconUrl)) {
                $g.iconUrl = ""
            }
            $g.steamAppId = 0
            $g.steamAppIdVerified = $false
            $unresolvedCount++
        } else {
            $g.steamAppIdVerified = $false
            $requestErrors++
        }
        Start-Sleep -Milliseconds 50
    }
    Write-Host "Steam identity audit: $verifiedCount verified, $unresolvedCount unresolved, $requestErrors request errors." -ForegroundColor Green
}

$outputList = @($gameDict.Values | Sort-Object { $_.title })

Write-Host "Fetching Discord detectable executables..." -ForegroundColor Cyan
$discordIndex = Get-DiscordExecutableIndex
$discordMatchedGames = 0
if ($null -ne $discordIndex) {
    foreach ($game in $outputList) {
        if ($game.steamAppIdVerified -ne $true) {
            [void]$game.Remove("executables")
            continue
        }
        $appId = [int]$game.steamAppId
        if ($appId -gt 0 -and $discordIndex.ContainsKey($appId)) {
            $game.executables = @($discordIndex[$appId] | Sort-Object)
            $discordMatchedGames++
        }
    }
}

# A basename that identifies two supported games is unsafe and must never enter the runtime index.
$owners = @{}
foreach ($game in $outputList) {
    if ($game.steamAppIdVerified -ne $true) {
        [void]$game.Remove("executables")
        continue
    }
    $cleanNames = @(Get-CachedExecutables $game)
    $game.executables = $cleanNames
    foreach ($name in $cleanNames) {
        $key = $name.ToLowerInvariant()
        if (-not $owners.ContainsKey($key)) {
            $owners[$key] = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::OrdinalIgnoreCase)
        }
        [void]$owners[$key].Add($game.normalized)
    }
}
$ambiguousNames = @($owners.Keys | Where-Object { $owners[$_].Count -gt 1 })
foreach ($game in $outputList) {
    $game.executables = @($game.executables | Where-Object {
        $_ -and $ambiguousNames -notcontains $_.ToLowerInvariant()
    })
    if ($game.executables.Count -eq 0) { [void]$game.Remove("executables") }
}

$outputDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

$payload = @{
    version = 1
    updatedAt = (Get-Date).ToUniversalTime().ToString("o")
    totalGames = $outputList.Count
    discordExecutableGames = @($outputList | Where-Object { $_.executables.Count -gt 0 }).Count
    games = $outputList
}

$json = $payload | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.Encoding]::UTF8)

Write-Host "Successfully generated $OutputPath with $($outputList.Count) supported games." -ForegroundColor Green
