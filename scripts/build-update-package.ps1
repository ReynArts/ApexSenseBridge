[CmdletBinding()]
param(
    [string]$ProjectRoot = "",
    [string]$ReleaseDir = "",
    [string]$OutputPath = "",
    [string]$Version = "",
    [switch]$DriversChanged
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
if ([string]::IsNullOrWhiteSpace($ReleaseDir)) {
    $ReleaseDir = Join-Path $ProjectRoot "build-win\Release"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "dist\ApexSenseBridge-Update.zip"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

$installerDirectory = Join-Path $ProjectRoot "installer"
$installerScript = Join-Path $installerDirectory "ApexSenseBridge.iss"
$driverManifestPath = Join-Path $installerDirectory "driver-manifest.json"
$updaterName = "ApexSenseBridgeUpdater.exe"
$maximumEntryBytes = 100MB
$maximumPackageBytes = 200MB
$entryTimestamp = New-Object System.DateTimeOffset(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)

function Fail([string]$Message) {
    throw "ApexSenseBridge update package: $Message"
}

function Get-CoreVersion([string]$Value) {
    $cleaned = $Value.Trim().TrimStart('v', 'V')
    if ($cleaned -notmatch '^(\d+)\.(\d+)\.(\d+)') {
        Fail "version '$Value' does not contain major.minor.patch"
    }
    return "{0}.{1}.{2}" -f [int]$Matches[1], [int]$Matches[2], [int]$Matches[3]
}

function Test-ForbiddenPackageFile([string]$Leaf, [string]$SourcePath) {
    if ($Leaf -match '^(USBip-.*\.exe|HidHide_.*\.exe|install-usbip\.ps1|ApexSenseBridge-Setup\.exe|ApexSenseBridge-Uninstaller\.exe|unins\d+\.(exe|dat|msg))$') {
        return $true
    }
    if ([System.IO.Path]::GetExtension($Leaf) -ieq ".e32") {
        return $true
    }
    if ($SourcePath -match '(?i)[\\/]third_party[\\/]prerequisites[\\/][^\\/]+\.exe$') {
        return $true
    }
    return $false
}

function Test-SafeRelativePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.StartsWith("/") -or $Path.Contains("\") -or
        $Path.Contains(":") -or $Path.Contains("//")) {
        return $false
    }
    foreach ($segment in $Path.Split('/')) {
        if ($segment -eq "" -or $segment -eq "." -or $segment -eq ".." -or
            $segment.EndsWith(".") -or $segment.EndsWith(" ") -or
            $segment -match '^(?i)(con|prn|aux|nul|com[0-9]|lpt[0-9])(\..*)?$' -or
            $segment -match '[<>"|?*\x00-\x1f]') {
            return $false
        }
    }
    return $true
}

function ConvertTo-JsonString([string]$Value) {
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    foreach ($character in $Value.ToCharArray()) {
        switch ($character) {
            '"' { [void]$builder.Append('\"') }
            '\' { [void]$builder.Append('\\') }
            default {
                if ([int]$character -lt 32 -or [int]$character -gt 126) {
                    [void]$builder.Append(('\u{0:x4}' -f [int]$character))
                } else {
                    [void]$builder.Append($character)
                }
            }
        }
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
    } finally {
        $sha.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $installerScript -PathType Leaf)) {
    Fail "installer script is missing: $installerScript"
}
if (-not (Test-Path -LiteralPath $driverManifestPath -PathType Leaf)) {
    Fail "driver manifest is missing: $driverManifestPath"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakePath = Join-Path $ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
        Fail "CMakeLists.txt is missing and -Version was not given"
    }
    $versionMatch = [Regex]::Match((Get-Content -LiteralPath $cmakePath -Raw),
        '(?m)^project\(ApexSenseBridge VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)\r?$')
    if (-not $versionMatch.Success) {
        Fail "could not read the canonical project version from CMakeLists.txt"
    }
    $Version = $versionMatch.Groups[1].Value
}
$Version = Get-CoreVersion $Version

