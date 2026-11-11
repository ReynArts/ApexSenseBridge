param(
    [switch]$Uninstall,
    [switch]$TrustCertificate
)

$ErrorActionPreference = "Stop"
$identityName = "ReynArts.ApexSenseBridgeWidget"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Uninstall) {
    $installed = Get-AppxPackage -Name $identityName
    if (-not $installed) {
        Write-Host "The ApexSenseBridge widget is not installed."
        return
    }
    $installed | Remove-AppxPackage
    Write-Host "The ApexSenseBridge widget was removed."
    return
}

if ([Environment]::OSVersion.Version.Build -lt 17763) {
    throw "The Game Bar widget needs Windows 10 version 1809 (build 17763) or later."
}
if (-not (Get-AppxPackage -Name "Microsoft.XboxGamingOverlay")) {
    Write-Warning "Xbox Game Bar is not installed for this user. Install it from the Microsoft Store, then open it with Win+G."
}

$package = Get-ChildItem -LiteralPath $here -Filter "ApexSenseBridgeWidget_*_x64.msix" | Select-Object -First 1
if (-not $package) {
    throw "No ApexSenseBridgeWidget_*_x64.msix next to this script."
}

$signature = Get-AuthenticodeSignature -LiteralPath $package.FullName
if ($signature.Status -eq "NotSigned") {
    throw "The package is not signed. Sign it before installing."
}
if ($signature.Status -ne "Valid") {
    $certificate = Get-ChildItem -LiteralPath $here -Filter "*.cer" | Select-Object -First 1
    if ($TrustCertificate -and $certificate) {
        Write-Host "Trusting $($certificate.Name) for this computer (administrator approval required)..."
        $command = "Import-Certificate -FilePath '$($certificate.FullName)' -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null"
        $process = Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile", "-Command", $command -Verb RunAs -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "The certificate could not be trusted." }
    } else {
        throw "The package signature is not trusted ($($signature.Status)). A package signed with the test certificate needs -TrustCertificate."
    }
}

function Get-DependencyIdentity([string]$path) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($path)
    try {
        $entry = $archive.GetEntry("AppxManifest.xml")
        $reader = New-Object System.IO.StreamReader($entry.Open())
        try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
        return $manifest.Package.Identity
    } finally {
        $archive.Dispose()
    }
}

$missing = @()
$dependencyFolder = Join-Path $here "Dependencies"
if (Test-Path -LiteralPath $dependencyFolder) {
    foreach ($file in Get-ChildItem -LiteralPath $dependencyFolder -Filter "*.appx") {
        $identity = Get-DependencyIdentity $file.FullName
        $wanted = [version]$identity.Version
        $present = Get-AppxPackage -Name $identity.Name | Where-Object { $_.Architecture -eq $identity.ProcessorArchitecture -and [version]$_.Version -ge $wanted }
        if (-not $present) { $missing += $file.FullName }
    }
}

$arguments = @{ Path = $package.FullName; ForceApplicationShutdown = $true }
if ($missing.Count -gt 0) { $arguments["DependencyPath"] = $missing }
Add-AppxPackage @arguments

$installed = Get-AppxPackage -Name $identityName
Write-Host "Installed $($installed.PackageFullName)."
Write-Host "Open Xbox Game Bar (Win+G), open the widget menu and choose ApexSenseBridge. The ApexSenseBridge Tray or the Playnite extension must be running."
