$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$updatePath = Join-Path $projectRoot "scripts\update-app.ps1"
$releaseCheckPath = Join-Path $projectRoot "scripts\verify-version-consistency.ps1"
$securityPath = Join-Path $projectRoot "shared\ReleaseSecurity.cs"
$trayUpdaterPath = Join-Path $projectRoot "ApexSenseBridgeTray\Services\UpdateCheckerService.cs"
$playniteUpdaterPath = Join-Path $projectRoot "playnite\ApexSenseBridge\UpdateManager.cs"
$updateText = [IO.File]::ReadAllText($updatePath)
$releaseCheckText = [IO.File]::ReadAllText($releaseCheckPath)
$securityText = [IO.File]::ReadAllText($securityPath)
$trayUpdaterText = [IO.File]::ReadAllText($trayUpdaterPath)
$playniteUpdaterText = [IO.File]::ReadAllText($playniteUpdaterPath)
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("asb-updater-identity-test-" + [Guid]::NewGuid().ToString("N"))

function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Updater identity contract failed: $Message"
    }
}

foreach ($consumer in @(
    @{ Name = "Tray updater"; Text = $trayUpdaterText },
    @{ Name = "Playnite updater"; Text = $playniteUpdaterText }
)) {
    Assert-Contract ($consumer.Text.Contains("ReleaseSecurity.VerifyInstaller(")) `
        "$($consumer.Name) does not verify the setup through ReleaseSecurity"
    Assert-Contract (-not [Regex]::IsMatch(
        $consumer.Text, '\.(ProductName|ProductVersion|FileVersion)\b|FileVersionInfo')) `
        "$($consumer.Name) reads installer version resources itself instead of ReleaseSecurity"
}

Assert-Contract (-not [Regex]::IsMatch(
    $securityText, 'Equals\(\s*versionInfo\.Product|TryParse\(\s*versionInfo\.Product')) `
    "ReleaseSecurity compares raw, unnormalized version resource strings"
Assert-Contract ($securityText.Contains("IsExpectedProductName(versionInfo.ProductName)")) `
    "the product name is not checked through the padding-tolerant helper"
Assert-Contract ($securityText.Contains("MatchesReleaseVersion(versionInfo.ProductVersion, expectedVersion)")) `
    "the product version is not checked through the padding-tolerant helper"
Assert-Contract ($securityText.Contains("IsSamePublisher(installerCertificate.Subject,")) `
    "the signer comparison is not routed through the strict publisher helper"
Assert-Contract ($securityText.Contains("return value == '\0' || char.IsWhiteSpace(value);")) `
    "NUL padding is not trimmed alongside whitespace"

Assert-Contract ($updateText.Contains('Test-SetupProductName $setupInfo.ProductName')) `
    "update-app.ps1 does not check the product name through the padding-tolerant helper"
Assert-Contract ($updateText.Contains('ConvertTo-VersionField $setupInfo.ProductVersion')) `
    "update-app.ps1 does not normalize the setup product version"
Assert-Contract (-not [Regex]::IsMatch($updateText, '\.ProductName\s+-c?ne|\.ProductName\s+-c?eq')) `
    "update-app.ps1 compares the raw product name"
