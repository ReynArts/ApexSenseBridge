$ErrorActionPreference = "Stop"
$usbipVersion = "0.9.7.7"
$usbipKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1"
$usbipUdeService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude"
$usbipFilterService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_filter"
$hidHideKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF}"
$hidHideService = "HKLM:\SYSTEM\CurrentControlSet\Services\HidHide"
$logPath = Join-Path $PSScriptRoot "driver-install.log"

function Write-DriverLog([string]$Message) {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    $line = "[$timestamp] $Message"
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
    Write-Host $Message
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-BoundedInstaller(
    [string]$Path,
    [string[]]$Arguments,
    [string]$Name,
    [int]$TimeoutSeconds = 300
) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name installer is missing: $Path"
    }

    Write-DriverLog "Installing $Name..."
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-DriverLog "$Name exceeded the $TimeoutSeconds second timeout; terminating its process tree."
        & (Join-Path $env:SystemRoot "System32\taskkill.exe") /PID $process.Id /T /F | Out-Null
        throw "$Name installation timed out."
    }
    Write-DriverLog "$Name installer exited with code $($process.ExitCode)."
    if ($process.ExitCode -notin @(0, 1641, 3010)) {
        throw "$Name installer failed with exit code $($process.ExitCode)."
    }
}

if (-not (Test-IsAdministrator)) {
    try {
        Write-Host "Administrator access is required to install the kernel drivers."
        $powerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
        $arguments = @(
            "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-File", "`"$PSCommandPath`""
        )
        $elevated = Start-Process -FilePath $powerShell -ArgumentList $arguments `
            -Verb RunAs -Wait -PassThru
        exit $elevated.ExitCode
    }
    catch {
        Write-Error ("Elevation was cancelled or failed: " + $_.Exception.Message)
        exit 1
    }
}

try {
    Write-DriverLog "Starting ApexSenseBridge portable driver setup."

    $usbip = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
    $usbipServicesReady = (Test-Path -LiteralPath $usbipUdeService) -and
                          (Test-Path -LiteralPath $usbipFilterService)
    if ($null -ne $usbip) {
        $installedVersion = ([string]$usbip.DisplayVersion).Trim()
        $supportedVersions = @("0.9.7.5", "0.9.7.6", "0.9.7.7")
        if ($installedVersion -notin $supportedVersions -or -not $usbipServicesReady) {
            throw "USBip $installedVersion is already registered or incomplete. Uninstall USBip in Windows Settings, restart, and run this helper again. This avoids the upstream uninstaller hang."
        }
        Write-DriverLog "Compatible usbip-win2 $installedVersion is already ready; preserving it."
    }
    elseif ((Test-Path -LiteralPath $usbipUdeService) -or
            (Test-Path -LiteralPath $usbipFilterService)) {
        throw "Orphaned USBip driver services were found. Repair or remove USBip, restart, and run this helper again."
    }
    else {
        $usbipInstaller = Join-Path $PSScriptRoot "Drivers\USBip-0.9.7.7-x64.exe"
        $usbipLog = Join-Path $PSScriptRoot "usbip-upstream.log"
        Invoke-BoundedInstaller $usbipInstaller @(
            "/VERYSILENT",
            "/COMPONENTS=main,client",
            "/SUPPRESSMSGBOXES",
            "/NORESTART",
            "/SP-",
            "/LOG=`"$usbipLog`""
        ) "usbip-win2 $usbipVersion"

        $usbip = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
        $installedVersion = if ($null -ne $usbip) { ([string]$usbip.DisplayVersion).Trim() } else { "" }
        if ($installedVersion -ne $usbipVersion -or
            -not (Test-Path -LiteralPath $usbipUdeService) -or
            -not (Test-Path -LiteralPath $usbipFilterService)) {
            throw "USBip setup returned without creating the expected $usbipVersion driver registration."
        }
    }

    if ((Test-Path -LiteralPath $hidHideKey) -and
        (Test-Path -LiteralPath $hidHideService)) {
        Write-DriverLog "HidHide 1.5.230 is already ready; skipping."
    }
    elseif (Test-Path -LiteralPath $hidHideService) {
        Write-DriverLog "An existing HidHide driver is present; preserving it."
    }
    else {
        $hidHideInstaller = Join-Path $PSScriptRoot "Drivers\HidHide_1.5.230_x64.exe"
        Invoke-BoundedInstaller $hidHideInstaller @("/quiet", "/norestart") "HidHide 1.5.230"
        if (-not (Test-Path -LiteralPath $hidHideService)) {
            throw "HidHide setup returned without creating its driver service."
        }
    }

    Write-DriverLog "All portable driver prerequisites are ready. A Windows restart is recommended."
    exit 0
}
catch {
    try { Write-DriverLog ("ERROR: " + $_.Exception.Message) } catch {}
    Write-Error $_.Exception.Message
    exit 1
}
