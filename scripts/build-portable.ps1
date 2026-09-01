param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "build-win\Release"
$distDir = Join-Path $projectRoot "dist"
$stagingDir = Join-Path $distDir "ApexSenseBridge-Portable"
$zipPath = Join-Path $distDir "ApexSenseBridge-Portable.zip"

function Fail([string]$Message) {
    throw "ApexSenseBridge portable package: $Message"
}

function Copy-RequiredFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        Fail "required file is missing: $Source"
    }
    $destinationDirectory = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationDirectory)) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

if (-not $SkipBuild) {
    Write-Host "Building native portable payload..."
    & (Join-Path $PSScriptRoot "build-windows.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "native build failed" }

    Write-Host "Building portable Tray application..."
    & (Join-Path $PSScriptRoot "build-tray-app.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "Tray app build failed" }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

$distFull = [System.IO.Path]::GetFullPath($distDir).TrimEnd('\')
$stagingFull = [System.IO.Path]::GetFullPath($stagingDir).TrimEnd('\')
if (-not $stagingFull.StartsWith($distFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to replace unexpected staging path: $stagingFull"
}
if (Test-Path -LiteralPath $stagingFull) {
    Remove-Item -LiteralPath $stagingFull -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingFull -Force | Out-Null

foreach ($name in @(
    "ApexSenseBridge.exe",
    "ApexSenseBridgeControl.exe",
    "ApexSenseBridgeTray.exe",
    "ApexSenseBridgeTray.exe.config",
    "libVIIPER.dll",
    "viiper.exe"
)) {
    Copy-RequiredFile (Join-Path $releaseDir $name) (Join-Path $stagingFull $name)
}

Copy-RequiredFile (Join-Path $projectRoot "data\supported_games.json") `
    (Join-Path $stagingFull "Data\supported_games.json")
Copy-RequiredFile (Join-Path $projectRoot "assets\app.ico") `
    (Join-Path $stagingFull "Resources\app.ico")

foreach ($name in @("VIIPER-LICENSE.txt", "VIIPER-SOURCE.txt",
                    "VIIPER-v0.7.0-asb.patch")) {
    Copy-RequiredFile (Join-Path $releaseDir $name) (Join-Path $stagingFull "Licenses\$name")
}
Copy-RequiredFile (Join-Path $projectRoot "LICENSE") `
    (Join-Path $stagingFull "Licenses\ApexSenseBridge-LICENSE.txt")
Copy-RequiredFile (Join-Path $projectRoot "THIRD_PARTY_NOTICES.md") `
    (Join-Path $stagingFull "Licenses\THIRD_PARTY_NOTICES.md")
Copy-RequiredFile (Join-Path $projectRoot "installer\driver-manifest.json") `
    (Join-Path $stagingFull "Licenses\driver-manifest.json")

foreach ($name in @(
    "USBip-0.9.7.7-x64.exe",
    "HidHide_1.5.230_x64.exe",
    "USBIP-WIN2-LICENSE.txt",
    "HIDHIDE-LICENSE.txt"
)) {
    Copy-RequiredFile (Join-Path $projectRoot "third_party\prerequisites\$name") `
        (Join-Path $stagingFull "Drivers\$name")
}

foreach ($name in @(
    "Install-Drivers.cmd",
    "Install-Drivers.ps1",
    "Start-ApexSenseBridge.cmd",
    "README-PORTABLE.txt"
)) {
    Copy-RequiredFile (Join-Path $projectRoot "portable\$name") (Join-Path $stagingFull $name)
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -LiteralPath $stagingFull -DestinationPath $zipPath -CompressionLevel Optimal

if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
    Fail "ZIP creation succeeded without producing $zipPath"
}

Write-Host ""
Write-Host "Portable package ready:" -ForegroundColor Green
Write-Host "  $zipPath"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash)"
