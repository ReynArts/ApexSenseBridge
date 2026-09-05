param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDirectory = Join-Path $projectRoot ".tmp-viiper-070-build-source"
$patchPath = Join-Path $projectRoot "third_party\viiper-patches\viiper-v0.7.0-asb.patch"
$expectedCommit = "6b71b148a2243fab77ee1a46f4e22e00bd7d5a04"
$version = "v0.7.0-asb4"

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "dist\experimental\viiper-v0.7.0-asb4.exe"
} else {
    $OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to fetch the pinned VIIPER source."
}
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    throw "Go 1.26.2 or newer is required to build VIIPER v0.7.0."
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Missing VIIPER patch: $patchPath"
}
if (Test-Path -LiteralPath $sourceDirectory) {
    throw "Temporary VIIPER source already exists: $sourceDirectory`nRemove it after checking the path, then rerun this script."
}

git clone --depth 1 --branch v0.7.0 https://github.com/Alia5/VIIPER.git $sourceDirectory
if ($LASTEXITCODE -ne 0) { throw "Could not clone VIIPER v0.7.0." }

$actualCommit = (git -C $sourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "Unexpected VIIPER source commit: $actualCommit"
}

git -C $sourceDirectory apply --check $patchPath
if ($LASTEXITCODE -ne 0) { throw "The ApexSenseBridge VIIPER v0.7.0 patch no longer applies cleanly." }
git -C $sourceDirectory apply $patchPath
if ($LASTEXITCODE -ne 0) { throw "Could not apply the ApexSenseBridge VIIPER v0.7.0 patch." }

Push-Location $sourceDirectory
try {
    go test ./...
    if ($LASTEXITCODE -ne 0) { throw "VIIPER tests failed." }

    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $linkerFlags = "-s -w " +
        "-X main.Version=$version " +
        "-X main.Commit=6b71b14+asb4 " +
        "-X main.Date=2026-09-01 " +
        "-X github.com/Alia5/VIIPER/internal/codegen/common.Version=$version"
    go build -trimpath -buildvcs=false -ldflags $linkerFlags -o $OutputPath ./cmd/viiper
    if ($LASTEXITCODE -ne 0) { throw "VIIPER compilation failed." }
} finally {
    Pop-Location
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
$patchHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $patchPath).Hash
$outputDirectory = Split-Path -Parent $OutputPath
Copy-Item -LiteralPath (Join-Path $sourceDirectory "LICENSE.txt") `
    -Destination (Join-Path $outputDirectory "VIIPER-v0.7.0-asb4-LICENSE.txt") -Force
Copy-Item -LiteralPath $patchPath `
    -Destination (Join-Path $outputDirectory "VIIPER-v0.7.0-asb.patch") -Force
@(
    "This viiper.exe mirrors the official ApexSenseBridge v0.7 backend build."
    ""
    "Upstream: https://github.com/Alia5/VIIPER.git"
    "Base tag: v0.7.0"
    "Base commit: $expectedCommit"
    "Local version: $version"
    "Patch: third_party/viiper-patches/viiper-v0.7.0-asb.patch"
    "Patch SHA-256: $patchHash"
    "Virtual DualSense firmware feature report: 0x0630"
    "Status: validated backend; the release payload is built by scripts/build-viiper-windows.ps1"
) | Set-Content -LiteralPath (Join-Path $outputDirectory "VIIPER-v0.7.0-asb4-SOURCE.txt") `
    -Encoding UTF8
Write-Host "Built validation copy of $version with virtual DualSense firmware 0x0630:"
Write-Host "  $OutputPath"
Write-Host "  SHA256 $hash"
