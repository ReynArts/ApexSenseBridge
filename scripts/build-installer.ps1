param(
    [string]$PlayniteInstallDir = "",
    [string]$PlayniteSdkPath = "",
    [string]$ToolboxPath = "",
    [string]$IsccPath = "",
    [switch]$SkipBuild,
    [switch]$SignPath,
    [switch]$Sign,
    [switch]$RequireSigning
)

$ErrorActionPreference = "Stop"
$global:LASTEXITCODE = 0

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "build-win\Release"
$prerequisiteDir = Join-Path $projectRoot "third_party\prerequisites"
$installerScript = Join-Path $projectRoot "installer\ApexSenseBridge.iss"
$distDir = Join-Path $projectRoot "dist"

$requiredPackages = @(
    @{
        Name = "USBip-0.9.8.0-x64.exe"
        Sha256 = "81F426741F7EE2ED991FEBE24A22DACA8400B6AE2F171054E3FB404897E15D39"
    },
    @{
        Name = "HidHide_1.5.230_x64.exe"
        Sha256 = "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6"
    }
)

function Fail([string]$Message) {
    throw "ApexSenseBridge installer: $Message"
}

$signingRequested = $Sign -or $RequireSigning
if ($SignPath -and $signingRequested) {
    Fail "-SignPath cannot be combined with local certificate signing."
}
$signingIdentityCount = @(
    -not [string]::IsNullOrWhiteSpace($env:ASB_SIGNING_CERTIFICATE_PATH),
    -not [string]::IsNullOrWhiteSpace($env:ASB_SIGNING_CERTIFICATE_BASE64),
    -not [string]::IsNullOrWhiteSpace($env:ASB_SIGNING_CERTIFICATE_THUMBPRINT)
) | Where-Object { $_ } | Measure-Object | Select-Object -ExpandProperty Count
if ($RequireSigning -and $signingIdentityCount -eq 0) {
    Fail "signing is required, but no signing certificate is configured"
}
if ($signingRequested -and $signingIdentityCount -ne 1) {
    Fail "configure exactly one ASB signing identity before requesting signing"
}

# Windows keeps a running executable locked. Stop the standalone Tray before
# replacing the canonical build payload during a release build.
if (-not $SkipBuild) {
    Stop-Process -Name "ApexSenseBridgeTray" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
}

