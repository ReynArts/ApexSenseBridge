[CmdletBinding()]
param(
    [string]$ExpectedVersion = "",
    [switch]$CheckArtifacts,
    [switch]$RequireSignatures,
    [switch]$RequireSignedUninstaller,
    [switch]$AllowUntrustedSignatures,
    [string]$UpdatePackagePath = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot

if ($AllowUntrustedSignatures -and -not $RequireSignatures) {
    throw "ApexSenseBridge release contract: -AllowUntrustedSignatures requires -RequireSignatures"
}
if ($RequireSignedUninstaller -and -not $RequireSignatures) {
    throw "ApexSenseBridge release contract: -RequireSignedUninstaller requires -RequireSignatures"
}

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
    # Inno Setup reserves a fixed-width string and pads ProductVersion with spaces.
    $productVersion = ([string](Get-Item -LiteralPath $path).VersionInfo.ProductVersion).Trim(
        [char[]](0, 9, 10, 11, 12, 13, 32, 133, 160))
    $actual = Get-CoreVersion $productVersion
    if ($actual -ne $script:Version) {
        Fail "$RelativePath has product version '$actual', expected '$script:Version'"
    }
    if ($productVersion -ne $script:ReleaseLabel) {
        Fail "$RelativePath has release label '$productVersion', expected '$script:ReleaseLabel'"
    }
}

