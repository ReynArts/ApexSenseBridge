param(
    [string]$OutputPath = "",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDirectory = Join-Path $projectRoot ".tmp-viiper-build-source"
$patchName = "viiper-v0.9.0-asb.patch"
$patchPath = Join-Path $projectRoot "third_party\viiper-patches\$patchName"
$baseTag = "v0.9.0"
$expectedCommit = "ef817d51ca1175c86c6c485c0bf52822c5d6ae49"
$version = "v0.9.0-asb2"
$linkerCommit = "ef817d5+asb2"
$linkerDate = "2026-11-09"

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "build-win\Release\viiper.exe"
} else {
    $OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to fetch the pinned VIIPER source."
}
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    throw "Go is required to build the VIIPER sidecar."
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Missing VIIPER patch: $patchPath"
}
if (Test-Path -LiteralPath $sourceDirectory) {
    throw "Temporary VIIPER source already exists: $sourceDirectory`nRemove it after checking the path, then rerun this script."
}

git clone --depth 1 --branch $baseTag https://github.com/Alia5/VIIPER.git $sourceDirectory
if ($LASTEXITCODE -ne 0) { throw "Could not clone VIIPER $baseTag." }

$actualCommit = (git -C $sourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "Unexpected VIIPER source commit: $actualCommit"
}

git -C $sourceDirectory apply --check --ignore-space-change --unidiff-zero $patchPath
if ($LASTEXITCODE -ne 0) { throw "The ApexSenseBridge VIIPER patch no longer applies cleanly." }
git -C $sourceDirectory apply --ignore-space-change --unidiff-zero $patchPath
if ($LASTEXITCODE -ne 0) { throw "Could not apply the ApexSenseBridge VIIPER patch." }

$bundleDirectory = Join-Path $sourceDirectory "internal\install\bundle"
New-Item -ItemType Directory -Path $bundleDirectory -Force | Out-Null
$bundleLicenses = @(
    "ApexSenseBridge VIIPER sidecar $version"
    ""
    "This viiper.exe is VIIPER $baseTag ($expectedCommit) with the ApexSenseBridge patch $patchName."
    "VIIPER is free software licensed under the GNU General Public License, version 3 or (at your option) any later version."
    ""
    "ApexSenseBridge starts it with the server command only. The license texts and the third-party notices for the Go modules linked into this executable are installed with ApexSenseBridge in its Licenses folder (VIIPER-LICENSE.txt and THIRD_PARTY_NOTICES.md)."
    "The patch and the source reference are in the same folder (VIIPER-v0.9.0-asb.patch and VIIPER-SOURCE.txt)."
) -join "`n"
[System.IO.File]::WriteAllText(
    (Join-Path $bundleDirectory "licenses.txt"),
    $bundleLicenses + "`n",
    (New-Object System.Text.UTF8Encoding($false)))

$previousCgo = $env:CGO_ENABLED
Push-Location $sourceDirectory
try {
    $env:CGO_ENABLED = "0"
    if (-not $SkipTests) {
        go test ./device/dualsense/... ./internal/server/usb/...
        if ($LASTEXITCODE -ne 0) { throw "VIIPER DualSense tests failed." }
    }

    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $linkerFlags = "-s -w " +
        "-X main.Version=$version " +
        "-X main.Commit=$linkerCommit " +
        "-X main.Date=$linkerDate " +
        "-X github.com/Alia5/VIIPER/internal/codegen/common.Version=$version"
    go build -trimpath -buildvcs=false -ldflags $linkerFlags -o $OutputPath ./cmd/viiper
    if ($LASTEXITCODE -ne 0) { throw "VIIPER compilation failed." }
} finally {
    $env:CGO_ENABLED = $previousCgo
    Pop-Location
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
$patchHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $patchPath).Hash
$outputDirectory = Split-Path -Parent $OutputPath
Copy-Item -LiteralPath (Join-Path $sourceDirectory "LICENSE.txt") `
    -Destination (Join-Path $outputDirectory "VIIPER-LICENSE.txt") -Force
Copy-Item -LiteralPath $patchPath `
    -Destination (Join-Path $outputDirectory "VIIPER-v0.9.0-asb.patch") -Force
@(
    "This viiper.exe was built reproducibly for ApexSenseBridge."
    ""
    "Upstream: https://github.com/Alia5/VIIPER.git"
    "Base tag: $baseTag"
    "Base commit: $expectedCommit"
    "Local version: $version"
    "Patch: third_party/viiper-patches/$patchName"
    "Patch SHA-256: $patchHash"
    "Virtual DualSense firmware feature report: 0x0630"
) | Set-Content -LiteralPath (Join-Path $outputDirectory "VIIPER-SOURCE.txt") `
    -Encoding UTF8
Write-Host "Built official ApexSenseBridge backend $version with virtual DualSense firmware 0x0630:"
Write-Host "  $OutputPath"
Write-Host "  SHA256 $hash"
