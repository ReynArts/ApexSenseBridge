<#
.SYNOPSIS
    Fetches PC games that support DualSense Adaptive Triggers and Haptic Feedback from PCGamingWiki
    and generates data/supported_games.json.
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

$adaptiveGames = Get-PcgwGamesFromPage -PageTitle "List of games that support PlayStation adaptive triggers"
Write-Host "Found $($adaptiveGames.Count) games with Adaptive Triggers." -ForegroundColor Green

$hapticGames = Get-PcgwGamesFromPage -PageTitle "List of games that support Dualsense haptic feedback"
Write-Host "Found $($hapticGames.Count) games with Haptic Feedback." -ForegroundColor Green

$gameDict = [System.Collections.Generic.Dictionary[string, hashtable]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($title in $adaptiveGames) {
    $norm = Normalize-Title $title
    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $true
            hapticFeedback = $false
            profile = (Get-SpecialProfile $norm)
        }
    } else {
        $gameDict[$norm].adaptiveTriggers = $true
    }
}

foreach ($title in $hapticGames) {
    $norm = Normalize-Title $title
    if (-not $gameDict.ContainsKey($norm)) {
        $gameDict[$norm] = @{
            title = $title
            normalized = $norm
            adaptiveTriggers = $false
            hapticFeedback = $true
            profile = (Get-SpecialProfile $norm)
        }
    } else {
        $gameDict[$norm].hapticFeedback = $true
    }
}

# Ensure verified built-in titles are present even if not yet on PCGW
$builtin = @(
    @{ title = "Marvel's Spider-Man 2"; normalized = "marvelsspiderman2"; profile = "spider-man-2"; adaptiveTriggers = $true; hapticFeedback = $true },
    @{ title = "Marvel's Spider-Man: Miles Morales"; normalized = "marvelsspidermanmilesmorales"; profile = "miles-morales"; adaptiveTriggers = $true; hapticFeedback = $true },
    @{ title = "Ghost of Tsushima DIRECTOR'S CUT"; normalized = "ghostoftsushimadirectorscut"; profile = "ghost-of-tsushima"; adaptiveTriggers = $true; hapticFeedback = $true },
    @{ title = "Warframe"; normalized = "warframe"; profile = "warframe"; adaptiveTriggers = $true; hapticFeedback = $true },
    @{ title = "Call of Duty: Modern Warfare 4 Beta"; normalized = "callofduty"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true },
    @{ title = "Grand Theft Auto V"; normalized = "grandtheftautov"; profile = "standard"; adaptiveTriggers = $true; hapticFeedback = $true }
)

foreach ($b in $builtin) {
    if (-not $gameDict.ContainsKey($b.normalized)) {
        $gameDict[$b.normalized] = $b
    } else {
        if ($b.profile -ne "standard") {
            $gameDict[$b.normalized].profile = $b.profile
        }
    }
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
