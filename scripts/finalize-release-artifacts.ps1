[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$distDirectory = Join-Path $projectRoot "dist"

function Fail([string]$Message) {
    throw "ApexSenseBridge release artifacts: $Message"
}

$setup = Join-Path $distDirectory "ApexSenseBridge-Setup.exe"
$portable = Join-Path $distDirectory "ApexSenseBridge-Portable.zip"
$updatePackage = Join-Path $distDirectory "ApexSenseBridge-Update.zip"
$playnitePackages = @(Get-ChildItem -LiteralPath $distDirectory -Filter "ApexSenseBridge-Playnite-*.pext" -File)
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    Fail "installer is missing: $setup"
}
if (-not (Test-Path -LiteralPath $portable -PathType Leaf)) {
    Fail "portable package is missing: $portable"
}
if (-not (Test-Path -LiteralPath $updatePackage -PathType Leaf)) {
    Fail "update package is missing: $updatePackage"
}
if ($playnitePackages.Count -ne 1) {
    Fail "expected exactly one versioned Playnite package, found $($playnitePackages.Count)"
}

$artifacts = @($setup, $portable, $updatePackage, $playnitePackages[0].FullName)
$checksumPath = Join-Path $distDirectory "SHA256SUMS.txt"
$checksumLines = $artifacts |
    Sort-Object { Split-Path -Leaf $_ } |
    ForEach-Object {
        $name = Split-Path -Leaf $_
        $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
        "$hash  $name"
    }
[System.IO.File]::WriteAllLines(
    $checksumPath, $checksumLines, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Release checksums refreshed: $checksumPath" -ForegroundColor Green

