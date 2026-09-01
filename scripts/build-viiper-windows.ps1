param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDirectory = Join-Path $projectRoot ".tmp-viiper-build-source"
$patchPath = Join-Path $projectRoot "third_party\viiper-patches\viiper-v0.6.1-dualsense.patch"
$expectedCommit = "904bef3a308c13e3c20e57597635d6fd19f4bb58"
$version = "v0.6.1-steamless9"

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "build-win\Release\viiper.exe"
} else {
    $OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to fetch the pinned VIIPER source."
}
if (-not (Get-Command go -ErrorAction SilentlyContinue)) {
    throw "Go 1.26.2 or newer is required to build the VIIPER sidecar."
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Missing VIIPER patch: $patchPath"
}
if (Test-Path -LiteralPath $sourceDirectory) {
    throw "Temporary VIIPER source already exists: $sourceDirectory`nRemove it after checking the path, then rerun this script."
}

git clone --depth 1 --branch v0.6.1 https://github.com/Alia5/VIIPER.git $sourceDirectory
if ($LASTEXITCODE -ne 0) { throw "Could not clone VIIPER v0.6.1." }

$actualCommit = (git -C $sourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "Unexpected VIIPER source commit: $actualCommit"
}

git -C $sourceDirectory apply --check $patchPath
if ($LASTEXITCODE -ne 0) { throw "The ApexSenseBridge VIIPER patch no longer applies cleanly." }
git -C $sourceDirectory apply $patchPath
if ($LASTEXITCODE -ne 0) { throw "Could not apply the ApexSenseBridge VIIPER patch." }

Push-Location $sourceDirectory
try {
    go test ./...
    if ($LASTEXITCODE -ne 0) { throw "VIIPER tests failed." }

    $outputDirectory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $linkerFlags = "-s -w " +
        "-X main.Version=$version " +
        "-X main.Commit=904bef3+asb-fw0630 " +
        "-X main.Date=2026-09-01 " +
        "-X github.com/Alia5/VIIPER/internal/codegen/common.Version=$version"
    go build -trimpath -ldflags $linkerFlags -o $OutputPath ./cmd/viiper
    if ($LASTEXITCODE -ne 0) { throw "VIIPER compilation failed." }
} finally {
    Pop-Location
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
$outputDirectory = Split-Path -Parent $OutputPath
Copy-Item -LiteralPath (Join-Path $sourceDirectory "LICENSE.txt") `
    -Destination (Join-Path $outputDirectory "VIIPER-LICENSE.txt") -Force
@(
    "This viiper.exe was built reproducibly for ApexSenseBridge."
    ""
    "Upstream: https://github.com/Alia5/VIIPER.git"
    "Base tag: v0.6.1"
    "Base commit: $expectedCommit"
    "Local version: $version"
    "Patch: third_party/viiper-patches/viiper-v0.6.1-dualsense.patch"
    "Virtual DualSense firmware feature report: 0x0630"
) | Set-Content -LiteralPath (Join-Path $outputDirectory "VIIPER-SOURCE.txt") `
    -Encoding UTF8
Write-Host "Built $version with virtual DualSense firmware 0x0630:"
Write-Host "  $OutputPath"
Write-Host "  SHA256 $hash"
