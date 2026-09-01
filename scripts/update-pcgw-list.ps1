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

function Get-SteamCover {
    param([string]$Title)
    try {
        $cleanTitle = $Title -replace '[\u2122\u00AE\u00A9]', ''
        $encoded = [System.Uri]::EscapeDataString($cleanTitle.Trim())
        $url = "https://store.steampowered.com/api/storesearch/?term=$encoded&l=english&cc=US"
        $headers = @{ "User-Agent" = "ApexSenseBridge-Updater/1.0" }
        $res = Invoke-RestMethod -Uri $url -Headers $headers -Method Get -TimeoutSec 6
        if ($res.items -and $res.items.Count -gt 0) {
            $first = $res.items[0]
            $appId = [int]$first.id
            if ($appId -gt 0) {
                $img = $first.tiny_image
                if (-not $img) {
                    $img = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/$appId/capsule_231x87.jpg"
                }
                return @{ SteamAppId = $appId; IconUrl = $img }
            }
        }
    } catch {
    }
    return @{ SteamAppId = 0; IconUrl = "" }
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
    $icon = if ($cached -and $cached.iconUrl) { $cached.iconUrl } else { "" }
    $appId = if ($cached -and $cached.steamAppId) { $cached.steamAppId } else { 0 }

    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $true
            hapticFeedback = $false
            profile = (Get-SpecialProfile $norm)
            iconUrl = $icon
            steamAppId = $appId
        }
    } else {
        $gameDict[$norm].adaptiveTriggers = $true
    }
}

foreach ($title in $hapticGames) {
    $norm = Normalize-Title $title
    $cached = $existingCache[$norm]
    $icon = if ($cached -and $cached.iconUrl) { $cached.iconUrl } else { "" }
    $appId = if ($cached -and $cached.steamAppId) { $cached.steamAppId } else { 0 }

    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $false
            hapticFeedback = $true
            profile = (Get-SpecialProfile $norm)
            iconUrl = $icon
            steamAppId = $appId
        }
    } else {
        $gameDict[$norm].hapticFeedback = $true
    }
}

# Ensure verified built-in titles are present even if not yet on PCGW
$builtin = @(
    @{ title = "Marvel's Spider-Man 2"; normalized = "marvelsspiderman2"; profile = "spider-man-2"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 0; iconUrl = "" },
    @{ title = "Marvel's Spider-Man: Miles Morales"; normalized = "marvelsspidermanmilesmorales"; profile = "miles-morales"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 1817190; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1817190/capsule_231x87.jpg" },
    @{ title = "Ghost of Tsushima DIRECTOR'S CUT"; normalized = "ghostoftsushimadirectorscut"; profile = "ghost-of-tsushima"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 2215430; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2215430/capsule_231x87.jpg" },
    @{ title = "Warframe"; normalized = "warframe"; profile = "warframe"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 230410; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/230410/capsule_231x87.jpg" },
    @{ title = "Call of Duty: Modern Warfare 4 Beta"; normalized = "callofduty"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 1938090; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1938090/capsule_231x87.jpg" },
    @{ title = "Grand Theft Auto V"; normalized = "grandtheftautov"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true; steamAppId = 271590; iconUrl = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/271590/capsule_231x87.jpg" }
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
        }
    }
}

# Resolve Steam thumbnails for any games that do not have iconUrl
$missingIcons = @($gameDict.Values | Where-Object { -not $_.iconUrl })
if ($missingIcons.Count -gt 0) {
    Write-Host "Resolving Steam cover thumbnails for $($missingIcons.Count) games..." -ForegroundColor Cyan
    $counter = 0
    foreach ($g in $missingIcons) {
        $steamInfo = Get-SteamCover -Title $g.title
        if ($steamInfo.IconUrl) {
            $g.iconUrl = $steamInfo.IconUrl
            $g.steamAppId = $steamInfo.SteamAppId
            $counter++
        }
        Start-Sleep -Milliseconds 40
    }
    Write-Host "Resolved $counter cover thumbnails." -ForegroundColor Green
}

$outputList = @($gameDict.Values | Sort-Object { $_.title })

$outputDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

$payload = @{
    version = 1
    updatedAt = (Get-Date).ToUniversalTime().ToString("o")
    totalGames = $outputList.Count
    games = $outputList
}

$json = $payload | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.Encoding]::UTF8)

Write-Host "Successfully generated $OutputPath with $($outputList.Count) supported games." -ForegroundColor Green
