[CmdletBinding()]
param(
    [string]$ExpectedVersion = "",
    [switch]$CheckArtifacts,
    [switch]$RequireSignatures
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot

function Fail([string]$Message) {
    throw "ApexSenseBridge release contract: $Message"
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $projectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "required file is missing: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Get-RequiredMatch(
    [string]$RelativePath,
    [string]$Pattern,
    [string]$Label
) {
    $match = [Regex]::Match((Read-Text $RelativePath), $Pattern)
    if (-not $match.Success) {
        Fail "could not read $Label from $RelativePath"
    }
    return $match.Groups[1].Value
}

function Assert-Version([string]$RelativePath, [string]$Pattern, [string]$Label) {
    $actual = Get-RequiredMatch $RelativePath $Pattern $Label
    if ($actual -ne $script:Version) {
        Fail "$Label in $RelativePath is '$actual', expected '$script:Version'"
    }
}

function Get-CoreVersion([string]$Value) {
    try {
        $cleaned = $Value.Trim().TrimStart('v', 'V')
        if ($cleaned -match '^(\d+\.\d+\.\d+)') {
            return $matches[1]
        }
        $parsed = [Version]::Parse($cleaned)
        if ($parsed.Build -lt 0) {
            Fail "version '$Value' does not contain major.minor.patch"
        }
        return "{0}.{1}.{2}" -f $parsed.Major, $parsed.Minor, $parsed.Build
    } catch {
        Fail "invalid version '$Value'"
    }
}

function Assert-ArtifactVersion([string]$RelativePath) {
    $path = Join-Path $projectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "release artifact is missing: $RelativePath"
    }
    $actual = Get-CoreVersion (Get-Item -LiteralPath $path).VersionInfo.ProductVersion
    if ($actual -ne $script:Version) {
        Fail "$RelativePath has product version '$actual', expected '$script:Version'"
    }
}

$script:Version = Get-RequiredMatch "CMakeLists.txt" `
    '(?m)^project\(ApexSenseBridge VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)\r?$' `
    "canonical project version"

if ([string]::IsNullOrWhiteSpace($ExpectedVersion) -and
    $env:GITHUB_REF_TYPE -eq "tag") {
    $ExpectedVersion = $env:GITHUB_REF_NAME
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion)) {
    $normalizedExpected = Get-CoreVersion $ExpectedVersion
    if ($normalizedExpected -ne $script:Version) {
        Fail "Git tag/expected version '$normalizedExpected' differs from source '$script:Version'"
    }
}

Assert-Version "installer\ApexSenseBridge.iss" `
    '(?m)^#define AppVersion "([0-9]+\.[0-9]+\.[0-9]+)"\r?$' "installer version"
Assert-Version "README.md" `
    '(?m)^# ApexSenseBridge ([0-9]+\.[0-9]+\.[0-9]+)\r?$' "README version"
Assert-Version "src\cli\CliApplication.cpp" `
    'ApexSenseBridge ([0-9]+\.[0-9]+\.[0-9]+)\\n' "CLI fallback version"
Assert-Version "src\platform\windows\ApexSenseBridge.rc" `
    '(?m)^#define ASB_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+)"\r?$' "resource fallback version"
Assert-Version "ApexSenseBridgeTray\MainWindow.xaml" `
    'Text="ApexSenseBridge v([0-9]+\.[0-9]+\.[0-9]+)"' "Tray UI version"
Assert-Version "ApexSenseBridgeTray\Services\UpdateCheckerService.cs" `
    'return "([0-9]+\.[0-9]+\.[0-9]+)";' "Tray updater fallback version"
Assert-Version "ApexSenseBridgeTray\Properties\AssemblyInfo.cs" `
    'AssemblyFileVersion\("([0-9]+\.[0-9]+\.[0-9]+)\.0"\)' "Tray assembly version"
Assert-Version "playnite\ApexSenseBridge\extension.yaml" `
    '(?m)^Version: ([0-9]+\.[0-9]+\.[0-9]+)\r?$' "Playnite manifest version"
Assert-Version "playnite\ApexSenseBridge\Properties\AssemblyInfo.cs" `
    'AssemblyFileVersion\("([0-9]+\.[0-9]+\.[0-9]+)\.0"\)' "Playnite assembly version"
Assert-Version "scripts\update-app.ps1" `
    'if \(\$installedVersion -eq "0\.0\.0"\) \{\r?\n\s*\$installedVersion = "([0-9]+\.[0-9]+\.[0-9]+)"' "script updater fallback version"

if ($CheckArtifacts) {
    foreach ($artifact in @(
        "build-win\Release\ApexSenseBridge.exe",
        "build-win\Release\ApexSenseBridgeControl.exe",
        "build-win\Release\ApexSenseBridgeTray.exe",
        "playnite\ApexSenseBridge\bin\Release\ApexSenseBridge.dll",
        "dist\ApexSenseBridge-Setup.exe",
        "dist\ApexSenseBridgeTray.exe"
    )) {
        Assert-ArtifactVersion $artifact
    }

    $expectedPext = "ApexSenseBridge-Playnite-$($script:Version).pext"
    $pextFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "dist") `
        -Filter "ApexSenseBridge-Playnite-*.pext" -File)
    if ($pextFiles.Count -ne 1 -or $pextFiles[0].Name -ne $expectedPext) {
        Fail "dist must contain exactly $expectedPext and no stale Playnite package"
    }

    $checksumPath = Join-Path $projectRoot "dist\SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        Fail "dist\SHA256SUMS.txt is missing"
    }
    $checksumLines = @(Get-Content -LiteralPath $checksumPath |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($checksumLines.Count -eq 0) {
        Fail "dist\SHA256SUMS.txt is empty"
    }
    foreach ($line in $checksumLines) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  ([^\\/]+)$') {
            Fail "invalid checksum line: $line"
        }
        $expectedHash = $Matches[1].ToUpperInvariant()
        $artifactName = $Matches[2]
        $artifactPath = Join-Path (Join-Path $projectRoot "dist") $artifactName
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            Fail "checksum references missing artifact: $artifactName"
        }
        $actualHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        if ($actualHash -ne $expectedHash) {
            Fail "SHA-256 mismatch for $artifactName"
        }
    }
}

if ($RequireSignatures) {
    if (-not $CheckArtifacts) {
        Fail "-RequireSignatures also requires -CheckArtifacts"
    }
    $signedPaths = @(
        "build-win\Release\ApexSenseBridge.exe",
        "build-win\Release\ApexSenseBridgeControl.exe",
        "build-win\Release\ApexSenseBridgeTray.exe",
        "build-win\Release\viiper.exe",
        "build-win\Release\libVIIPER.dll",
        "playnite\ApexSenseBridge\bin\Release\ApexSenseBridge.dll",
        "dist\ApexSenseBridge-Setup.exe",
        "dist\ApexSenseBridgeTray.exe"
    )
    $publisher = $null
    foreach ($relativePath in $signedPaths) {
        $path = Join-Path $projectRoot $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Fail "signed payload is missing: $relativePath"
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
            -not $signature.SignerCertificate) {
            Fail "$relativePath has no valid trusted Authenticode signature ($($signature.Status))"
        }
        if ($null -eq $publisher) {
            $publisher = $signature.SignerCertificate.Subject
        } elseif ($signature.SignerCertificate.Subject -ne $publisher) {
            Fail "$relativePath is signed by a different publisher"
        }
    }
}

Write-Output "Release version contract passed for $($script:Version)."