function Assert-UpdatePackage(
    [string]$Path,
    [string]$ExpectedVersion,
    [string]$DriverManifestPath,
    [string]$BinaryDirectory = ""
) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Fail "release artifact is missing: $Path"
    }
    if ((Get-Item -LiteralPath $Path).Length -gt 200MB) {
        Fail "update package exceeds the 200 MB limit"
    }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $binaries = New-Object System.Collections.Generic.List[string]
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entriesByName = @{}
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName
            $key = $name.ToLowerInvariant()
            if ($entriesByName.ContainsKey($key)) {
                Fail "update package has a duplicate entry: $name"
            }
            $entriesByName[$key] = $entry
            if ($name -ne "manifest.json") {
                $safe = $name -match '^(app|playnite)/[^\\:]+$' -and $name -notmatch '(^|/)\.\.?(/|$)' -and
                    $name -notmatch '//' -and -not $name.EndsWith("/") -and
                    $name -notmatch '(^|/)[^/]*[. ](/|$)' -and $name -notmatch '[<>"|?*\x00-\x1f]' -and
                    $name -notmatch '(?i)(^|/)(con|prn|aux|nul|com[0-9]|lpt[0-9])(\.[^/]*)?(/|$)'
                if (-not $safe) {
                    Fail "update package has an unsafe entry name: $name"
                }
            }
            if ($entry.Length -gt 100MB) {
                Fail "update package entry exceeds the 100 MB limit: $name"
            }
        }
        if (-not $entriesByName.ContainsKey("manifest.json")) {
            Fail "update package has no manifest.json"
        }

        $manifestStream = $archive.GetEntry("manifest.json").Open()
        try {
            $memory = New-Object System.IO.MemoryStream
            $manifestStream.CopyTo($memory)
            $manifestBytes = $memory.ToArray()
        } finally {
            $manifestStream.Dispose()
        }
        if ($manifestBytes.Length -ge 3 -and $manifestBytes[0] -eq 0xEF -and
            $manifestBytes[1] -eq 0xBB -and $manifestBytes[2] -eq 0xBF) {
            Fail "update package manifest.json must be UTF-8 without a BOM"
        }
        try {
            $manifest = (New-Object System.Text.UTF8Encoding($false, $true)).GetString($manifestBytes) |
                ConvertFrom-Json
        } catch {
            Fail "update package manifest.json is not valid UTF-8 JSON"
        }

        if ($manifest.schema -ne 1) {
            Fail "update package manifest schema is '$($manifest.schema)', expected 1"
        }
        if ($manifest.version -ne $ExpectedVersion) {
            Fail "update package manifest version is '$($manifest.version)', expected '$ExpectedVersion'"
        }
        if ($manifest.driversChanged -isnot [bool]) {
            Fail "update package manifest driversChanged must be a boolean"
        }
        $drivers = Get-Content -LiteralPath $DriverManifestPath -Raw | ConvertFrom-Json
        if ($manifest.usbipVersion -ne $drivers.'usbip-win2'.version) {
            Fail "update package usbipVersion '$($manifest.usbipVersion)' differs from installer\driver-manifest.json"
        }
        if ($manifest.hidHideVersion -ne $drivers.HidHide.version) {
            Fail "update package hidHideVersion '$($manifest.hidHideVersion)' differs from installer\driver-manifest.json"
        }

        $files = @($manifest.files)
        if ($files.Count -eq 0) {
            Fail "update package manifest lists no files"
        }
        if ($files.Count -ne $entriesByName.Count - 1) {
            Fail "update package holds $($entriesByName.Count - 1) files but its manifest lists $($files.Count)"
        }
        $listed = @{}
        $index = 0
        foreach ($file in $files) {
            $index++
            if ($file.section -cnotin @("app", "playnite")) {
                Fail "update package manifest has an unknown section '$($file.section)'"
            }
            $entryName = "$($file.section)/$($file.path)"
            $key = $entryName.ToLowerInvariant()
            if ($listed.ContainsKey($key)) {
                Fail "update package manifest lists $entryName twice"
            }
            $listed[$key] = $true
            if ($file.sha256 -cnotmatch '^[0-9a-f]{64}$') {
                Fail "update package manifest sha256 for $entryName is not 64 lowercase hex digits"
            }
            if (($file.size -isnot [int] -and $file.size -isnot [long]) -or $file.size -lt 0) {
                Fail "update package manifest size for $entryName is not a non-negative integer"
            }
            $leaf = $entryName.Substring($entryName.LastIndexOf('/') + 1)
            if ($leaf -match '^(USBip-.*\.exe|HidHide_.*\.exe|install-usbip\.ps1|ApexSenseBridge-Setup\.exe|ApexSenseBridge-Uninstaller\.exe|unins\d+\.(exe|dat|msg))$' -or
                $leaf -match '\.e32$') {
                Fail "update package must not contain $leaf"
            }
            if (-not $entriesByName.ContainsKey($key)) {
                Fail "update package manifest lists $entryName, which is not in the zip"
            }
            $entry = $entriesByName[$key]
            if ($entry.FullName -cne $entryName) {
                Fail "update package entry '$($entry.FullName)' differs in case from manifest path '$entryName'"
            }
            $entryStream = $entry.Open()
            try {
                $memory = New-Object System.IO.MemoryStream
                $entryStream.CopyTo($memory)
                $bytes = $memory.ToArray()
            } finally {
                $entryStream.Dispose()
            }
            if ($bytes.LongLength -ne $file.size) {
                Fail "update package size mismatch for $entryName"
            }
            $sha = [System.Security.Cryptography.SHA256]::Create()
            try {
                $actualHash = (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
            } finally {
                $sha.Dispose()
            }
            if ($actualHash -ne $file.sha256) {
                Fail "update package SHA-256 mismatch for $entryName"
            }
            if ($BinaryDirectory -ne "" -and $leaf -match '(?i)\.(exe|dll)$') {
                New-Item -ItemType Directory -Path $BinaryDirectory -Force | Out-Null
                $binaryPath = Join-Path $BinaryDirectory ("{0:d3}-{1}" -f $index, $leaf)
                [System.IO.File]::WriteAllBytes($binaryPath, $bytes)
                $binaries.Add($binaryPath)
            }
        }
    } finally {
        $archive.Dispose()
    }
    return ,$binaries.ToArray()
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

