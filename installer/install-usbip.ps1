param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
$usbipKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1"
$udeServiceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude"
$filterServiceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_filter"

function Write-UsbipLog([string]$Message) {
    $directory = Split-Path -Parent $LogPath
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    Add-Content -LiteralPath $LogPath -Value "[$timestamp] $Message" -Encoding UTF8
}

function Test-UsbipServices {
    return (Test-Path -LiteralPath $udeServiceKey) -and
           (Test-Path -LiteralPath $filterServiceKey)
}

try {
    Write-UsbipLog "Starting guarded usbip-win2 prerequisite installation."

    if (-not (Test-Path -LiteralPath $InstallerPath)) {
        throw "The bundled USBip installer is missing: $InstallerPath"
    }

    $installed = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
    if ($null -ne $installed) {
        $installedVersion = [string]$installed.DisplayVersion
        if ($installedVersion.Trim() -eq "0.9.7.7" -and (Test-UsbipServices)) {
            Write-UsbipLog "USBip 0.9.7.7 and both driver services are already present; skipping."
            exit 0
        }
        throw "Refusing USBip's nested upgrade path because version '$installedVersion' is already registered."
    }

    if ((Test-Path -LiteralPath $udeServiceKey) -or
        (Test-Path -LiteralPath $filterServiceKey)) {
        throw "USBip driver services exist without a matching uninstall registration."
    }

    $upstreamLog = $LogPath + ".upstream.log"
    $arguments = @(
        "/VERYSILENT",
        "/COMPONENTS=main,client",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/SP-",
        "/LOG=`"$upstreamLog`""
    )
    $process = Start-Process -FilePath $InstallerPath -ArgumentList $arguments `
        -WindowStyle Hidden -PassThru

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-UsbipLog "USBip setup exceeded the $TimeoutSeconds second timeout; terminating its process tree."
        & (Join-Path $env:SystemRoot "System32\taskkill.exe") /PID $process.Id /T /F | Out-Null
        throw "USBip setup timed out after $TimeoutSeconds seconds."
    }

    Write-UsbipLog "USBip setup exited with code $($process.ExitCode)."
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "USBip setup failed with exit code $($process.ExitCode)."
    }

    $installed = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
    $installedVersion = if ($null -ne $installed) { [string]$installed.DisplayVersion } else { "" }
    if ($installedVersion.Trim() -ne "0.9.7.7" -or -not (Test-UsbipServices)) {
        throw "USBip setup returned success but its 0.9.7.7 registration or driver services are missing."
    }

    Write-UsbipLog "usbip-win2 0.9.7.7 prerequisite installation completed successfully."
    exit 0
}
catch {
    try { Write-UsbipLog ("ERROR: " + $_.Exception.Message) } catch {}
    Write-Error $_.Exception.Message
    exit 1
}
