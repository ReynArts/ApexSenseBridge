param(
    [string]$Version,
    [string]$Publisher,
    [string]$PublisherDisplayName = "ReynArts",
    [string]$CertificatePath,
    [string]$CertificatePassword,
    [switch]$TestCertificate,
    [switch]$Unsigned,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $root "ApexSenseBridgeWidget"
$installScript = Join-Path $PSScriptRoot "Install-ApexSenseBridgeWidget.ps1"
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "dist\widget" }

function Fail($message) {
    Write-Host ""
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

function Get-PublisherId([string]$subject) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash([System.Text.Encoding]::Unicode.GetBytes($subject))
    $bits = (($hash[0..7] | ForEach-Object { [Convert]::ToString($_, 2).PadLeft(8, "0") }) -join "") + "0"
    $alphabet = "0123456789abcdefghjkmnpqrstvwxyz"
    return ((0..12 | ForEach-Object { $alphabet[[Convert]::ToInt32($bits.Substring($_ * 5, 5), 2)] }) -join "")
}

$modes = @(@($(if ($CertificatePath) { "certificate" }), $(if ($TestCertificate) { "test" }), $(if ($Unsigned) { "unsigned" })) | Where-Object { $_ })
if ($modes.Count -ne 1) {
    Fail "Choose exactly one of -CertificatePath (with -CertificatePassword), -TestCertificate or -Unsigned."
}

if (-not $Version) {
    $define = Select-String -LiteralPath (Join-Path $root "installer\ApexSenseBridge.iss") -Pattern '^#define AppVersion "([0-9]+\.[0-9]+\.[0-9]+)"' | Select-Object -First 1
    if (-not $define) { Fail "The application version could not be read from installer\ApexSenseBridge.iss." }
    $Version = $define.Matches[0].Groups[1].Value + ".0"
}
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$') { Fail "The package version must have four numeric parts." }

$signingCertificate = $null
if ($CertificatePath) {
    if (-not (Test-Path -LiteralPath $CertificatePath)) { Fail "Certificate file not found: $CertificatePath" }
    $signingCertificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertificatePath, $CertificatePassword)
    if (-not $Publisher) { $Publisher = $signingCertificate.Subject }
    if ($signingCertificate.Subject -ne $Publisher) {
        Fail "The package publisher '$Publisher' must equal the certificate subject '$($signingCertificate.Subject)'."
    }
}
if (-not $Publisher) { $Publisher = "CN=ReynArts" }