$releaseSuffix = Get-RequiredMatch "CMakeLists.txt" `
    'set\(ASB_RELEASE_LABEL "\$\{PROJECT_VERSION\}([^"\r\n]*)"\)' "release label suffix"
$releaseLabel = $script:Version + $releaseSuffix
if ((Read-Text "CHANGELOG.md") -notmatch ('(?m)^## ' + [regex]::Escape($releaseLabel) + '\r?$')) {
    Fail "CHANGELOG.md has no section for release '$releaseLabel'"
}
foreach ($releaseManifest in @(
    @{ Path = "installer\ApexSenseBridge.iss"; Pattern = '#define AppReleaseLabel "([^"\r\n]+)"' },
    @{ Path = "ApexSenseBridgeTray\Properties\AssemblyInfo.cs"; Pattern = 'AssemblyInformationalVersion\("([^"\r\n]+)"\)' },
    @{ Path = "playnite\ApexSenseBridge\Properties\AssemblyInfo.cs"; Pattern = 'AssemblyInformationalVersion\("([^"\r\n]+)"\)' }
)) {
    $actualLabel = Get-RequiredMatch $releaseManifest.Path $releaseManifest.Pattern "release label"
    if ($actualLabel -ne $releaseLabel) { Fail "release label in $($releaseManifest.Path) is '$actualLabel', expected '$releaseLabel'" }
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and
    $ExpectedVersion.Trim().TrimStart('v', 'V') -ne $releaseLabel) {
    Fail "release tag '$ExpectedVersion' differs from release label '$releaseLabel'"
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
        "dist\ApexSenseBridge-Setup.exe"
    )) {
        Assert-ArtifactVersion $artifact
    }

    $expectedPext = "ApexSenseBridge-Playnite-$($script:Version).pext"
    $pextFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "dist") `
        -Filter "ApexSenseBridge-Playnite-*.pext" -File)
    if ($pextFiles.Count -ne 1 -or $pextFiles[0].Name -ne $expectedPext) {
        Fail "dist must contain exactly $expectedPext and no stale Playnite package"
    }

    if ([string]::IsNullOrWhiteSpace($UpdatePackagePath)) {
        $UpdatePackagePath = Join-Path $projectRoot "dist\ApexSenseBridge-Update.zip"
    }
    Assert-UpdatePackage $UpdatePackagePath $script:Version `
        (Join-Path $projectRoot "installer\driver-manifest.json") | Out-Null

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
        "dist\ApexSenseBridge-Setup.exe"
    )
    if ($RequireSignedUninstaller) {
        $uninstallerCache = Join-Path $projectRoot "build-signpath\signed-uninstaller-cache"
        $uninstallers = @(Get-ChildItem -LiteralPath $uninstallerCache -File -Filter "*.e32" `
            -ErrorAction SilentlyContinue)
        if ($uninstallers.Count -ne 1) {
            Fail "expected exactly one signed Inno Setup uninstaller, found $($uninstallers.Count)"
        }
        $signedPaths += $uninstallers[0].FullName
    }
    $publisher = $null
    foreach ($relativePath in $signedPaths) {
        $path = if ([System.IO.Path]::IsPathRooted($relativePath)) {
            $relativePath
        } else {
            Join-Path $projectRoot $relativePath
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Fail "signed payload is missing: $relativePath"
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        $signatureIsTrusted = $signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid
        $signatureIsIntactTestSignature = $AllowUntrustedSignatures -and
            $signature.SignerCertificate -and
            $signature.Status -notin @(
                [System.Management.Automation.SignatureStatus]::NotSigned,
                [System.Management.Automation.SignatureStatus]::HashMismatch)
        if ((-not $signatureIsTrusted -and -not $signatureIsIntactTestSignature) -or
            -not $signature.SignerCertificate) {
            Fail "$relativePath has no valid trusted Authenticode signature ($($signature.Status))"
        }
        if ($null -eq $publisher) {
            $publisher = $signature.SignerCertificate.Subject
        } elseif ($signature.SignerCertificate.Subject -ne $publisher) {
            Fail "$relativePath is signed by a different publisher"
        }
    }

    $updateBinaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("asb-update-verify-" + [Guid]::NewGuid().ToString("N"))
    try {
        $updateBinaries = Assert-UpdatePackage $UpdatePackagePath $script:Version `
            (Join-Path $projectRoot "installer\driver-manifest.json") $updateBinaryDirectory
        foreach ($binary in $updateBinaries) {
            $packagedName = (Split-Path -Leaf $binary).Substring(4)
            $signature = Get-AuthenticodeSignature -LiteralPath $binary
            $signatureIsTrusted = $signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid
            $signatureIsIntactTestSignature = $AllowUntrustedSignatures -and
                $signature.SignerCertificate -and
                $signature.Status -notin @(
                    [System.Management.Automation.SignatureStatus]::NotSigned,
                    [System.Management.Automation.SignatureStatus]::HashMismatch)
            if ((-not $signatureIsTrusted -and -not $signatureIsIntactTestSignature) -or
                -not $signature.SignerCertificate) {
                Fail "update package file $packagedName has no valid trusted Authenticode signature ($($signature.Status))"
            }
            if ($signature.SignerCertificate.Subject -ne $publisher) {
                Fail "update package file $packagedName is signed by a different publisher"
            }
        }
    } finally {
        if (Test-Path -LiteralPath $updateBinaryDirectory) {
            Remove-Item -LiteralPath $updateBinaryDirectory -Recurse -Force
        }
    }
}

Write-Output "Release version contract passed for $releaseLabel (numeric version $($script:Version))."
