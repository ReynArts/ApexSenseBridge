[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SignedDirectory
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDirectory = Join-Path $projectRoot "build-win\Release"
$playniteDirectory = Join-Path $projectRoot "playnite\ApexSenseBridge\bin\Release"

function Fail([string]$Message) {
    throw "ApexSenseBridge SignPath payload: $Message"
}

if (-not (Test-Path -LiteralPath $SignedDirectory -PathType Container)) {
    Fail "signed artifact directory is missing: $SignedDirectory"
}
$signedRoot = [System.IO.Path]::GetFullPath($SignedDirectory)

$payloads = @(
    @{ Name = "ApexSenseBridge.exe"; Destination = Join-Path $releaseDirectory "ApexSenseBridge.exe" },
    @{ Name = "ApexSenseBridgeControl.exe"; Destination = Join-Path $releaseDirectory "ApexSenseBridgeControl.exe" },
    @{ Name = "ApexSenseBridgeTray.exe"; Destination = Join-Path $releaseDirectory "ApexSenseBridgeTray.exe" },
    @{ Name = "viiper.exe"; Destination = Join-Path $releaseDirectory "viiper.exe" },
    @{ Name = "libVIIPER.dll"; Destination = Join-Path $releaseDirectory "libVIIPER.dll" },
    @{ Name = "ApexSenseBridge.Playnite.dll"; Destination = Join-Path $playniteDirectory "ApexSenseBridge.dll" }
)

$publisher = $null
foreach ($payload in $payloads) {
    $matches = @(Get-ChildItem -LiteralPath $signedRoot -Recurse -File -Filter $payload.Name)
    if ($matches.Count -ne 1) {
        Fail "expected exactly one signed $($payload.Name), found $($matches.Count)"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $matches[0].FullName
    if (-not $signature.SignerCertificate -or
        $signature.Status -in @(
            [System.Management.Automation.SignatureStatus]::NotSigned,
            [System.Management.Automation.SignatureStatus]::HashMismatch)) {
        Fail "$($payload.Name) does not contain an intact Authenticode signature ($($signature.Status))"
    }
    if ($null -eq $publisher) {
        $publisher = $signature.SignerCertificate.Subject
    } elseif ($signature.SignerCertificate.Subject -ne $publisher) {
        Fail "$($payload.Name) is signed by a different publisher"
    }
    $destinationDirectory = Split-Path -Parent $payload.Destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $matches[0].FullName -Destination $payload.Destination -Force
}

Write-Host "Applied SignPath payload signed by $publisher" -ForegroundColor Green

