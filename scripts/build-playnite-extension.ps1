param(
    [string]$PlayniteInstallDir = "",
    [string]$PlayniteSdkPath = "",
    [string]$ToolboxPath = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $root "playnite\ApexSenseBridge"
$project = Join-Path $projectDir "ApexSenseBridge.csproj"
$output = Join-Path $projectDir "bin\Release"
$dist = Join-Path $root "dist"
$sdkCache = Join-Path $root ".cache\playnite-sdk-6.16.0"
$sdkPackage = Join-Path $sdkCache "playnitesdk.6.16.0.nupkg"
$sdkPackageSha256 = "B83C0553C479894F922E27F638D4DB75F90B8C5B3A5659F6FA85E764A607FF24"

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

if ([string]::IsNullOrWhiteSpace($PlayniteInstallDir)) {
    $installCandidates = @(
        $env:PLAYNITE_INSTALL_DIR,
        (Join-Path $env:ProgramFiles "Playnite"),
        (Join-Path ${env:ProgramFiles(x86)} "Playnite"),
        "G:\Program Files\Playnite"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $PlayniteInstallDir = $installCandidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "Playnite.SDK.dll") } |
        Select-Object -First 1
}

if ([string]::IsNullOrWhiteSpace($PlayniteSdkPath) -and
    -not [string]::IsNullOrWhiteSpace($PlayniteInstallDir)) {
    $PlayniteSdkPath = Join-Path $PlayniteInstallDir "Playnite.SDK.dll"
}
if ([string]::IsNullOrWhiteSpace($ToolboxPath) -and
    -not [string]::IsNullOrWhiteSpace($PlayniteInstallDir)) {
    $ToolboxPath = Join-Path $PlayniteInstallDir "Toolbox.exe"
}

if ([string]::IsNullOrWhiteSpace($PlayniteSdkPath) -or
    -not (Test-Path -LiteralPath $PlayniteSdkPath)) {
    Write-Host "Playnite is not installed; resolving the pinned official PlayniteSDK 6.16.0 package..."
    New-Item -ItemType Directory -Force -Path $sdkCache | Out-Null
    $cachedHash = if (Test-Path -LiteralPath $sdkPackage) {
        (Get-FileHash -LiteralPath $sdkPackage -Algorithm SHA256).Hash
    } else { "" }
    if ($cachedHash -ne $sdkPackageSha256) {
        Invoke-WebRequest -UseBasicParsing `
            -Uri "https://api.nuget.org/v3-flatcontainer/playnitesdk/6.16.0/playnitesdk.6.16.0.nupkg" `
            -OutFile $sdkPackage
    }
    $actualPackageHash = (Get-FileHash -LiteralPath $sdkPackage -Algorithm SHA256).Hash
    if ($actualPackageHash -ne $sdkPackageSha256) {
        Fail "PlayniteSDK package SHA-256 mismatch. Expected $sdkPackageSha256, got $actualPackageHash"
    }
    $expandedSdk = Join-Path $sdkCache "expanded"
    if (-not (Test-Path -LiteralPath $expandedSdk)) {
        New-Item -ItemType Directory -Force -Path $expandedSdk | Out-Null
        $sdkZip = Join-Path $sdkCache "playnitesdk.6.16.0.zip"
        Copy-Item -LiteralPath $sdkPackage -Destination $sdkZip -Force
        Expand-Archive -LiteralPath $sdkZip -DestinationPath $expandedSdk -Force
    }
    $PlayniteSdkPath = Get-ChildItem -LiteralPath $expandedSdk -Recurse -Filter "Playnite.SDK.dll" |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not (Test-Path -LiteralPath $PlayniteSdkPath)) {
    Fail "Playnite.SDK.dll could not be resolved. Pass -PlayniteSdkPath explicitly."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    Fail "vswhere.exe was not found. Install Visual Studio Build Tools."
}

$visualStudio = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) {
    Fail "No Visual Studio installation containing MSBuild was found."
}
$msbuild = Join-Path $visualStudio "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path -LiteralPath $msbuild)) {
    Fail "MSBuild.exe was not found in $visualStudio"
}

Write-Host "Building the Playnite extension..."
& $msbuild $project /t:Rebuild /p:Configuration=Release "/p:PlayniteSdkPath=$PlayniteSdkPath"
if ($LASTEXITCODE -ne 0) {
    Fail "Playnite extension compilation failed."
}

if (-not (Test-Path -LiteralPath $dist)) {
    New-Item -ItemType Directory -Path $dist | Out-Null
}

$manifest = Get-Content -LiteralPath (Join-Path $output "extension.yaml") -Raw
$extensionId = [regex]::Match($manifest, '(?m)^Id:\s*(.+)$').Groups[1].Value.Trim()
$version = [regex]::Match($manifest, '(?m)^Version:\s*(.+)$').Groups[1].Value.Trim()
if ([string]::IsNullOrWhiteSpace($extensionId) -or [string]::IsNullOrWhiteSpace($version)) {
    Fail "extension.yaml does not contain a valid Id and Version."
}
$packageBase = "{0}_{1}" -f $extensionId, ($version -replace '\.', '_')
$packagePath = Join-Path $dist ($packageBase + ".pext")

Write-Host "Packing the Playnite extension..."
if (-not [string]::IsNullOrWhiteSpace($ToolboxPath) -and
    (Test-Path -LiteralPath $ToolboxPath)) {
    & $ToolboxPath pack $output $dist
    if ($LASTEXITCODE -ne 0) {
        Fail "Playnite extension packaging failed."
    }
} else {
    # A .pext is a ZIP archive whose extension.yaml is at the archive root.
    # This path keeps CI lightweight while producing the exact format consumed
    # by Playnite's official ExtensionInstaller (ZipFile.OpenRead).
    $temporaryZip = Join-Path $dist ($packageBase + ".zip")
    if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force }
    if (Test-Path -LiteralPath $packagePath) { Remove-Item -LiteralPath $packagePath -Force }
    Compress-Archive -Path (Join-Path $output "*") -DestinationPath $temporaryZip -CompressionLevel Optimal
    Move-Item -LiteralPath $temporaryZip -Destination $packagePath
}

Write-Host ""
Write-Host "Playnite extension built successfully:" -ForegroundColor Green
Write-Host $packagePath
