[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$stagingRoot = Join-Path $projectRoot "build-signpath"
$stagingDirectory = Join-Path $stagingRoot "unsigned-payload"
$releaseDirectory = Join-Path $projectRoot "build-win\Release"
$playniteDirectory = Join-Path $projectRoot "playnite\ApexSenseBridge\bin\Release"

function Fail([string]$Message) {
    throw "ApexSenseBridge SignPath payload: $Message"
}

$projectFull = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
$stagingFull = [System.IO.Path]::GetFullPath($stagingDirectory).TrimEnd('\')
if (-not $stagingFull.StartsWith($projectFull + "\build-signpath\", [StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to replace unexpected staging directory: $stagingFull"
}
if (Test-Path -LiteralPath $stagingFull) {
    Remove-Item -LiteralPath $stagingFull -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingFull -Force | Out-Null

$payloads = @(
    @{ Source = Join-Path $releaseDirectory "ApexSenseBridge.exe"; Name = "ApexSenseBridge.exe" },
    @{ Source = Join-Path $releaseDirectory "ApexSenseBridgeControl.exe"; Name = "ApexSenseBridgeControl.exe" },
    @{ Source = Join-Path $releaseDirectory "ApexSenseBridgeTray.exe"; Name = "ApexSenseBridgeTray.exe" },
    @{ Source = Join-Path $releaseDirectory "viiper.exe"; Name = "viiper.exe" },
    @{ Source = Join-Path $releaseDirectory "libVIIPER.dll"; Name = "libVIIPER.dll" },
    @{ Source = Join-Path $playniteDirectory "ApexSenseBridge.dll"; Name = "ApexSenseBridge.Playnite.dll" }
)

foreach ($payload in $payloads) {
    if (-not (Test-Path -LiteralPath $payload.Source -PathType Leaf)) {
        Fail "required unsigned payload is missing: $($payload.Source)"
    }
    Copy-Item -LiteralPath $payload.Source -Destination (Join-Path $stagingFull $payload.Name) -Force
}

$stagedNames = @(Get-ChildItem -LiteralPath $stagingFull -File | Select-Object -ExpandProperty Name | Sort-Object)
$expectedNames = @($payloads.Name | Sort-Object)
if (($stagedNames -join "`n") -ne ($expectedNames -join "`n")) {
    Fail "the staged payload does not exactly match the SignPath artifact configuration"
}

Write-Host "SignPath unsigned payload ready: $stagingFull" -ForegroundColor Green

