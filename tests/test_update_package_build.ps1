$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $projectRoot "scripts\build-update-package.ps1"
$verifyScript = Join-Path $projectRoot "scripts\verify-version-consistency.ps1"
$installerBuildScript = Join-Path $projectRoot "scripts\build-installer.ps1"
$workflowPath = Join-Path $projectRoot ".github\workflows\release.yml"
$realInstaller = Join-Path $projectRoot "installer\ApexSenseBridge.iss"
$realDriverManifest = Join-Path $projectRoot "installer\driver-manifest.json"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("asb-update-package-test-" + [Guid]::NewGuid().ToString("N"))

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Update package contract failed: $Message"
    }
}

function Assert-Throws([scriptblock]$Action, [string]$Pattern, [string]$Message) {
    $thrown = $null
    try {
        & $Action | Out-Null
    } catch {
        $thrown = $_.Exception.Message
    }
    Assert-Contract ($null -ne $thrown) "$Message (nothing was thrown)"
    Assert-Contract ($thrown -match $Pattern) "$Message (got: $thrown)"
}

function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString("x2") }) -join "")
    } finally {
        $sha.Dispose()
    }
}

function Read-ZipEntryBytes($Archive, [string]$Name) {
    $stream = $Archive.GetEntry($Name).Open()
    try {
        $memory = New-Object IO.MemoryStream
        $stream.CopyTo($memory)
        return $memory.ToArray()
    } finally {
        $stream.Dispose()
    }
}

function Read-Package([string]$Path) {
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $manifestBytes = Read-ZipEntryBytes $archive "manifest.json"
        $entries = @{}
        foreach ($entry in $archive.Entries) {
            $entries[$entry.FullName] = Read-ZipEntryBytes $archive $entry.FullName
        }
        return [pscustomobject]@{
            Names = @($archive.Entries | ForEach-Object { $_.FullName })
            ManifestBytes = $manifestBytes
            ManifestText = (New-Object Text.UTF8Encoding($false, $true)).GetString($manifestBytes)
            Entries = $entries
        }
    } finally {
        $archive.Dispose()
    }
}

function Copy-Zip([string]$Source, [string]$Destination) {
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Edit-Zip([string]$Path, [scriptblock]$Edit) {
    $archive = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Update)
    try {
        & $Edit $archive
    } finally {
        $archive.Dispose()
    }
}

function Set-ZipEntryBytes($Archive, [string]$Name, [byte[]]$Bytes) {
    $existing = $Archive.GetEntry($Name)
    if ($null -ne $existing) { $existing.Delete() }
    $entry = $Archive.CreateEntry($Name)
    $stream = $entry.Open()
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
    } finally {
        $stream.Dispose()
    }
}

function New-FixtureProject([string]$Root) {
    New-Item -ItemType Directory -Path (Join-Path $Root "installer") -Force | Out-Null
    Copy-Item -LiteralPath $realInstaller -Destination (Join-Path $Root "installer\ApexSenseBridge.iss")
    Copy-Item -LiteralPath $realDriverManifest -Destination (Join-Path $Root "installer\driver-manifest.json")
    $installerDirectory = Join-Path $Root "installer"
    $inFiles = $false
    foreach ($line in (Get-Content -LiteralPath $realInstaller)) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith("[")) {
            $inFiles = $trimmed -ieq "[Files]"
            continue
        }
        $match = [Regex]::Match($trimmed, '^Source:\s*"([^"]+)"')
        if (-not $inFiles -or -not $match.Success) { continue }
        $source = $match.Groups[1].Value -replace '\{#UsbipInstaller\}', "USBip-0.9.8.0-x64.exe"
        if ($source.Contains("*")) { continue }
        $path = [IO.Path]::GetFullPath((Join-Path $installerDirectory $source))
        if (Test-Path -LiteralPath $path -PathType Leaf) { continue }
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [IO.File]::WriteAllText($path, "fixture:" + (Split-Path -Leaf $path), (New-Object Text.UTF8Encoding($false)))
    }
    $localization = Join-Path $Root "playnite\ApexSenseBridge\bin\Release\Localization"
    New-Item -ItemType Directory -Path (Join-Path $localization "extra") -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $localization "en_US.xaml"), "en", (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $localization "fr_FR.xaml"), "fr", (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText((Join-Path $localization "extra\deep.xaml"), "deep", (New-Object Text.UTF8Encoding($false)))
}

