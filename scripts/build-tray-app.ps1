param()

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $root "ApexSenseBridgeTray"
$project = Join-Path $projectDir "ApexSenseBridgeTray.csproj"
$outputDir = Join-Path $projectDir "bin\Release"
$buildWinRelease = Join-Path $root "build-win\Release"
$dist = Join-Path $root "dist"

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

# Update supported_games.json if not present
$gamesJson = Join-Path $root "data\supported_games.json"
if (-not (Test-Path $gamesJson)) {
    Write-Host "Generating supported_games.json from PCGamingWiki..."
    & (Join-Path $PSScriptRoot "update-pcgw-list.ps1")
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

Write-Host "Building ApexSenseBridgeTray application..."
& $msbuild $project /t:Rebuild /p:Configuration=Release
if ($LASTEXITCODE -ne 0) {
    Fail "ApexSenseBridgeTray compilation failed."
}

$trayExe = Join-Path $outputDir "ApexSenseBridgeTray.exe"
if (-not (Test-Path $trayExe)) {
    Fail "ApexSenseBridgeTray.exe was not created."
}

# Copy to build-win\Release for Inno Setup packaging
if (-not (Test-Path $buildWinRelease)) {
    New-Item -ItemType Directory -Path $buildWinRelease -Force | Out-Null
}
Copy-Item (Join-Path $outputDir "ApexSenseBridgeTray.exe*") $buildWinRelease -Force

# Copy to dist
if (-not (Test-Path $dist)) {
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
}
Copy-Item (Join-Path $outputDir "ApexSenseBridgeTray.exe*") $dist -Force

# Copy directly to root of repository for immediate testing
Copy-Item (Join-Path $outputDir "ApexSenseBridgeTray.exe*") $root -Force

Write-Host ""
Write-Host "ApexSenseBridgeTray built successfully:" -ForegroundColor Green
Write-Host "  Root:    $(Join-Path $root 'ApexSenseBridgeTray.exe')"
Write-Host "  Dist:    $(Join-Path $dist 'ApexSenseBridgeTray.exe')"
Write-Host "  Release: $trayExe"