$msbuild = ""
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $installation = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($LASTEXITCODE -eq 0 -and $installation) {
        $candidate = Join-Path $installation "MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            $msbuild = $candidate
            $xamlTargets = Join-Path $installation "MSBuild\Microsoft\WindowsXaml"
        }
    }
}
if (-not $msbuild) { Fail "Visual Studio MSBuild was not found." }
if (-not (Test-Path -LiteralPath $xamlTargets)) {
    Fail "The 'Universal Windows Platform build tools' component of Visual Studio is required to build the widget."
}

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("asb-widget-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force $stage | Out-Null
    $source = Join-Path $stage "project"
    & robocopy $projectDir $source /E /XD bin obj AppPackages BundleArtifacts /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { Fail "The project could not be copied to the staging folder." }

    $manifestPath = Join-Path $source "Package.appxmanifest"
    $manifest = [System.IO.File]::ReadAllText($manifestPath)
    $identity = [regex]::Match($manifest, "<Identity[^>]*>")
    if (-not $identity.Success) { Fail "Package.appxmanifest has no Identity element." }
    $patched = [regex]::Replace($identity.Value, 'Publisher="[^"]*"', ('Publisher="' + [System.Security.SecurityElement]::Escape($Publisher) + '"'))
    $patched = [regex]::Replace($patched, '\sVersion="[^"]*"', (' Version="' + $Version + '"'))
    $manifest = $manifest.Replace($identity.Value, $patched)
    $manifest = [regex]::Replace($manifest, "<PublisherDisplayName>.*?</PublisherDisplayName>", ("<PublisherDisplayName>" + [System.Security.SecurityElement]::Escape($PublisherDisplayName) + "</PublisherDisplayName>"))
    [System.IO.File]::WriteAllText($manifestPath, $manifest, (New-Object System.Text.UTF8Encoding($false)))

    $certificateFile = $null
    $certificatePasswordUsed = $null
    $publicCertificate = $null
    if ($TestCertificate) {
        $friendly = "ApexSenseBridge widget test"
        $certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $Publisher -and $_.FriendlyName -eq $friendly -and $_.NotAfter -gt (Get-Date).AddDays(30) } | Select-Object -First 1
        if (-not $certificate) {
            $certificate = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature -FriendlyName $friendly -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(1) -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
        }
        $certificatePasswordUsed = [guid]::NewGuid().ToString("N")
        $certificateFile = Join-Path $stage "signing.pfx"
        Export-PfxCertificate -Cert $certificate -FilePath $certificateFile -Password (ConvertTo-SecureString -String $certificatePasswordUsed -Force -AsPlainText) | Out-Null
        $publicCertificate = Join-Path $stage "test-certificate.cer"
        Export-Certificate -Cert $certificate -FilePath $publicCertificate | Out-Null
        $thumbprint = $certificate.Thumbprint
    } elseif ($CertificatePath) {
        $certificateFile = $CertificatePath
        $certificatePasswordUsed = $CertificatePassword
        $thumbprint = $signingCertificate.Thumbprint
    }

    $project = Join-Path $source "ApexSenseBridgeWidget.csproj"
    $packageDirectory = Join-Path $stage "pkg"
    $buildArguments = @(
        $project, "/t:Rebuild", "/restore", "/p:Configuration=Release", "/p:Platform=x64",
        "/p:UseDotNetNativeToolchain=true", "/p:AppxBundle=Never", "/p:GenerateAppxPackageOnBuild=true",
        "/p:GenerateTestArtifacts=true", "/p:AppxPackageDir=$packageDirectory\", "/p:AppxPackageTestDir=$packageDirectory\test\",
        "/nologo", "/v:minimal", "/clp:ErrorsOnly;WarningsOnly;Summary"
    )
    if ($Unsigned) {
        $buildArguments += "/p:AppxPackageSigningEnabled=false"
    } else {
        $buildArguments += @("/p:AppxPackageSigningEnabled=true", "/p:PackageCertificateKeyFile=$certificateFile", "/p:PackageCertificatePassword=$certificatePasswordUsed", "/p:PackageCertificateThumbprint=$thumbprint")
    }

    Write-Host "Building the widget package $Version (publisher $Publisher)..."
    & $msbuild @buildArguments
    if ($LASTEXITCODE -ne 0) { Fail "The widget build failed." }

    $built = Join-Path $packageDirectory "test"
    $msix = Get-ChildItem -LiteralPath $built -Filter "*.msix" | Select-Object -First 1
    if (-not $msix) { Fail "No .msix package was produced." }
    $dependencies = Join-Path $built "Dependencies\x64"
    if (-not (Test-Path -LiteralPath $dependencies)) { Fail "The x64 framework dependencies were not produced." }

    if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Force (Join-Path $OutputDirectory "Dependencies") | Out-Null
    Copy-Item -LiteralPath $msix.FullName -Destination $OutputDirectory
    Copy-Item -Path (Join-Path $dependencies "*.appx") -Destination (Join-Path $OutputDirectory "Dependencies")
    Copy-Item -LiteralPath $installScript -Destination $OutputDirectory
    if ($publicCertificate) { Copy-Item -LiteralPath $publicCertificate -Destination (Join-Path $OutputDirectory "ApexSenseBridgeWidget-test-certificate.cer") }

    $finalPackage = Join-Path $OutputDirectory $msix.Name
    $status = (Get-AuthenticodeSignature -LiteralPath $finalPackage).Status
    $family = "ReynArts.ApexSenseBridgeWidget_" + (Get-PublisherId $Publisher)
    $zip = $null
    if (-not $Unsigned) {
        $zip = Join-Path (Split-Path -Parent $OutputDirectory) ("ApexSenseBridgeWidget-" + $Version + "-x64.zip")
        if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
        Compress-Archive -Path (Join-Path $OutputDirectory "*") -DestinationPath $zip
    }

    Write-Host ""
    Write-Host "Package:           $finalPackage"
    Write-Host "Signature:         $status"
    Write-Host "Package family:    $family"
    Write-Host "SHA256:            $((Get-FileHash -LiteralPath $finalPackage -Algorithm SHA256).Hash)"
    if ($zip) { Write-Host "Archive:           $zip" }
    if ($Unsigned) { Write-Host "Unsigned: sign the .msix with a certificate whose subject is exactly '$Publisher', then add it to an archive with Dependencies and Install-ApexSenseBridgeWidget.ps1." }
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
}
