param(
    [string]$OutputPath = "",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$sourceDirectory = Join-Path $projectRoot ".tmp-libviiper-build-source"
$patchName = "viiper-v0.9.0-asb.patch"
$patchPath = Join-Path $projectRoot "third_party\viiper-patches\$patchName"
$toolsDirectory = Join-Path $projectRoot ".tools\libviiper-build"
$baseTag = "v0.9.0"
$expectedCommit = "ef817d51ca1175c86c6c485c0bf52822c5d6ae49"
$version = "v0.9.0-asb2"

$goVersion = "1.27.2"
$goArchiveName = "go$goVersion.windows-amd64.zip"
$goArchiveHash = "1314008898BD40DF77AF4B014F777F08873DBDFBCD3D92308728EE03304FE04F"
$goArchiveUrl = "https://go.dev/dl/$goArchiveName"
$llvmDirectoryName = "llvm-mingw-20260826-ucrt-x86_64"
$llvmArchiveName = "$llvmDirectoryName.zip"
$llvmArchiveHash = "AE601F4E0F72BBDF441AD2DF8BB16F037E2E9251559EA6B37B4057AEF39C06C3"
$llvmArchiveUrl = "https://github.com/mstorsjo/llvm-mingw/releases/download/20260826/$llvmArchiveName"

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "build-win\Release\libVIIPER.dll"
} else {
    $OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
}

function Assert-UnderDirectory([string]$Path, [string]$Parent) {
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\')
    if (-not $fullPath.StartsWith($fullParent + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify unexpected path: $fullPath"
    }
}

function Get-PinnedArchive(
    [string]$Url,
    [string]$Path,
    [string]$ExpectedHash
) {
    Assert-UnderDirectory $Path $toolsDirectory
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($hash -eq $ExpectedHash) { return }
        Remove-Item -LiteralPath $Path -Force
    }

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) {
        throw "curl.exe is required to download the pinned portable build toolchains."
    }
    & $curl.Source --location --fail --retry 3 --output $Path $Url
    if ($LASTEXITCODE -ne 0) {
        throw "Could not download $Url"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedHash) {
        throw "Downloaded archive hash mismatch for $Path. Expected $ExpectedHash, got $actualHash"
    }
}

function Expand-PinnedArchive(
    [string]$Archive,
    [string]$ExpectedExecutable,
    [string]$ExtractedDirectory
) {
    if (Test-Path -LiteralPath $ExpectedExecutable -PathType Leaf) { return }
    Assert-UnderDirectory $ExtractedDirectory $toolsDirectory
    if (Test-Path -LiteralPath $ExtractedDirectory) {
        Remove-Item -LiteralPath $ExtractedDirectory -Recurse -Force
    }
    Expand-Archive -LiteralPath $Archive -DestinationPath $toolsDirectory
    if (-not (Test-Path -LiteralPath $ExpectedExecutable -PathType Leaf)) {
        throw "Portable toolchain extraction did not produce $ExpectedExecutable"
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to fetch the pinned VIIPER source."
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Missing VIIPER patch: $patchPath"
}

New-Item -ItemType Directory -Path $toolsDirectory -Force | Out-Null
$goArchive = Join-Path $toolsDirectory $goArchiveName
$llvmArchive = Join-Path $toolsDirectory $llvmArchiveName
$goExecutable = Join-Path $toolsDirectory "go\bin\go.exe"
$gofmtExecutable = Join-Path $toolsDirectory "go\bin\gofmt.exe"
$llvmDirectory = Join-Path $toolsDirectory $llvmDirectoryName
$clangExecutable = Join-Path $llvmDirectory "bin\clang.exe"
$clangxxExecutable = Join-Path $llvmDirectory "bin\clang++.exe"

Get-PinnedArchive $goArchiveUrl $goArchive $goArchiveHash
Get-PinnedArchive $llvmArchiveUrl $llvmArchive $llvmArchiveHash
Expand-PinnedArchive $goArchive $goExecutable (Join-Path $toolsDirectory "go")
Expand-PinnedArchive $llvmArchive $clangExecutable $llvmDirectory

if (Test-Path -LiteralPath $sourceDirectory) {
    throw "Temporary libVIIPER source already exists: $sourceDirectory`nRemove it after checking the path, then rerun this script."
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

$previousPath = $env:PATH
$previousCgo = $env:CGO_ENABLED
$previousCc = $env:CC
$previousCxx = $env:CXX
$previousGoToolchain = $env:GOTOOLCHAIN
$previousGoCache = $env:GOCACHE
$previousGoModCache = $env:GOMODCACHE
try {
    $env:PATH = "$(Split-Path -Parent $goExecutable);$(Split-Path -Parent $clangExecutable);$env:PATH"
    $env:CGO_ENABLED = "1"
    $env:CC = $clangExecutable
    $env:CXX = $clangxxExecutable
    $env:GOTOOLCHAIN = "local"
    $env:GOCACHE = Join-Path $toolsDirectory "gocache"
    $env:GOMODCACHE = Join-Path $toolsDirectory "gomodcache"

    Push-Location $sourceDirectory
    try {
        if (-not $SkipTests) {
            & $goExecutable test ./device/dualsense/... ./internal/server/usb/...
            if ($LASTEXITCODE -ne 0) { throw "libVIIPER DualSense tests failed." }
        }

        $outputDirectory = Split-Path -Parent $OutputPath
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        & $goExecutable build -buildmode=c-shared -trimpath -buildvcs=false `
            -ldflags "-s -w -buildid=asb-libviiper-$version" `
            -o $OutputPath ./lib/viiper
        if ($LASTEXITCODE -ne 0) { throw "libVIIPER compilation failed." }
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $previousPath
    $env:CGO_ENABLED = $previousCgo
    $env:CC = $previousCc
    $env:CXX = $previousCxx
    $env:GOTOOLCHAIN = $previousGoToolchain
    $env:GOCACHE = $previousGoCache
    $env:GOMODCACHE = $previousGoModCache
}

$outputDirectory = Split-Path -Parent $OutputPath
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
$patchHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $patchPath).Hash
Copy-Item -LiteralPath (Join-Path $sourceDirectory "LICENSE.txt") `
    -Destination (Join-Path $outputDirectory "VIIPER-LICENSE.txt") -Force
Copy-Item -LiteralPath $patchPath `
    -Destination (Join-Path $outputDirectory "VIIPER-v0.9.0-asb.patch") -Force
@(
    "These VIIPER release payloads were built reproducibly for ApexSenseBridge."
    ""
    "Upstream: https://github.com/Alia5/VIIPER.git"
    "Base tag: $baseTag"
    "Base commit: $expectedCommit"
    "Integrated library version: $version"
    "Fallback sidecar version: $version"
    "Patch: third_party/viiper-patches/$patchName"
    "Patch SHA-256: $patchHash"
    "Go toolchain: $goVersion ($goArchiveHash)"
    "LLVM-MinGW toolchain: 20260826 ($llvmArchiveHash)"
    "Virtual DualSense firmware feature report: 0x0630"
) | Set-Content -LiteralPath (Join-Path $outputDirectory "VIIPER-SOURCE.txt") `
    -Encoding UTF8

Write-Host "Built official integrated backend ${version}:"
Write-Host "  $OutputPath"
Write-Host "  SHA256 $hash"
