param()

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "tests\ApexSenseBridgeTray.LearningTests.csproj"
$testExe = Join-Path $root "tests\bin\Release\ApexSenseBridgeTray.LearningTests.exe"

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
        throw "No suitable MSBuild.exe was found."
    }
}

& $msbuild $project /t:Rebuild /p:Configuration=Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $testExe
exit $LASTEXITCODE