$driverManifest = Get-Content -LiteralPath $driverManifestPath -Raw | ConvertFrom-Json
$usbipVersion = [string]$driverManifest.'usbip-win2'.version
$hidHideVersion = [string]$driverManifest.HidHide.version
if ([string]::IsNullOrWhiteSpace($usbipVersion) -or [string]::IsNullOrWhiteSpace($hidHideVersion)) {
    Fail "installer\driver-manifest.json does not define both driver versions"
}

$defines = @{}
$sourceEntries = New-Object System.Collections.Generic.List[object]
$inFiles = $false
$lineNumber = 0
foreach ($rawLine in (Get-Content -LiteralPath $installerScript)) {
    $lineNumber++
    $line = $rawLine.Trim()
    if ($line -match '^#define\s+(\w+)\s+"([^"]*)"') {
        $defines[$Matches[1]] = $Matches[2]
        continue
    }
    if ($line.StartsWith("[")) {
        $inFiles = $line -ieq "[Files]"
        continue
    }
    if (-not $inFiles -or $line -eq "" -or $line.StartsWith(";")) {
        continue
    }
    $expanded = [Regex]::Replace($line, '\{#(\w+)\}', {
        param($match)
        $name = $match.Groups[1].Value
        if (-not $defines.ContainsKey($name)) {
            Fail "installer line $lineNumber uses the undefined preprocessor symbol $name"
        }
        return $defines[$name]
    })
    $lineMatch = [Regex]::Match($expanded, '^Source:\s*"([^"]+)"\s*;(.*)$')
    if (-not $lineMatch.Success) {
        Fail "installer [Files] line $lineNumber is not understood: $line"
    }
    $parameters = $lineMatch.Groups[2].Value
    $destinationMatch = [Regex]::Match($parameters, 'DestDir:\s*"([^"]*)"')
    if (-not $destinationMatch.Success) {
        Fail "installer [Files] line $lineNumber has no DestDir"
    }
    $destinationNameMatch = [Regex]::Match($parameters, 'DestName:\s*"([^"]*)"')
    $flagsMatch = [Regex]::Match($parameters, 'Flags:\s*([^;]*)')
    $sourceEntries.Add([pscustomobject]@{
        Source = $lineMatch.Groups[1].Value
        DestDir = $destinationMatch.Groups[1].Value
        DestName = if ($destinationNameMatch.Success) { $destinationNameMatch.Groups[1].Value } else { "" }
        Flags = if ($flagsMatch.Success) { $flagsMatch.Groups[1].Value.Trim() } else { "" }
    })
}
if (-not $defines.ContainsKey("ExtensionId")) {
    Fail "installer does not define ExtensionId"
}
$extensionPattern = '^\{userappdata\}\\Playnite\\Extensions\\' + [Regex]::Escape($defines["ExtensionId"]) + '(?:\\(.+))?$'

$packageFiles = New-Object System.Collections.Generic.List[object]
$seenDestinations = @{}
$skippedTemporary = 0

function Add-PackageFile([string]$Section, [string]$RelativePath, [string]$SourcePath) {
    if (-not (Test-SafeRelativePath $RelativePath)) {
        Fail "unsafe package path: $RelativePath"
    }
    $leaf = $RelativePath.Substring($RelativePath.LastIndexOf('/') + 1)
    if (Test-ForbiddenPackageFile $leaf $SourcePath) {
        Fail "refusing to package a prerequisite installer, uninstaller or setup: $SourcePath"
    }
    $key = ($Section + "/" + $RelativePath).ToLowerInvariant()
    if ($seenDestinations.ContainsKey($key)) {
        Fail "duplicate package path (case-insensitive): $Section/$RelativePath"
    }
    $seenDestinations[$key] = $true
    $packageFiles.Add([pscustomobject]@{ Section = $Section; Path = $RelativePath; SourcePath = $SourcePath })
}

