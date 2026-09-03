param(
    [string]$PlayniteInstallDir = "",
    [string]$PlayniteSdkPath = "",
    [string]$ToolboxPath = "",
    [string]$IsccPath = "",
    [switch]$Sign,
    [switch]$RequireSigning
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "build-win\Release"
$prerequisiteDir = Join-Path $projectRoot "third_party\prerequisites"
$installerScript = Join-Path $projectRoot "installer\ApexSenseBridge.iss"
$distDir = Join-Path $projectRoot "dist"

$requiredPackages = @(
    @{
        Name = "USBip-0.9.7.7-x64.exe"
        Sha256 = "51620FA5F9F8BE5932BC9D786DEEE557CE06D5407A99CAB490DCFAC71F185FEA"
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

# A release build owns these outputs. Remove them up front so an older PEXT or
# staging directory cannot accidentally be published beside the current build.
$projectFull = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
$distFull = [System.IO.Path]::GetFullPath($distDir).TrimEnd('\')
if (-not $distFull.StartsWith($projectFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to clean an unexpected dist directory: $distFull"
}
New-Item -ItemType Directory -Path $distFull -Force | Out-Null
foreach ($name in @(
    "ApexSenseBridge-Setup.exe",
    "ApexSenseBridge-Portable.zip",
    "ApexSenseBridgeTray.exe",
    "ApexSenseBridgeTray.exe.config",
    "SHA256SUMS.txt"
)) {
    $ownedOutput = Join-Path $distFull $name
    if (Test-Path -LiteralPath $ownedOutput) {
        Remove-Item -LiteralPath $ownedOutput -Force
    }
}
$portableStaging = Join-Path $distFull "ApexSenseBridge-Portable"
if (Test-Path -LiteralPath $portableStaging) {
    Remove-Item -LiteralPath $portableStaging -Recurse -Force
}
Get-ChildItem -LiteralPath $distFull -Filter "ApexSenseBridge_*.pext" -File `
    -ErrorAction SilentlyContinue | Remove-Item -Force

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

Write-Host "Building the statically linked native engine and control panel..."
& (Join-Path $PSScriptRoot "build-windows.ps1")
if ($LASTEXITCODE -ne 0) { Fail "native build failed" }

foreach ($name in @("ApexSenseBridge.exe", "ApexSenseBridgeControl.exe", "libVIIPER.dll", "viiper.exe",
                    "VIIPER-LICENSE.txt", "VIIPER-SOURCE.txt",
                    "VIIPER-v0.7.0-asb.patch")) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseDir $name))) {
        Fail "$name is missing from $releaseDir. Build the pinned patched VIIPER payloads first."
    }
}

Write-Host "Building the Playnite extension payload..."
& (Join-Path $PSScriptRoot "build-playnite-extension.ps1") `
    -PlayniteInstallDir $PlayniteInstallDir `
    -PlayniteSdkPath $PlayniteSdkPath `
    -ToolboxPath $ToolboxPath `
    -Sign:$signingRequested
if ($LASTEXITCODE -ne 0) { Fail "Playnite extension build failed" }

Write-Host "Building the Standalone Tray application..."
& (Join-Path $PSScriptRoot "build-tray-app.ps1")
if ($LASTEXITCODE -ne 0) { Fail "Tray app build failed" }

if ($signingRequested) {
    Write-Host "Signing the Windows release payload..."
    $payloadsToSign = @(
        "ApexSenseBridge.exe",
        "ApexSenseBridgeControl.exe",
        "ApexSenseBridgeTray.exe",
        "viiper.exe",
        "libVIIPER.dll"
    ) | ForEach-Object { Join-Path $releaseDir $_ }
    & (Join-Path $PSScriptRoot "sign-windows-artifacts.ps1") -Path $payloadsToSign
    if ($LASTEXITCODE -ne 0) { Fail "release payload signing failed" }

    # build-tray-app copies the unsigned executable before this signing stage.
    # Replace that public standalone copy with the signed release payload.
    Copy-Item -LiteralPath (Join-Path $releaseDir "ApexSenseBridgeTray.exe") `
        -Destination (Join-Path $distFull "ApexSenseBridgeTray.exe") -Force
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

$releaseArtifactPaths = @(
    $setup,
    $portableZip,
    (Join-Path $distFull "ApexSenseBridgeTray.exe"),
    (Join-Path $distFull "ApexSenseBridgeTray.exe.config")
)
$releaseArtifactPaths += @(
    Get-ChildItem -LiteralPath $distFull -Filter "ApexSenseBridge_*.pext" -File |
        Select-Object -ExpandProperty FullName
)
foreach ($artifactPath in $releaseArtifactPaths) {
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        Fail "release artifact is missing: $artifactPath"
    }
}
$checksumPath = Join-Path $distFull "SHA256SUMS.txt"
$checksumLines = $releaseArtifactPaths |
    Sort-Object { Split-Path -Leaf $_ } |
    ForEach-Object {
        $name = Split-Path -Leaf $_
        $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
        "$hash  $name"
    }
[System.IO.File]::WriteAllLines(
    $checksumPath, $checksumLines, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ""
Write-Host "Release packages ready:" -ForegroundColor Green
Write-Host "  $setup"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash)"
Write-Host "  $portableZip"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $portableZip -Algorithm SHA256).Hash)"
Write-Host "  $checksumPath"
