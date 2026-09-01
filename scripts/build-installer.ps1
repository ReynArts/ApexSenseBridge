param(
    [string]$PlayniteInstallDir = "",
    [string]$PlayniteSdkPath = "",
    [string]$ToolboxPath = "",
    [string]$IsccPath = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "build-win\Release"
$prerequisiteDir = Join-Path $projectRoot "third_party\prerequisites"
$installerScript = Join-Path $projectRoot "installer\ApexSenseBridge.iss"

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

foreach ($name in @("ApexSenseBridge.exe", "ApexSenseBridgeControl.exe", "viiper.exe",
                    "VIIPER-LICENSE.txt", "VIIPER-SOURCE.txt")) {
    if (-not (Test-Path -LiteralPath (Join-Path $releaseDir $name))) {
        Fail "$name is missing from $releaseDir. Build the pinned patched VIIPER payload first."
    }
}

Write-Host "Building the Playnite extension payload..."
& (Join-Path $PSScriptRoot "build-playnite-extension.ps1") `
    -PlayniteInstallDir $PlayniteInstallDir `
    -PlayniteSdkPath $PlayniteSdkPath `
    -ToolboxPath $ToolboxPath
if ($LASTEXITCODE -ne 0) { Fail "Playnite extension build failed" }

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
& $IsccPath $installerScript
if ($LASTEXITCODE -ne 0) { Fail "Inno Setup compilation failed" }

$setup = Join-Path $projectRoot "dist\ApexSenseBridge-Setup.exe"
if (-not (Test-Path -LiteralPath $setup)) {
    Fail "Inno Setup succeeded but $setup was not created"
}

Write-Host ""
Write-Host "Offline installer ready:" -ForegroundColor Green
Write-Host "  $setup"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash)"
