param()

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $root "ApexSenseBridgeTray"
$project = Join-Path $projectDir "ApexSenseBridgeTray.csproj"
$learningTestProject = Join-Path $root "tests\ApexSenseBridgeTray.LearningTests.csproj"
$learningTestExe = Join-Path $root "tests\bin\Release\ApexSenseBridgeTray.LearningTests.exe"
$buildWinRelease = Join-Path $root "build-win\Release"
$outputDir = $buildWinRelease
$legacyOutputDir = Join-Path $projectDir "bin\Release"

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

# Update supported_games.json if not present
$gamesJson = Join-Path $root "data\supported_games.json"
if (-not (Test-Path $gamesJson)) {
    Write-Host "Generating supported_games.json from PCGamingWiki..."
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source (Join-Path $PSScriptRoot "update_pcgw_list.py")
    } else {
        Write-Warning "data\supported_games.json is missing and python.exe was not found to regenerate it."
    }
}

$msbuild = ""
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $visualStudio = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -eq 0 -and $visualStudio) {
        $candidate = Join-Path $visualStudio "MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            $msbuild = $candidate
        }
    }
}

if (-not $msbuild) {
    $frameworkMsbuild = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe"
    if (Test-Path -LiteralPath $frameworkMsbuild) {
        $msbuild = $frameworkMsbuild
    } else {
        Fail "No suitable MSBuild.exe was found."
    }
}

Write-Host "Building ApexSenseBridgeTray application..."
& $msbuild $project /t:Rebuild /p:Configuration=Release
if ($LASTEXITCODE -ne 0) {
    Fail "ApexSenseBridgeTray compilation failed."
}

Write-Host "Running executable-learning regression tests..."
& $msbuild $learningTestProject /t:Rebuild /p:Configuration=Release
if ($LASTEXITCODE -ne 0) {
    Fail "Executable-learning test compilation failed."
}
& $learningTestExe
if ($LASTEXITCODE -ne 0) {
    Fail "Executable-learning regression tests failed."
}

$trayExe = Join-Path $outputDir "ApexSenseBridgeTray.exe"
if (-not (Test-Path $trayExe)) {
    Fail "ApexSenseBridgeTray.exe was not created."
}

if (Test-Path -LiteralPath $legacyOutputDir) {
    Remove-Item -LiteralPath $legacyOutputDir -Recurse -Force
}

$running = Get-Process -Name "ApexSenseBridgeTray" -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force -ErrorAction SilentlyContinue
    $running | Wait-Process -Timeout 3 -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 300

Write-Host ""
Write-Host "ApexSenseBridgeTray built successfully:" -ForegroundColor Green
Write-Host "  Runtime: $(Join-Path $buildWinRelease 'ApexSenseBridgeTray.exe')"
