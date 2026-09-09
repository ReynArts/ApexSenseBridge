$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$installerPath = Join-Path $projectRoot "installer\ApexSenseBridge.iss"
$manifestPath = Join-Path $projectRoot "installer\driver-manifest.json"
$installer = Get-Content -LiteralPath $installerPath -Raw
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Installer registry contract failed: $Message"
    }
}

function Get-QuotedDefine([string]$Name) {
    $pattern = '(?m)^\s*#define\s+' + [Regex]::Escape($Name) +
               '\s+"([^"]+)"\s*$'
    $match = [Regex]::Match($installer, $pattern)
    Assert-Contract $match.Success "missing quoted define $Name"
    return $match.Groups[1].Value
}

$usbipProductCode = [string]$manifest.'usbip-win2'.productCode
$hidHideProductCode = [string]$manifest.HidHide.productCode

Assert-Contract (
    (Get-QuotedDefine "UsbipProductKeyPascal") -ceq $usbipProductCode
) "USBip Pascal key differs from driver-manifest.json"
Assert-Contract (
    (Get-QuotedDefine "UsbipProductKey") -ceq ('{' + $usbipProductCode)
) "USBip section key is not escaped exactly once for Inno Setup"
Assert-Contract (
    (Get-QuotedDefine "HidHideProductCodePascal") -ceq $hidHideProductCode
) "HidHide Pascal key differs from driver-manifest.json"
Assert-Contract (
    (Get-QuotedDefine "HidHideProductCode") -ceq ('{' + $hidHideProductCode)
) "HidHide section key is not escaped exactly once for Inno Setup"

Assert-Contract ([Regex]::IsMatch(
    $installer,
    '(?s)function\s+UsbipUninstallKey:\s*String;.*?' +
    '\{#UsbipProductKeyPascal\}.*?end;'
)) "UsbipUninstallKey does not use the verbatim Pascal key"
Assert-Contract ([Regex]::IsMatch(
    $installer,
    '(?s)function\s+HidHideUninstallKey:\s*String;.*?' +
    '\{#HidHideProductCodePascal\}.*?end;'
)) "HidHideUninstallKey does not use the verbatim Pascal key"

Write-Output "Installer registry contract passed."