function Invoke-Build([string]$Root, [string]$Output, [string[]]$ExtraArguments = @()) {
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-ProjectRoot", $Root, "-Version", "9.8.7", "-OutputPath", $Output) + $ExtraArguments
    $ErrorActionPreference = "Continue"
    $output = & powershell.exe @arguments 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

$tokens = $null
$parseErrors = $null
foreach ($scriptPath in @($buildScript, $verifyScript, $installerBuildScript)) {
    [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
    Assert-Contract ($parseErrors.Count -eq 0) "$scriptPath does not parse"
}
$verifyAst = [System.Management.Automation.Language.Parser]::ParseFile($verifyScript, [ref]$tokens, [ref]$parseErrors)
$verifyFunction = @($verifyAst.FindAll({
    param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Assert-UpdatePackage"
}, $false))
Assert-Contract ($verifyFunction.Count -eq 1) "verify-version-consistency.ps1 lacks Assert-UpdatePackage"

$workflowText = [IO.File]::ReadAllText($workflowPath)
Assert-Contract ($workflowText.Contains("dist/ApexSenseBridge-Update.zip")) "release.yml does not publish the update package"
$installerBuildText = [IO.File]::ReadAllText($installerBuildScript)
Assert-Contract ($installerBuildText.Contains("build-update-package.ps1")) "build-installer.ps1 does not build the update package"
Assert-Contract ($installerBuildText.IndexOf("build-update-package.ps1") -gt $installerBuildText.IndexOf("sign-windows-artifacts.ps1")) `
    "the update package must be built after the payload is signed"

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $loadedPath = Join-Path $testRoot "loaded-functions.ps1"
    $loadedText = "function Fail([string]`$Message) { throw `"ApexSenseBridge release contract: `$Message`" }`r`n" +
        $verifyFunction[0].Extent.Text
    [IO.File]::WriteAllText($loadedPath, $loadedText, (New-Object Text.UTF8Encoding($false)))
    . $loadedPath

    $fixture = Join-Path $testRoot "fixture"
    New-FixtureProject $fixture
    Remove-Item -LiteralPath (Join-Path $fixture "build-win\Release\ApexSenseBridgeUpdater.exe") -Force -ErrorAction SilentlyContinue
    $zipA = Join-Path $testRoot "out\a.zip"
    $zipB = Join-Path $testRoot "out\b.zip"
    $driverManifestPath = Join-Path $fixture "installer\driver-manifest.json"

    $first = Invoke-Build $fixture $zipA
    Assert-Contract ($first.ExitCode -eq 0) "the fixture build failed: $($first.Output)"
    Assert-Contract ($first.Output -match "ApexSenseBridgeUpdater\.exe (was not found|is listed by the installer but missing)") `
        "a missing updater must produce a clear warning: $($first.Output)"

    $second = Invoke-Build $fixture $zipB
    Assert-Contract ($second.ExitCode -eq 0) "the second fixture build failed"
    Assert-Contract ((Get-FileHash -LiteralPath $zipA).Hash -eq (Get-FileHash -LiteralPath $zipB).Hash) `
        "two builds of the same inputs are not byte-identical"

    $package = Read-Package $zipA
    Assert-Contract ($package.Names[0] -ceq "manifest.json") "manifest.json must be the first entry"
    Assert-Contract (-not ($package.ManifestBytes.Length -ge 3 -and $package.ManifestBytes[0] -eq 0xEF -and
        $package.ManifestBytes[1] -eq 0xBB -and $package.ManifestBytes[2] -eq 0xBF)) "manifest.json has a BOM"
    $manifest = $package.ManifestText | ConvertFrom-Json
    Assert-Contract ($manifest.schema -eq 1) "schema must be 1"
    Assert-Contract ($manifest.version -ceq "9.8.7") "manifest version must be the requested version"
    Assert-Contract ($manifest.driversChanged -is [bool] -and -not $manifest.driversChanged) "driversChanged must default to false"
    $drivers = Get-Content -LiteralPath $driverManifestPath -Raw | ConvertFrom-Json
    Assert-Contract ($manifest.usbipVersion -ceq $drivers.'usbip-win2'.version) "usbipVersion must come from driver-manifest.json"
    Assert-Contract ($manifest.hidHideVersion -ceq $drivers.HidHide.version) "hidHideVersion must come from driver-manifest.json"

    $files = @($manifest.files)
    Assert-Contract ($package.Names.Count -eq $files.Count + 1) "the zip must hold exactly manifest.json plus the listed files"
    $listedNames = @($files | ForEach-Object { "$($_.section)/$($_.path)" })
    Assert-Contract ((@($package.Names | Select-Object -Skip 1) -join "`n") -ceq ($listedNames -join "`n")) `
        "zip entries and manifest entries must match one to one, in the same order"
    $sortKeys = @($files | ForEach-Object { "$(if ($_.section -eq 'app') { 0 } else { 1 })|$($_.path)" })
    $sortedKeys = New-Object 'System.Collections.Generic.List[string]'
    $sortKeys | ForEach-Object { $sortedKeys.Add($_) }
    $sortedKeys.Sort([StringComparer]::Ordinal)
    Assert-Contract (($sortKeys -join "`n") -ceq ($sortedKeys -join "`n")) "manifest entries must be sorted ordinally by section then path"
    foreach ($file in $files) {
        $bytes = $package.Entries["$($file.section)/$($file.path)"]
        Assert-Contract ($file.sha256 -cmatch '^[0-9a-f]{64}$') "sha256 must be lowercase hex: $($file.path)"
        Assert-Contract ((Get-Sha256Hex $bytes) -ceq $file.sha256) "hash mismatch for $($file.path)"
        Assert-Contract ($bytes.LongLength -eq $file.size) "size mismatch for $($file.path)"
        Assert-Contract ($file.section -cin @("app", "playnite")) "unexpected section for $($file.path)"
    }
    foreach ($name in $listedNames) {
        Assert-Contract ($name -notmatch '(?i)USBip-[^/]*\.exe|HidHide_[^/]*\.exe|install-usbip|Uninstaller|unins\d|\.e32|Setup\.exe') `
            "a prerequisite, uninstaller or setup file leaked into the package: $name"
    }
    foreach ($required in @("app/ApexSenseBridge.exe", "app/ApexSenseBridgeTray.exe", "app/Licenses/driver-manifest.json",
                            "app/Licenses/ApexSenseBridge-LICENSE.txt", "app/Resources/app.ico", "app/Data/supported_games.json",
                            "playnite/ApexSenseBridge.dll", "playnite/extension.yaml", "playnite/icon.png",
                            "playnite/Localization/en_US.xaml", "playnite/Localization/fr_FR.xaml",
                            "playnite/Localization/extra/deep.xaml")) {
        Assert-Contract ($listedNames -ccontains $required) "missing from the package: $required"
    }
    Assert-Contract (-not ($listedNames -ccontains "app/ApexSenseBridgeUpdater.exe")) "the updater must be absent when it was not built"
    Assert-Contract (-not ($listedNames | Where-Object { $_ -match '\\' })) "paths must use forward slashes"

    Assert-UpdatePackage $zipA "9.8.7" $driverManifestPath | Out-Null
    Assert-Throws { Assert-UpdatePackage $zipA "9.8.8" $driverManifestPath } "manifest version" `
        "a manifest version differing from the project version must be rejected"

    $withUpdaterRoot = Join-Path $testRoot "with-updater"
    New-FixtureProject $withUpdaterRoot
    [IO.File]::WriteAllText((Join-Path $withUpdaterRoot "build-win\Release\ApexSenseBridgeUpdater.exe"), "updater",
        (New-Object Text.UTF8Encoding($false)))
    $zipUpdater = Join-Path $testRoot "out\updater.zip"
    $withUpdater = Invoke-Build $withUpdaterRoot $zipUpdater @("-DriversChanged")
    Assert-Contract ($withUpdater.ExitCode -eq 0) "the build with the updater failed: $($withUpdater.Output)"
    Assert-Contract ($withUpdater.Output -notmatch "ApexSenseBridgeUpdater\.exe was not found") "no warning is expected when the updater exists"
    $updaterPackage = Read-Package $zipUpdater
    Assert-Contract ($updaterPackage.Names -ccontains "app/ApexSenseBridgeUpdater.exe") "the updater must be packaged under app when present"
    $updaterManifest = $updaterPackage.ManifestText | ConvertFrom-Json
    Assert-Contract ($updaterManifest.driversChanged -is [bool] -and $updaterManifest.driversChanged) "-DriversChanged must set driversChanged to true"
    Assert-UpdatePackage $zipUpdater "9.8.7" (Join-Path $withUpdaterRoot "installer\driver-manifest.json") | Out-Null

    $tamperedHash = Join-Path $testRoot "out\tampered-hash.zip"
    Copy-Zip $zipA $tamperedHash
    Edit-Zip $tamperedHash { param($archive)
        Set-ZipEntryBytes $archive "app/ApexSenseBridge.exe" ([Text.Encoding]::ASCII.GetBytes("fixture:ApexSenseBridge.exX"))
    }
    Assert-Throws { Assert-UpdatePackage $tamperedHash "9.8.7" $driverManifestPath } "SHA-256 mismatch" `
        "a same-size tampered file must be caught by its hash"

    $tamperedSize = Join-Path $testRoot "out\tampered-size.zip"
    Copy-Zip $zipA $tamperedSize
    Edit-Zip $tamperedSize { param($archive)
        Set-ZipEntryBytes $archive "app/viiper.exe" ([Text.Encoding]::ASCII.GetBytes("tampered and longer"))
    }
    Assert-Throws { Assert-UpdatePackage $tamperedSize "9.8.7" $driverManifestPath } "size mismatch" `
        "a resized file must be caught"

    $extraEntry = Join-Path $testRoot "out\extra-entry.zip"
    Copy-Zip $zipA $extraEntry
    Edit-Zip $extraEntry { param($archive) Set-ZipEntryBytes $archive "app/extra.exe" ([byte[]](1, 2, 3)) }
    Assert-Throws { Assert-UpdatePackage $extraEntry "9.8.7" $driverManifestPath } "holds \d+ files but its manifest lists" `
        "an entry missing from the manifest must be rejected"

    $traversal = Join-Path $testRoot "out\traversal.zip"
    Copy-Zip $zipA $traversal
    Edit-Zip $traversal { param($archive) Set-ZipEntryBytes $archive "app/../evil.dll" ([byte[]](1)) }
    Assert-Throws { Assert-UpdatePackage $traversal "9.8.7" $driverManifestPath } "unsafe entry name" `
        "a path traversal entry must be rejected"

    $missingEntry = Join-Path $testRoot "out\missing-entry.zip"
    Copy-Zip $zipA $missingEntry
    Edit-Zip $missingEntry { param($archive)
        $archive.GetEntry("app/libVIIPER.dll").Delete()
        Set-ZipEntryBytes $archive "app/libviiper-other.dll" ([byte[]](1))
    }
    Assert-Throws { Assert-UpdatePackage $missingEntry "9.8.7" $driverManifestPath } "not in the zip" `
        "a manifest entry absent from the zip must be rejected"

    $bomManifest = Join-Path $testRoot "out\bom.zip"
    Copy-Zip $zipA $bomManifest
    Edit-Zip $bomManifest { param($archive)
        $bytes = [byte[]](0xEF, 0xBB, 0xBF) + $package.ManifestBytes
        Set-ZipEntryBytes $archive "manifest.json" $bytes
    }
    Assert-Throws { Assert-UpdatePackage $bomManifest "9.8.7" $driverManifestPath } "without a BOM" `
        "a manifest with a BOM must be rejected"

    $wrongDrivers = Join-Path $testRoot "out\wrong-drivers.zip"
    Copy-Zip $zipA $wrongDrivers
    Edit-Zip $wrongDrivers { param($archive)
        $text = $package.ManifestText.Replace('"usbipVersion": "0.9.8.0"', '"usbipVersion": "0.0.0.1"')
        Set-ZipEntryBytes $archive "manifest.json" ((New-Object Text.UTF8Encoding($false)).GetBytes($text))
    }
    Assert-Throws { Assert-UpdatePackage $wrongDrivers "9.8.7" $driverManifestPath } "usbipVersion" `
        "a driver version differing from driver-manifest.json must be rejected"

    $prerequisiteRoot = Join-Path $testRoot "with-prerequisite"
    New-FixtureProject $prerequisiteRoot
    $prerequisiteScript = Join-Path $prerequisiteRoot "installer\ApexSenseBridge.iss"
    [IO.File]::AppendAllText($prerequisiteScript,
        "`r`n[Files]`r`nSource: `"..\third_party\prerequisites\USBip-0.9.8.0-x64.exe`"; DestDir: `"{app}\Drivers`"; Flags: ignoreversion`r`n")
    $refused = Invoke-Build $prerequisiteRoot (Join-Path $testRoot "out\refused.zip")
    Assert-Contract ($refused.ExitCode -ne 0 -and $refused.Output -match "refusing to package") `
        "a prerequisite installer listed for {app} must be refused: $($refused.Output)"
    Assert-Contract (-not (Test-Path -LiteralPath (Join-Path $testRoot "out\refused.zip"))) "a refused build must not leave a package"

    $uninstallerRoot = Join-Path $testRoot "with-uninstaller"
    New-FixtureProject $uninstallerRoot
    [IO.File]::AppendAllText((Join-Path $uninstallerRoot "installer\ApexSenseBridge.iss"),
        "`r`n[Files]`r`nSource: `"..\build-win\Release\unins000.exe`"; DestDir: `"{app}`"; Flags: ignoreversion`r`n")
    [IO.File]::WriteAllText((Join-Path $uninstallerRoot "build-win\Release\unins000.exe"), "x", (New-Object Text.UTF8Encoding($false)))
    $refusedUninstaller = Invoke-Build $uninstallerRoot (Join-Path $testRoot "out\refused2.zip")
    Assert-Contract ($refusedUninstaller.ExitCode -ne 0 -and $refusedUninstaller.Output -match "refusing to package") `
        "an uninstaller listed for {app} must be refused"

    $unknownRoot = Join-Path $testRoot "with-unknown-destination"
    New-FixtureProject $unknownRoot
    [IO.File]::AppendAllText((Join-Path $unknownRoot "installer\ApexSenseBridge.iss"),
        "`r`n[Files]`r`nSource: `"..\LICENSE`"; DestDir: `"{sys}`"; Flags: ignoreversion`r`n")
    $refusedUnknown = Invoke-Build $unknownRoot (Join-Path $testRoot "out\refused3.zip")
    Assert-Contract ($refusedUnknown.ExitCode -ne 0 -and $refusedUnknown.Output -match "destination is not understood") `
        "an installer destination outside app and the Playnite extension must be refused"

    $duplicateRoot = Join-Path $testRoot "with-duplicate"
    New-FixtureProject $duplicateRoot
    [IO.File]::AppendAllText((Join-Path $duplicateRoot "installer\ApexSenseBridge.iss"),
        "`r`n[Files]`r`nSource: `"..\LICENSE`"; DestDir: `"{app}`"; DestName: `"apexsensebridge.EXE`"; Flags: ignoreversion`r`n")
    $refusedDuplicate = Invoke-Build $duplicateRoot (Join-Path $testRoot "out\refused4.zip")
    Assert-Contract ($refusedDuplicate.ExitCode -ne 0 -and $refusedDuplicate.Output -match "duplicate package path") `
        "a case-insensitive duplicate destination must be refused"

    $realBuild = $null
    $realReleaseDirectory = Join-Path $projectRoot "build-win\Release"
    if (Test-Path -LiteralPath (Join-Path $realReleaseDirectory "ApexSenseBridgeTray.exe")) {
        $realZip = Join-Path $testRoot "out\real.zip"
        $realBuild = Invoke-Build $projectRoot $realZip @()
        $realText = $realBuild.Output
        if ($realBuild.ExitCode -eq 0) {
            $realPackage = Read-Package $realZip
            $realManifest = $realPackage.ManifestText | ConvertFrom-Json
            foreach ($file in @($realManifest.files)) {
                Assert-Contract ((Get-Sha256Hex $realPackage.Entries["$($file.section)/$($file.path)"]) -ceq $file.sha256) `
                    "real build hash mismatch for $($file.path)"
            }
        } else {
            Assert-Contract ($realText -match "required file is missing|matched no file") `
                "the build from the real outputs failed for an unexpected reason: $realText"
        }
    }
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Output "Update package build contract passed."
