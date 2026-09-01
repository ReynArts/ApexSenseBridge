<#
.SYNOPSIS
    Checks for ApexSenseBridge updates from GitHub Releases and downloads/installs the latest setup.
.DESCRIPTION
    Autonomous CLI updater for ApexSenseBridge (Standalone users without Playnite).
.PARAMETER CheckOnly
    Only checks if an update is available without downloading or installing.
.PARAMETER Silent
    Downloads and installs the update silently in the background.
.PARAMETER Force
    Installs even if already on the latest version.
#>

param(
    [switch]$CheckOnly,
    [switch]$Silent,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repo = "ReynArts/ApexSenseBridge"
$apiUrl = "https://api.github.com/repos/$repo/releases/latest"

Write-Host "=== ApexSenseBridge Updater ===" -ForegroundColor Cyan

# 1. Detect currently installed version
$installedVersion = "0.0.0"
$regPath = "HKLM:\SOFTWARE\ApexSenseBridge"
if (Test-Path $regPath) {
    $v = (Get-ItemProperty -Path $regPath -Name "Version" -ErrorAction SilentlyContinue).Version
    if ($v) { $installedVersion = $v }
}

if ($installedVersion -eq "0.0.0") {
    $localExe = Join-Path $PSScriptRoot "..\build-win\Release\ApexSenseBridge.exe"
    if (Test-Path $localExe) {
        $info = (Get-Item $localExe).VersionInfo.ProductVersion
        if ($info) { $installedVersion = $info.Trim() }
    }
}

if ($installedVersion -eq "0.0.0") {
    $installedVersion = "0.5.0"
}

Write-Host "Installed Version : v$installedVersion"

# 2. Fetch latest release from GitHub API
Write-Host "Checking GitHub Releases for updates..."
$headers = @{ "User-Agent" = "ApexSenseBridge-Updater/1.0" }

try {
    $release = Invoke-RestMethod -Uri $apiUrl -Headers $headers -Method Get
} catch {
    Write-Error "Failed to query GitHub API: $_"
    exit 1
}

$latestTag = $release.tag_name.TrimStart('v', 'V')
$releaseUrl = $release.html_url
$releaseBody = $release.body

Write-Host "Latest Version    : v$latestTag" -ForegroundColor Green

# 3. Version comparison
function Compare-SemVer([string]$v1, [string]$v2) {
    try {
        $a = [System.Version]::Parse($v1)
        $b = [System.Version]::Parse($v2)
        return $a.CompareTo($b)
    } catch {
        return [string]::Compare($v1, $v2)
    }
}

$isNewer = (Compare-SemVer $latestTag $installedVersion) -gt 0

if (-not $isNewer -and -not $Force) {
    Write-Host ""
    Write-Host "[OK] ApexSenseBridge is already up to date (v$installedVersion)." -ForegroundColor Green
    exit 0
}

Write-Host ""
Write-Host "A new version of ApexSenseBridge is available: v$latestTag" -ForegroundColor Yellow

if ($CheckOnly) {
    Write-Host "Release URL: $releaseUrl"
    exit 0
}

# 4. Find installer asset in the release
$installerAsset = $release.assets | Where-Object { $_.name -like "*.exe" } | Select-Object -First 1

if (-not $installerAsset) {
    Write-Warning "No installer .exe found in the latest release assets."
    Write-Host "Opening release page in default browser..."
    Start-Process $releaseUrl
    exit 0
}

$downloadUrl = $installerAsset.browser_download_url
$tempInstaller = Join-Path $env:TEMP "ApexSenseBridge-Setup-$latestTag.exe"

Write-Host "Downloading $downloadUrl..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $downloadUrl -OutFile $tempInstaller -Headers $headers -UseBasicParsing
Write-Host "Download complete: $tempInstaller" -ForegroundColor Green

# 5. Execute installer
Write-Host "Launching installer..." -ForegroundColor Cyan

if ($Silent) {
    $p = Start-Process -FilePath $tempInstaller -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-" -Wait -PassThru
    if ($p.ExitCode -eq 0) {
        Write-Host "Update installed successfully!" -ForegroundColor Green
    } else {
        Write-Warning "Installer exited with code $($p.ExitCode)"
    }
} else {
    Start-Process -FilePath $tempInstaller
    Write-Host "Installer launched. Complete the setup wizard to finish updating." -ForegroundColor Green
}
