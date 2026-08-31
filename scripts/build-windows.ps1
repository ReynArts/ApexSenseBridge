$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build-win"

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "CMake was not found. Install CMake and reopen PowerShell."
}

# Pick the newest supported Visual Studio generator that is both known to
# CMake and actually installed. VS 2026 is preferred, VS 2022 is the fallback.
$help = cmake --help 2>&1 | Out-String
$candidates = @(
    @{ Name = "Visual Studio 18 2026"; Major = "18" },
    @{ Name = "Visual Studio 17 2022"; Major = "17" }
)

$generator = $null
foreach ($candidate in $candidates) {
    if ($help -notmatch [regex]::Escape($candidate.Name)) { continue }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * -version "[$($candidate.Major).0,$([int]$candidate.Major + 1).0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and $path) {
            $generator = $candidate.Name
            break
        }
    }
}

if (-not $generator) {
    Fail @"
No usable Visual Studio C++ Build Tools installation was found.
Install 'Desktop development with C++' plus an x64/x86 MSVC toolset and a Windows SDK.
VS Build Tools 2026 and 2022 are both supported by this script.
"@
}

# A CMake build directory is tied to one generator. Delete stale cache when
# switching generator versions so CMake never mixes VS/NMake/older installs.
if (Test-Path (Join-Path $build "CMakeCache.txt")) {
    $cache = Get-Content (Join-Path $build "CMakeCache.txt") -Raw
    if ($cache -notmatch [regex]::Escape("CMAKE_GENERATOR:INTERNAL=$generator")) {
        Write-Host "Removing stale CMake cache..."
        Remove-Item -Recurse -Force $build
    }
}

Write-Host "Configuring ApexSenseBridge with $generator..."
cmake -S $root -B $build -G $generator -A x64
if ($LASTEXITCODE -ne 0) { Fail "CMake configuration failed." }

Write-Host "Building Release..."
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { Fail "Compilation failed." }

$exe = Join-Path $build "Release\ApexSenseBridge.exe"
if (-not (Test-Path $exe)) {
    Fail "Compilation finished but ApexSenseBridge.exe was not found at $exe"
}

Write-Host ""
Write-Host "Built successfully:" -ForegroundColor Green
Write-Host "  $exe"
Write-Host ""
Write-Host "Safe first command:"
Write-Host "  & `"$exe`" list"
Write-Host ""
Write-Host "Do not run test-rt until the detected HID interface has been checked."
