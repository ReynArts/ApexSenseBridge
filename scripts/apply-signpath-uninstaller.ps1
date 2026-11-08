[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SignedDirectory
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$signPathRoot = Join-Path $projectRoot "build-signpath"
$cacheDirectory = Join-Path $signPathRoot "signed-uninstaller-cache"
$cacheNameFile = Join-Path $signPathRoot "uninstaller-cache-name.txt"

function Fail([string]$Message) {
    throw "ApexSenseBridge SignPath uninstaller: $Message"
}

if (-not (Test-Path -LiteralPath $SignedDirectory -PathType Container)) {
    Fail "signed artifact directory is missing: $SignedDirectory"
}
if (-not (Test-Path -LiteralPath $cacheNameFile -PathType Leaf)) {
    Fail "generated uninstaller cache name is missing"
}

$matches = @(Get-ChildItem -LiteralPath $SignedDirectory -Recurse -File `
    -Filter "ApexSenseBridge-Uninstaller.exe")
if ($matches.Count -ne 1) {
    Fail "expected exactly one signed uninstaller, found $($matches.Count)"
}
$signature = Get-AuthenticodeSignature -LiteralPath $matches[0].FullName
if (-not $signature.SignerCertificate -or
    $signature.Status -in @(
        [System.Management.Automation.SignatureStatus]::NotSigned,
        [System.Management.Automation.SignatureStatus]::HashMismatch)) {
    Fail "uninstaller does not contain an intact Authenticode signature ($($signature.Status))"
}

$cacheName = [System.IO.File]::ReadAllText($cacheNameFile).Trim()
if ([System.IO.Path]::GetFileName($cacheName) -ne $cacheName -or
    -not $cacheName.EndsWith(".e32", [StringComparison]::OrdinalIgnoreCase)) {
    Fail "invalid generated uninstaller cache name: $cacheName"
}
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
Copy-Item -LiteralPath $matches[0].FullName `
    -Destination (Join-Path $cacheDirectory $cacheName) -Force

Write-Host "Applied SignPath uninstaller signed by $($signature.SignerCertificate.Subject)" `
    -ForegroundColor Green
