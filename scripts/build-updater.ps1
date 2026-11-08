param(
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "ApexSenseBridgeUpdater\ApexSenseBridgeUpdater.csproj"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root "build-win\Release"
}

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

foreach ($required in @("shared\UpdatePackage.cs", "shared\ReleaseSecurity.cs", "shared\BridgeSession.cs", "shared\ProcessLiveness.cs")) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $required))) {
        Fail "$required is missing."
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

$outputFull = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\') + "\"
New-Item -ItemType Directory -Path $outputFull -Force | Out-Null

Write-Host "Building ApexSenseBridgeUpdater..."
& $msbuild $project /t:Rebuild /p:Configuration=Release "/p:OutDir=$outputFull" /nologo /v:minimal
if ($LASTEXITCODE -ne 0) {
    Fail "ApexSenseBridgeUpdater compilation failed."
}

$updaterExe = Join-Path $outputFull "ApexSenseBridgeUpdater.exe"
if (-not (Test-Path -LiteralPath $updaterExe -PathType Leaf)) {
    Fail "ApexSenseBridgeUpdater.exe was not created."
}

$bytes = [System.IO.File]::ReadAllBytes($updaterExe)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
if (-not $ascii.Contains("requireAdministrator")) {
    Fail "ApexSenseBridgeUpdater.exe does not embed the requireAdministrator manifest."
}
$unicode = [System.Text.Encoding]::Unicode.GetString($bytes)
foreach ($marker in @("--test-programdata", "--test-registry-root", "--test-session-name")) {
    if ($ascii.Contains($marker) -or $unicode.Contains($marker)) {
        Fail "ApexSenseBridgeUpdater.exe contains the test-only option $marker; build the Release configuration."
    }
}

Write-Host ""
Write-Host "ApexSenseBridgeUpdater built successfully:" -ForegroundColor Green
Write-Host "  Runtime: $updaterExe"
