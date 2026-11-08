[CmdletBinding()]
param(
    [string]$IsccPath = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$installerScript = Join-Path $projectRoot "installer\ApexSenseBridge.iss"
$signPathRoot = Join-Path $projectRoot "build-signpath"
$cacheDirectory = Join-Path $signPathRoot "signed-uninstaller-cache"
$stagingDirectory = Join-Path $signPathRoot "unsigned-uninstaller"
$cacheNameFile = Join-Path $signPathRoot "uninstaller-cache-name.txt"

function Fail([string]$Message) {
    throw "ApexSenseBridge SignPath uninstaller: $Message"
}

if ([string]::IsNullOrWhiteSpace($IsccPath)) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($IsccPath) -or
    -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    Fail "Inno Setup 6 compiler not found. Install it or pass -IsccPath."
}

foreach ($directory in @($cacheDirectory, $stagingDirectory)) {
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Remove-Item -LiteralPath $cacheNameFile -Force -ErrorAction SilentlyContinue

# Inno Setup intentionally stops the first compilation after creating the
# external uninstaller image. SignPath signs that image before the real build.
& $IsccPath "/DSignPathBuild" $installerScript
if ($LASTEXITCODE -eq 0) {
    Fail "Inno Setup unexpectedly completed without requesting an external uninstaller signature"
}

$candidates = @(Get-ChildItem -LiteralPath $cacheDirectory -File -Filter "*.e32")
if ($candidates.Count -ne 1) {
    Fail "expected exactly one generated .e32 uninstaller, found $($candidates.Count)"
}
$signature = Get-AuthenticodeSignature -LiteralPath $candidates[0].FullName
if ($signature.SignerCertificate) {
    Fail "generated uninstaller is already signed unexpectedly"
}

Copy-Item -LiteralPath $candidates[0].FullName `
    -Destination (Join-Path $stagingDirectory "ApexSenseBridge-Uninstaller.exe") -Force
[System.IO.File]::WriteAllText(
    $cacheNameFile,
    $candidates[0].Name,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host "SignPath unsigned uninstaller ready: $stagingDirectory" -ForegroundColor Green