Assert-Contract ([Regex]::IsMatch(
    $releaseCheckText, '\.Trim\(\s*\[char\[\]\]\(0, 9, 10, 11, 12, 13, 32, 133, 160\)\)')) `
    "the release artifact check does not trim NUL padding"

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($updatePath, [ref]$tokens, [ref]$parseErrors)
Assert-Contract ($parseErrors.Count -eq 0) "update-app.ps1 does not parse"
$wanted = @("ConvertTo-VersionField", "Test-SetupProductName", "Test-SameReleaseVersion")
$functionAsts = @($ast.FindAll({
    param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst]
}, $false) | Where-Object { $_.Name -in $wanted })
Assert-Contract ($functionAsts.Count -eq $wanted.Count) "update-app.ps1 lacks one of: $($wanted -join ', ')"

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $loadedPath = Join-Path $testRoot "loaded-functions.ps1"
    [IO.File]::WriteAllText(
        $loadedPath,
        (($functionAsts | ForEach-Object { $_.Extent.Text }) -join "`r`n"),
        (New-Object Text.UTF8Encoding($false)))
    . $loadedPath

    $padding = " " * 45
    $nul = [string][char]0

    Assert-Contract ((ConvertTo-VersionField $null) -ceq "") "a missing field must normalize to empty"
    Assert-Contract ((ConvertTo-VersionField (" " * 20)) -ceq "") "a blank Inno FileVersion must normalize to empty"
    Assert-Contract ((ConvertTo-VersionField ("ApexSenseBridge" + $padding)) -ceq "ApexSenseBridge") `
        "trailing Inno padding must be removed"
    Assert-Contract ((ConvertTo-VersionField ($nul + " `t1.0.2 " + $nul + "`r`n " + $nul)) -ceq "1.0.2") `
        "mixed whitespace and NUL padding must be removed"
    Assert-Contract ((ConvertTo-VersionField "Apex SenseBridge") -ceq "Apex SenseBridge") `
        "interior spaces must be preserved"
    Assert-Contract ((ConvertTo-VersionField ("Apex" + $nul + "SenseBridge")) -ceq ("Apex" + $nul + "SenseBridge")) `
        "interior NUL characters must be preserved"

    foreach ($accepted in @(
        "ApexSenseBridge",
        ("ApexSenseBridge" + $padding),
        ("ApexSenseBridge" + $nul + $nul),
        ("  apexsensebridge " + $nul + " ")
    )) {
        Assert-Contract (Test-SetupProductName $accepted) "padded product name rejected: [$accepted]"
    }
    foreach ($rejected in @(
        $null,
        "",
        $padding,
        "ApexSenseBridgeTray",
        ("ApexSenseBridge Setup" + $padding),
        "Apex SenseBridge",
        ("ApexSenseBridge" + $nul + "Tray"),
        ("USBip" + $padding)
    )) {
        Assert-Contract (-not (Test-SetupProductName $rejected)) "wrong product name accepted: [$rejected]"
    }

    foreach ($pair in @(
        @(("1.0.2" + $padding), "1.0.2"),
        @(("1.0.2" + $padding), "v1.0.2"),
        @(("1.0.2" + $nul + $padding), "1.0.2"),
        @(" 1.0.2.0 ", " V1.0.2 ")
    )) {
        Assert-Contract (Test-SameReleaseVersion $pair[0] $pair[1]) `
            "padded version rejected: [$($pair[0])] vs [$($pair[1])]"
    }
    foreach ($pair in @(
        @(("1.0.3" + $padding), "1.0.2"),
        @(("1.1.2" + $padding), "1.0.2"),
        @(("2.0.2" + $padding), "1.0.2"),
        @(("1.0" + $padding), "1.0.2"),
        @(("garbage" + $padding), "1.0.2"),
        @("", "1.0.2"),
        @("1.0.2", "garbage")
    )) {
        Assert-Contract (-not (Test-SameReleaseVersion $pair[0] $pair[1])) `
            "wrong version accepted: [$($pair[0])] vs [$($pair[1])]"
    }

    $innoFixture = Join-Path $projectRoot "third_party\prerequisites\USBip-0.9.8.0-x64.exe"
    Assert-Contract (Test-Path -LiteralPath $innoFixture -PathType Leaf) "the Inno Setup fixture is missing"
    $genuine = (Get-Item -LiteralPath $innoFixture).VersionInfo
    Assert-Contract ($genuine.ProductName.Length -gt "USBip".Length -and $genuine.ProductName -ne "USBip") `
        "a genuine Inno Setup binary no longer shows the padding this contract is about"
    Assert-Contract ((ConvertTo-VersionField $genuine.ProductName) -ceq "USBip") `
        "the padded product name of a genuine Inno Setup binary is not normalized"
    Assert-Contract (Test-SameReleaseVersion $genuine.ProductVersion "v0.9.8") `
        "the padded product version of a genuine Inno Setup binary is not accepted"
    Assert-Contract (-not (Test-SameReleaseVersion $genuine.ProductVersion "v0.9.9")) `
        "a different release must still be rejected for a genuine Inno Setup binary"
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Output "Updater identity contract passed."
