[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,
    [string]$CertificatePath = $env:ASB_SIGNING_CERTIFICATE_PATH,
    [string]$CertificateBase64 = $env:ASB_SIGNING_CERTIFICATE_BASE64,
    [string]$CertificatePassword = $env:ASB_SIGNING_CERTIFICATE_PASSWORD,
    [string]$CertificateThumbprint = $env:ASB_SIGNING_CERTIFICATE_THUMBPRINT,
    [string]$TimestampUrl = $env:ASB_SIGNING_TIMESTAMP_URL,
    [string]$SignToolPath = "",
    [switch]$MachineStore,
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    throw "ApexSenseBridge signing: $Message"
}

function Find-SignTool {
    if (-not [string]::IsNullOrWhiteSpace($SignToolPath)) {
        if (-not (Test-Path -LiteralPath $SignToolPath -PathType Leaf)) {
            Fail "SignTool was not found at $SignToolPath"
        }
        return [System.IO.Path]::GetFullPath($SignToolPath)
    }

    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $kitsRoots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"),
        (Join-Path $env:ProgramFiles "Windows Kits\10\bin")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and
        (Test-Path -LiteralPath $_ -PathType Container) }

    $candidates = foreach ($kitsRoot in $kitsRoots) {
        Get-ChildItem -LiteralPath $kitsRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $candidate = Join-Path $_.FullName "x64\signtool.exe"
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    [PSCustomObject]@{
                        Path = $candidate
                        Version = try { [Version]$_.Name } catch { [Version]"0.0" }
                    }
                }
            }
    }
    $selected = $candidates | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $selected) {
        Fail "SignTool.exe was not found. Install the Windows 10 or 11 SDK."
    }
    return $selected.Path
}

$resolvedPaths = @($Path | ForEach-Object {
    if (-not (Test-Path -LiteralPath $_ -PathType Leaf)) {
        Fail "file to sign is missing: $_"
    }
    [System.IO.Path]::GetFullPath($_)
} | Select-Object -Unique)

if ($resolvedPaths.Count -eq 0) {
    Fail "no files were supplied"
}

$signTool = Find-SignTool

if ($VerifyOnly) {
    foreach ($target in $resolvedPaths) {
        Write-Host "Verifying Authenticode signature: $target"
        & $signTool verify /pa /all /tw $target
        if ($LASTEXITCODE -ne 0) {
            Fail "signature verification failed for $target"
        }
    }
    return
}

if ([string]::IsNullOrWhiteSpace($TimestampUrl)) {
    $TimestampUrl = "http://timestamp.digicert.com"
}

$hasPath = -not [string]::IsNullOrWhiteSpace($CertificatePath)
$hasBase64 = -not [string]::IsNullOrWhiteSpace($CertificateBase64)
$hasThumbprint = -not [string]::IsNullOrWhiteSpace($CertificateThumbprint)
if ((@($hasPath, $hasBase64, $hasThumbprint) | Where-Object { $_ }).Count -ne 1) {
    Fail "configure exactly one signing identity: certificate path, base64 PFX, or certificate thumbprint"
}

$temporaryCertificate = $null
try {
    if ($hasBase64) {
        $temporaryCertificate = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("ApexSenseBridge-signing-{0}.pfx" -f [Guid]::NewGuid().ToString("N"))
        try {
            $certificateBytes = [Convert]::FromBase64String(($CertificateBase64 -replace '\s', ''))
            [System.IO.File]::WriteAllBytes($temporaryCertificate, $certificateBytes)
        } catch {
            Fail "ASB_SIGNING_CERTIFICATE_BASE64 is not a valid base64-encoded PFX"
        }
        $CertificatePath = $temporaryCertificate
        $hasPath = $true
    }

    $identityArguments = @()
    if ($hasPath) {
        if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
            Fail "certificate was not found at $CertificatePath"
        }
        $identityArguments += @('/f', [System.IO.Path]::GetFullPath($CertificatePath))
        if (-not [string]::IsNullOrEmpty($CertificatePassword)) {
            $identityArguments += @('/p', $CertificatePassword)
        }
    } else {
        $normalizedThumbprint = $CertificateThumbprint -replace '[^0-9A-Fa-f]', ''
        if ($normalizedThumbprint.Length -ne 40) {
            Fail "the certificate thumbprint must contain 40 hexadecimal characters"
        }
        $identityArguments += @('/sha1', $normalizedThumbprint)
        if ($MachineStore) {
            $identityArguments += '/sm'
        }
    }

    foreach ($target in $resolvedPaths) {
        Write-Host "Signing and timestamping: $target"
        $signed = $false
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            & $signTool sign /fd SHA256 /tr $TimestampUrl /td SHA256 /d "ApexSenseBridge" `
                @identityArguments $target
            if ($LASTEXITCODE -eq 0) {
                $signed = $true
                break
            }
            if ($attempt -lt 3) {
                Write-Warning "Signing attempt $attempt failed; retrying the timestamp request."
                Start-Sleep -Seconds ([Math]::Pow(2, $attempt))
            }
        }
        if (-not $signed) {
            Fail "signing failed for $target"
        }

        & $signTool verify /pa /all /tw $target
        if ($LASTEXITCODE -ne 0) {
            Fail "signature verification failed for $target"
        }
    }
} finally {
    if ($temporaryCertificate -and (Test-Path -LiteralPath $temporaryCertificate)) {
        Remove-Item -LiteralPath $temporaryCertificate -Force
    }
}