# A release build owns dist completely. Recreate it so stale binaries, old PEXT
# packages, and expanded portable directories can never coexist with the three
# publishable artifacts.
$projectFull = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
$distFull = [System.IO.Path]::GetFullPath($distDir).TrimEnd('\')
if (-not $distFull.StartsWith($projectFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to clean an unexpected dist directory: $distFull"
}
if (Test-Path -LiteralPath $distFull) {
    Remove-Item -LiteralPath $distFull -Recurse -Force
}
New-Item -ItemType Directory -Path $distFull -Force | Out-Null

foreach ($package in $requiredPackages) {
    $path = Join-Path $prerequisiteDir $package.Name
    if (-not (Test-Path -LiteralPath $path)) {
        Fail "offline prerequisite missing: $path"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $package.Sha256) {
        Fail "$($package.Name) SHA-256 mismatch. Expected $($package.Sha256), got $actual"
    }
}

if (-not $SkipBuild) {
    Write-Host "Building the statically linked native engine and control panel..."
    & (Join-Path $PSScriptRoot "build-windows.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "native build failed" }
} else {
    Write-Host "Reusing the existing signed release payload..."
}

Write-Host $(if ($SkipBuild) { "Packing the signed Playnite extension payload..." } else { "Building the Playnite extension payload..." })
& (Join-Path $PSScriptRoot "build-playnite-extension.ps1") `
    -PlayniteInstallDir $PlayniteInstallDir `
    -PlayniteSdkPath $PlayniteSdkPath `
    -ToolboxPath $ToolboxPath `
    -SkipBuild:$SkipBuild `
    -Sign:$signingRequested
if ($LASTEXITCODE -ne 0) { Fail "Playnite extension build failed" }

if (-not $SkipBuild) {
    Write-Host "Building the Standalone Tray application..."
    & (Join-Path $PSScriptRoot "build-tray-app.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "Tray app build failed" }

    Write-Host "Building the elevated updater..."
    & (Join-Path $PSScriptRoot "build-updater.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "Updater build failed" }
}

foreach ($name in @("ApexSenseBridge.exe", "ApexSenseBridgeControl.exe", "ApexSenseBridgeTray.exe",
                    "ApexSenseBridgeUpdater.exe", "libVIIPER.dll", "viiper.exe", "VIIPER-LICENSE.txt", "VIIPER-SOURCE.txt",
                    "VIIPER-v0.7.0-asb.patch")) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseDir $name) -PathType Leaf)) {
        Fail "$name is missing from $releaseDir. Build the pinned release payloads first."
    }
}

if ($signingRequested) {
    Write-Host "Signing the Windows release payload..."
    $payloadsToSign = @(
        "ApexSenseBridge.exe",
        "ApexSenseBridgeControl.exe",
        "ApexSenseBridgeTray.exe",
        "ApexSenseBridgeUpdater.exe",
        "viiper.exe",
        "libVIIPER.dll"
    ) | ForEach-Object { Join-Path $releaseDir $_ }
    & (Join-Path $PSScriptRoot "sign-windows-artifacts.ps1") -Path $payloadsToSign
    if ($LASTEXITCODE -ne 0) { Fail "release payload signing failed" }

}

if ([string]::IsNullOrWhiteSpace($IsccPath)) {
    $isccCandidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    $IsccPath = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($IsccPath) -or
    -not (Test-Path -LiteralPath $IsccPath)) {
    Fail "Inno Setup 6 compiler not found. Install it or pass -IsccPath."
}

Write-Host "Compiling the single offline installer..."
$isccArguments = @()
if ($signingRequested) {
    $signScript = Join-Path $PSScriptRoot "sign-windows-artifacts.ps1"
    $signToolDefinition = "/Sasbsign=powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `$q$signScript`$q -Path `$f"
    $isccArguments += "/DSignBuild"
    $isccArguments += $signToolDefinition
}
if ($SignPath) {
    $isccArguments += "/DSignPathBuild"
}
$isccArguments += $installerScript
& $IsccPath @isccArguments
if ($LASTEXITCODE -ne 0) { Fail "Inno Setup compilation failed" }

$setup = Join-Path $projectRoot "dist\ApexSenseBridge-Setup.exe"
if (-not (Test-Path -LiteralPath $setup)) {
    Fail "Inno Setup succeeded but $setup was not created"
}
if ($signingRequested) {
    & (Join-Path $PSScriptRoot "sign-windows-artifacts.ps1") -Path $setup -VerifyOnly
    if ($LASTEXITCODE -ne 0) { Fail "installer signature verification failed" }
}

Write-Host "Building the portable ZIP from the verified release payload..."
& (Join-Path $PSScriptRoot "build-portable.ps1") -SkipBuild
if ($LASTEXITCODE -ne 0) { Fail "portable package build failed" }

$portableZip = Join-Path $projectRoot "dist\ApexSenseBridge-Portable.zip"
if (-not (Test-Path -LiteralPath $portableZip)) {
    Fail "portable build succeeded but $portableZip was not created"
}

Write-Host "Building the update package from the verified release payload..."
& (Join-Path $PSScriptRoot "build-update-package.ps1")
if ($LASTEXITCODE -ne 0) { Fail "update package build failed" }

$updatePackage = Join-Path $projectRoot "dist\ApexSenseBridge-Update.zip"
if (-not (Test-Path -LiteralPath $updatePackage)) {
    Fail "update package build succeeded but $updatePackage was not created"
}

$checksumPath = Join-Path $distFull "SHA256SUMS.txt"
& (Join-Path $PSScriptRoot "finalize-release-artifacts.ps1")
if ($LASTEXITCODE -ne 0) { Fail "release checksum generation failed" }

Write-Host ""
Write-Host "Release packages ready:" -ForegroundColor Green
Write-Host "  $setup"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash)"
Write-Host "  $portableZip"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $portableZip -Algorithm SHA256).Hash)"
Write-Host "  $updatePackage"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $updatePackage -Algorithm SHA256).Hash)"
Write-Host "  $checksumPath"