foreach ($entry in $sourceEntries) {
    if ($entry.DestDir -match '^\{tmp\}' -or $entry.Flags -match '\bdeleteafterinstall\b') {
        $skippedTemporary++
        continue
    }
    $section = $null
    $subdirectory = ""
    if ($entry.DestDir -match '^\{app\}(?:\\(.+))?$') {
        $section = "app"
        if ($Matches[1]) { $subdirectory = $Matches[1] }
    } elseif ($entry.DestDir -match $extensionPattern) {
        $section = "playnite"
        if ($Matches[1]) { $subdirectory = $Matches[1] }
    } else {
        Fail "installer destination is not understood and will not be packaged: $($entry.DestDir)"
    }
    $subdirectory = $subdirectory.Replace('\', '/').Trim('/')

    $sourceLeaf = Split-Path -Leaf $entry.Source
    $sourceParent = Split-Path -Parent $entry.Source
    $sourceDirectory = [System.IO.Path]::GetFullPath((Join-Path $installerDirectory $sourceParent))
    $leaf = $sourceLeaf
    $sourcePath = Join-Path $sourceDirectory $sourceLeaf
    if ($leaf.Contains("*") -or $leaf.Contains("?")) {
        if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
            Fail "source directory is missing: $sourceDirectory"
        }
        $recurse = $entry.Flags -match '\brecursesubdirs\b'
        $matched = @(Get-ChildItem -LiteralPath $sourceDirectory -Filter $leaf -File -Recurse:$recurse)
        if ($matched.Count -eq 0) {
            Fail "wildcard source matched no file: $sourcePath"
        }
        $baseLength = $sourceDirectory.TrimEnd('\').Length + 1
        foreach ($file in $matched) {
            $relative = $file.FullName.Substring($baseLength).Replace('\', '/')
            $relativePath = if ($subdirectory -ne "") { "$subdirectory/$relative" } else { $relative }
            Add-PackageFile $section $relativePath $file.FullName
        }
        continue
    }

    $destinationLeaf = if ($entry.DestName -ne "") { $entry.DestName } else { $leaf }
    $relativePath = if ($subdirectory -ne "") { "$subdirectory/$destinationLeaf" } else { $destinationLeaf }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        if ($section -eq "app" -and $relativePath -ieq $updaterName) {
            Write-Warning "$updaterName is listed by the installer but missing at $sourcePath; the update package is built without it."
            continue
        }
        Fail "required file is missing: $sourcePath"
    }
    Add-PackageFile $section $relativePath $sourcePath
}

if (-not $seenDestinations.ContainsKey("app/" + $updaterName.ToLowerInvariant())) {
    $updaterSource = Join-Path $ReleaseDir $updaterName
    if (Test-Path -LiteralPath $updaterSource -PathType Leaf) {
        Add-PackageFile "app" $updaterName $updaterSource
    } else {
        Write-Warning "$updaterName was not found in $ReleaseDir and is not listed by the installer; the update package is built without it, so clients cannot apply it until the updater ships."
    }
}

foreach ($required in @("app/apexsensebridge.exe", "app/apexsensebridgetray.exe",
                        "playnite/apexsensebridge.dll", "playnite/extension.yaml")) {
    if (-not $seenDestinations.ContainsKey($required)) {
        Fail "the installer [Files] list did not yield the required file $required"
    }
}

$sectionOrder = @{ "app" = 0; "playnite" = 1 }
$sortedByKey = New-Object 'System.Collections.Generic.SortedDictionary[string,object]' ([System.StringComparer]::Ordinal)
foreach ($file in $packageFiles) {
    $sortedByKey.Add("$($sectionOrder[$file.Section])|$($file.Path)", $file)
}
$sortedFiles = @($sortedByKey.Values)

$loaded = New-Object System.Collections.Generic.List[object]
$totalBytes = [long]0
foreach ($file in $sortedFiles) {
    $bytes = [System.IO.File]::ReadAllBytes($file.SourcePath)
    if ($bytes.LongLength -gt $maximumEntryBytes) {
        Fail "$($file.Section)/$($file.Path) exceeds the 100 MB entry limit"
    }
    $totalBytes += $bytes.LongLength
    $loaded.Add([pscustomobject]@{
        Section = $file.Section
        Path = $file.Path
        Bytes = $bytes
        Sha256 = Get-Sha256Hex $bytes
        Size = $bytes.LongLength
    })
}
if ($totalBytes -gt $maximumPackageBytes) {
    Fail "the uncompressed payload exceeds the 200 MB package limit"
}

$manifestBuilder = New-Object System.Text.StringBuilder
[void]$manifestBuilder.Append("{`n")
[void]$manifestBuilder.Append("  `"schema`": 1,`n")
[void]$manifestBuilder.Append("  `"version`": $(ConvertTo-JsonString $Version),`n")
[void]$manifestBuilder.Append("  `"driversChanged`": $(if ($DriversChanged) { 'true' } else { 'false' }),`n")
[void]$manifestBuilder.Append("  `"usbipVersion`": $(ConvertTo-JsonString $usbipVersion),`n")
[void]$manifestBuilder.Append("  `"hidHideVersion`": $(ConvertTo-JsonString $hidHideVersion),`n")
[void]$manifestBuilder.Append("  `"files`": [`n")
for ($index = 0; $index -lt $loaded.Count; $index++) {
    $item = $loaded[$index]
    $separator = if ($index -lt $loaded.Count - 1) { "," } else { "" }
    [void]$manifestBuilder.Append("    { `"section`": $(ConvertTo-JsonString $item.Section), `"path`": $(ConvertTo-JsonString $item.Path), `"sha256`": `"$($item.Sha256)`", `"size`": $($item.Size) }$separator`n")
}
[void]$manifestBuilder.Append("  ]`n")
[void]$manifestBuilder.Append("}`n")
$manifestBytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($manifestBuilder.ToString())

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$temporaryPath = Join-Path $outputDirectory (".update-package-" + [Guid]::NewGuid().ToString("N") + ".tmp")

function Write-ZipEntry($Archive, [string]$Name, [byte[]]$Bytes) {
    $zipEntry = $Archive.CreateEntry($Name, [System.IO.Compression.CompressionLevel]::Optimal)
    $zipEntry.LastWriteTime = $entryTimestamp
    $stream = $zipEntry.Open()
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
    } finally {
        $stream.Dispose()
    }
}

try {
    $fileStream = [System.IO.File]::Open($temporaryPath, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $archive = New-Object System.IO.Compression.ZipArchive($fileStream,
            [System.IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            Write-ZipEntry $archive "manifest.json" $manifestBytes
            foreach ($item in $loaded) {
                Write-ZipEntry $archive ($item.Section + "/" + $item.Path) $item.Bytes
            }
        } finally {
            $archive.Dispose()
        }
    } finally {
        $fileStream.Dispose()
    }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($temporaryPath)
    try {
        $expectedNames = @("manifest.json") + @($loaded | ForEach-Object { $_.Section + "/" + $_.Path })
        $actualNames = @($archive.Entries | ForEach-Object { $_.FullName })
        if (($expectedNames -join "`n") -cne ($actualNames -join "`n")) {
            Fail "the written archive does not contain exactly the manifest and the listed files"
        }
        foreach ($item in $loaded) {
            $zipEntry = $archive.GetEntry($item.Section + "/" + $item.Path)
            $stream = $zipEntry.Open()
            try {
                $memory = New-Object System.IO.MemoryStream
                $stream.CopyTo($memory)
                $readBack = $memory.ToArray()
            } finally {
                $stream.Dispose()
            }
            if ($readBack.LongLength -ne $item.Size -or (Get-Sha256Hex $readBack) -ne $item.Sha256) {
                Fail "read-back verification failed for $($item.Section)/$($item.Path)"
            }
        }
    } finally {
        $archive.Dispose()
    }

    if (Test-Path -LiteralPath $OutputPath) {
        Remove-Item -LiteralPath $OutputPath -Force
    }
    Move-Item -LiteralPath $temporaryPath -Destination $OutputPath
} finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}

Write-Host ""
Write-Host "Update package ready:" -ForegroundColor Green
Write-Host "  $OutputPath"
Write-Host "  version $Version, $($loaded.Count) files, drivers changed: $([bool]$DriversChanged), usbip $usbipVersion, HidHide $hidHideVersion"
Write-Host "  SHA-256 $((Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash)"
