param(
    [switch]$ElevatedRelaunch
)

$ErrorActionPreference = "Stop"
$usbipVersion = "0.9.8.0"
$usbipInstallerSha256 = "81F426741F7EE2ED991FEBE24A22DACA8400B6AE2F171054E3FB404897E15D39"
$hidHideVersion = "1.5.230"
$hidHideInstallerSha256 = "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6"
$usbipKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{199505b0-b93d-4521-a8c7-897818e0205a}_is1"
$usbipUdeService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_ude"
$usbipFilterService = "HKLM:\SYSTEM\CurrentControlSet\Services\usbip2_filter"
$hidHideKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF}"
$hidHideService = "HKLM:\SYSTEM\CurrentControlSet\Services\HidHide"
$usbipInstallerName = "USBip-0.9.8.0-x64.exe"
$hidHideInstallerName = "HidHide_1.5.230_x64.exe"
$stagePrefix = "ApexSenseBridge-Drivers-"
$installerTimeoutSeconds = 600
$elevatedTimeoutSeconds = 1500
$logPath = Join-Path $PSScriptRoot "driver-install.log"
$resultPath = Join-Path $PSScriptRoot "driver-install.result"

function Write-DriverLog([string]$Message) {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
    $line = "[$timestamp] $Message"
    try { Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8 -ErrorAction Stop }
    catch { }
    Write-Host $Message
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-NativeErrorCode([System.Exception]$Exception) {
    $current = $Exception
    while ($null -ne $current) {
        if ($current -is [System.ComponentModel.Win32Exception]) {
            return [int]$current.NativeErrorCode
        }
        $current = $current.InnerException
    }
    return 0
}

function Get-InnermostMessage([System.Exception]$Exception) {
    $current = $Exception
    while ($null -ne $current.InnerException) {
        $current = $current.InnerException
    }
    return $current.Message
}

function Get-LaunchFailureHint([string]$Name, [int]$Code) {
    switch ($Code) {
        { $_ -in @(1223, 5, 225, 1260, 1261) } {
            return "Windows or a security product refused to start the $Name installer (error $Code), so this step changed nothing. Check Windows Security > Protection history and your antivirus quarantine for the installer, allow it, then run Install-Drivers.cmd again: components that are already installed are skipped. To install $Name by hand, right-click its file in the Drivers folder of the package, choose Run as administrator, then run Install-Drivers.cmd once more to verify it."
        }
        740 {
            return "The $Name installer needs administrator rights. Run Install-Drivers.cmd again and accept the administrator prompt."
        }
        default { return "" }
    }
}

function Get-InstallerExitHint([string]$Name, [int]$Code) {
    switch ($Code) {
        1618 { return "Another Windows Installer installation is already running. Wait for it to finish (or restart Windows), then run this helper again." }
        1602 { return "The $Name installation was cancelled." }
        1603 { return "The $Name installer reported a fatal error. Restart Windows and run this helper again; already installed components are skipped." }
        default { return "" }
    }
}

function Get-EnvironmentSummary {
    $driveType = "unknown"
    try {
        $root = [System.IO.Path]::GetPathRoot($PSScriptRoot)
        if ($root.StartsWith("\\")) { $driveType = "Network" }
        else { $driveType = [string](New-Object System.IO.DriveInfo($root)).DriveType }
    }
    catch { }
    return ("Windows {0}; PowerShell {1}; 64-bit process {2}; user {3}; administrator {4}; script folder {5}; drive type {6}" -f
        [Environment]::OSVersion.Version, $PSVersionTable.PSVersion, [Environment]::Is64BitProcess,
        [Environment]::UserName, (Test-IsAdministrator), $PSScriptRoot, $driveType)
}

function Get-PrerequisiteSnapshot {
    $usbip = Get-ItemProperty -LiteralPath $usbipKey -ErrorAction SilentlyContinue
    $hidHide = Get-ItemProperty -LiteralPath $hidHideKey -ErrorAction SilentlyContinue
    return [pscustomobject]@{
        UsbipRegistered = ($null -ne $usbip)
        UsbipVersion = if ($null -ne $usbip) { ([string]$usbip.DisplayVersion).Trim() } else { "" }
        UsbipUde = (Test-Path -LiteralPath $usbipUdeService)
        UsbipFilter = (Test-Path -LiteralPath $usbipFilterService)
        HidHideRegistered = ($null -ne $hidHide)
        HidHideVersion = if ($null -ne $hidHide) { ([string]$hidHide.DisplayVersion).Trim() } else { "" }
        HidHideService = (Test-Path -LiteralPath $hidHideService)
    }
}

function Resolve-PrerequisitePlan($Snapshot) {
    $plan = [pscustomobject]@{
        UsbipAction = "Install"
        UsbipMessage = ""
        HidHideAction = "Install"
        HidHideMessage = ""
    }

    if ($Snapshot.UsbipRegistered) {
        $shown = if ($Snapshot.UsbipVersion) { $Snapshot.UsbipVersion } else { "(unknown version)" }
        if ($Snapshot.UsbipVersion -ne $usbipVersion -or
            -not ($Snapshot.UsbipUde -and $Snapshot.UsbipFilter)) {
            $plan.UsbipAction = "Blocked"
            $plan.UsbipMessage = "USBip $shown is already registered or incomplete. Uninstall USBip in Windows Settings (USB devices pause briefly while Windows removes it), restart, and run this helper again. This avoids the upstream uninstaller hang. Nothing was installed."
        }
        else {
            $plan.UsbipAction = "Ready"
        }
    }
    elseif ($Snapshot.UsbipUde -or $Snapshot.UsbipFilter) {
        $plan.UsbipAction = "Blocked"
        $plan.UsbipMessage = "Orphaned USBip driver services were found. Repair or remove USBip in Windows Settings, restart, and run this helper again. Nothing was installed."
    }

    if ($Snapshot.HidHideRegistered) {
        $shown = if ($Snapshot.HidHideVersion) { $Snapshot.HidHideVersion } else { "(unknown version)" }
        if ($Snapshot.HidHideVersion -ne $hidHideVersion -or -not $Snapshot.HidHideService) {
            $plan.HidHideAction = "Blocked"
            $plan.HidHideMessage = "HidHide $shown is already registered or incomplete. Repair or uninstall HidHide in Windows Settings, restart, and run this helper again. Nothing was installed."
        }
        else {
            $plan.HidHideAction = "Ready"
        }
    }
    elseif ($Snapshot.HidHideService) {
        $plan.HidHideAction = "Blocked"
        $plan.HidHideMessage = "A HidHide driver service exists without a matching uninstall registration. Repair or remove HidHide, restart, and run this helper again. Nothing was installed."
    }

    return $plan
}

function Assert-PlanNotBlocked($Plan) {
    if ($Plan.UsbipAction -eq "Blocked") { throw $Plan.UsbipMessage }
    if ($Plan.HidHideAction -eq "Blocked") { throw $Plan.HidHideMessage }
}

function Assert-InstallerHash([string]$Path, [string]$ExpectedSha256, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label installer is missing: $Path. Extract the whole ApexSenseBridge-Portable.zip and make sure antivirus did not quarantine files from the Drivers folder."
    }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $ExpectedSha256) {
        throw "$Label installer failed its SHA-256 integrity check."
    }
}

function Remove-StageDirectory([string]$StageRoot) {
    if ([string]::IsNullOrWhiteSpace($StageRoot)) { return }
    try {
        $full = [System.IO.Path]::GetFullPath($StageRoot).TrimEnd('\')
        $tempFull = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
        $leaf = Split-Path -Leaf $full
        if (-not $full.StartsWith($tempFull + '\', [StringComparison]::OrdinalIgnoreCase)) { return }
        if (-not $leaf.StartsWith($stagePrefix, [StringComparison]::Ordinal)) { return }
        if (Test-Path -LiteralPath $full) {
            Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
        }
    }
    catch {
        Write-DriverLog "The temporary folder $StageRoot could not be removed: $($_.Exception.Message)"
    }
}

function Remove-StaleStages([string]$TempRoot) {
    $limit = (Get-Date).AddDays(-1)
    foreach ($directory in @(Get-ChildItem -LiteralPath $TempRoot -Directory -Filter ($stagePrefix + "*") -ErrorAction SilentlyContinue)) {
        if ($directory.LastWriteTime -lt $limit) { Remove-StageDirectory $directory.FullName }
    }
}
function New-DriverStage(
    [string]$ScriptPath,
    [string]$DriversSource,
    [object[]]$Installers,
    [string]$TempRoot
) {
    $stageRoot = Join-Path $TempRoot ($stagePrefix + [Guid]::NewGuid().ToString("N"))
    $stageDrivers = Join-Path $stageRoot "Drivers"
    New-Item -ItemType Directory -Path $stageDrivers -Force | Out-Null
    try {
        $stagedScript = Join-Path $stageRoot "Install-Drivers.ps1"
        Copy-Item -LiteralPath $ScriptPath -Destination $stagedScript -Force
        try { Unblock-File -LiteralPath $stagedScript } catch { }
        foreach ($installer in $Installers) {
            $source = Join-Path $DriversSource $installer.Name
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "$($installer.Label) installer is missing: $source. Extract the whole ApexSenseBridge-Portable.zip and make sure antivirus did not quarantine files from the Drivers folder."
            }
            $staged = Join-Path $stageDrivers $installer.Name
            Copy-Item -LiteralPath $source -Destination $staged -Force
            Assert-InstallerHash $staged $installer.Sha256 $installer.Label
            try { Unblock-File -LiteralPath $staged } catch { }
        }
    }
    catch {
        Remove-StageDirectory $stageRoot
        throw
    }
    return $stageRoot
}

function Copy-StagedLogs([string]$StageRoot, [string]$Destination) {
    foreach ($name in @("driver-install.log", "usbip-upstream.log")) {
        $source = Join-Path $StageRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $target = Join-Path $Destination $name
        if ($name -eq "driver-install.log" -and (Test-Path -LiteralPath $target -PathType Leaf)) {
            $text = Get-Content -LiteralPath $source -Raw -Encoding UTF8
            Add-Content -LiteralPath $target -Value $text -Encoding UTF8 -ErrorAction Stop
        }
        else {
            Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop
        }
    }
}

function Invoke-BoundedInstaller(
    [string]$Path,
    [string[]]$Arguments,
    [string]$Name,
    [int]$TimeoutSeconds = 600
) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name installer is missing: $Path"
    }

    $processName = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    if (@(Get-Process -Name $processName -ErrorAction SilentlyContinue).Count -gt 0) {
        throw "A $Name installer is already running. Wait for it to finish, then run this helper again."
    }

    Write-DriverLog "Installing $Name..."
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Path
    $startInfo.Arguments = ($Arguments -join " ")
    $startInfo.WorkingDirectory = Split-Path -Parent $Path
    $startInfo.UseShellExecute = $false
    try {
        $process = [System.Diagnostics.Process]::Start($startInfo)
    }
    catch {
        $nativeCode = Get-NativeErrorCode $_.Exception
        $detail = Get-InnermostMessage $_.Exception
        Write-DriverLog "The $Name installer could not be started (Windows error ${nativeCode}): $detail"
        throw ("$Name could not be started (Windows error ${nativeCode}: $detail). " +
               (Get-LaunchFailureHint $Name $nativeCode)).Trim()
    }

    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-DriverLog "$Name is still running after $TimeoutSeconds seconds; it was left running on purpose."
        throw "$Name is still installing after $TimeoutSeconds seconds. It was not terminated, because interrupting a driver installer can leave Windows half-configured. Wait for it to finish, then run this helper again: it detects what is already installed."
    }
    Write-DriverLog "$Name installer exited with code $($process.ExitCode)."
    if ($process.ExitCode -notin @(0, 1641, 3010)) {
        throw ("$Name installer failed with exit code $($process.ExitCode). " +
               (Get-InstallerExitHint $Name $process.ExitCode)).Trim()
    }
}

function Invoke-DriverInstallation([string]$DriversRoot) {
    Write-DriverLog "Starting ApexSenseBridge portable driver setup."
    Write-DriverLog (Get-EnvironmentSummary)

    $plan = Resolve-PrerequisitePlan (Get-PrerequisiteSnapshot)
    Assert-PlanNotBlocked $plan

    $usbipInstaller = Join-Path $DriversRoot $usbipInstallerName
    $hidHideInstaller = Join-Path $DriversRoot $hidHideInstallerName
    if ($plan.UsbipAction -eq "Install") {
        Assert-InstallerHash $usbipInstaller $usbipInstallerSha256 "usbip-win2 $usbipVersion"
    }
    if ($plan.HidHideAction -eq "Install") {
        Assert-InstallerHash $hidHideInstaller $hidHideInstallerSha256 "HidHide $hidHideVersion"
    }

    if ($plan.UsbipAction -eq "Ready") {
        Write-DriverLog "Compatible usbip-win2 $usbipVersion is already ready; preserving it."
    }
    else {
        $usbipLog = Join-Path $PSScriptRoot "usbip-upstream.log"
        Invoke-BoundedInstaller $usbipInstaller @(
            "/VERYSILENT",
            "/COMPONENTS=main,client",
            "/SUPPRESSMSGBOXES",
            "/NORESTART",
            "/SP-",
            "/LOG=`"$usbipLog`""
        ) "usbip-win2 $usbipVersion" $installerTimeoutSeconds

        $after = Resolve-PrerequisitePlan (Get-PrerequisiteSnapshot)
        if ($after.UsbipAction -ne "Ready") {
            throw "USBip setup returned without creating the expected $usbipVersion driver registration."
        }
        Write-DriverLog "usbip-win2 $usbipVersion is installed and verified."
    }

    if ($plan.HidHideAction -eq "Ready") {
        Write-DriverLog "Compatible HidHide $hidHideVersion is already ready; preserving it."
    }
    else {
        try {
            Invoke-BoundedInstaller $hidHideInstaller @("/quiet", "/norestart") `
                "HidHide $hidHideVersion" $installerTimeoutSeconds

            $after = Resolve-PrerequisitePlan (Get-PrerequisiteSnapshot)
            if ($after.HidHideAction -ne "Ready") {
                throw "HidHide setup returned without creating the expected $hidHideVersion registration and driver service."
            }
        }
        catch {
            throw ($_.Exception.Message +
                   " usbip-win2 $usbipVersion is installed and will be kept: run this helper again and it will retry HidHide only.")
        }
        Write-DriverLog "HidHide $hidHideVersion is installed and verified."
    }

    Write-DriverLog "All portable driver prerequisites are ready. A Windows restart is recommended."
}

function Write-UsbHubNotice {
    Write-DriverLog "Notice: usbip-win2 restarts every USB 3.0 hub while it installs. All connected USB devices (keyboard, mouse, controller, drives) stop working for a short time, then resume. Close file transfers, calls and recordings first, and do not unplug devices or power off during this step. The usbip-win2 project also recommends creating a Windows restore point before installing."
}

function Invoke-ElevatedStage([string]$StageRoot) {
    $powerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
    $stagedScript = Join-Path $StageRoot "Install-Drivers.ps1"
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $powerShell
    $startInfo.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $stagedScript + '" -ElevatedRelaunch'
    $startInfo.WorkingDirectory = $StageRoot
    $startInfo.UseShellExecute = $true
    $startInfo.Verb = "runas"

    Write-DriverLog "Administrator access is required to install the kernel drivers. Accept the Windows prompt."
    try {
        $elevated = [System.Diagnostics.Process]::Start($startInfo)
    }
    catch {
        $nativeCode = Get-NativeErrorCode $_.Exception
        if ($nativeCode -eq 1223) {
            throw "The Windows administrator prompt was declined or blocked (error 1223). Nothing was installed. Run Install-Drivers.cmd again and choose Yes."
        }
        throw "Administrator elevation failed (Windows error ${nativeCode}: $(Get-InnermostMessage $_.Exception)). Nothing was installed."
    }
    if ($null -eq $elevated) {
        throw "Administrator elevation did not start a process. Nothing was installed."
    }

    if (-not $elevated.WaitForExit($elevatedTimeoutSeconds * 1000)) {
        Write-DriverLog "The elevated driver setup is still running after $elevatedTimeoutSeconds seconds and was left running. Do not restart Windows until it finishes; then run this helper again to verify the result. Its log stays in $StageRoot."
        return [pscustomobject]@{ ExitCode = 1; KeepStage = $true }
    }

    $exitCode = $null
    try { $exitCode = [int]$elevated.ExitCode } catch { }
    if ($null -eq $exitCode) {
        $recorded = Join-Path $StageRoot "driver-install.result"
        if (Test-Path -LiteralPath $recorded -PathType Leaf) {
            $parsed = 0
            if ([int]::TryParse((Get-Content -LiteralPath $recorded -Raw).Trim(), [ref]$parsed)) {
                $exitCode = $parsed
            }
        }
    }
    if ($null -eq $exitCode) { $exitCode = 1 }

    $stagedLog = Join-Path $StageRoot "driver-install.log"
    if (Test-Path -LiteralPath $stagedLog -PathType Leaf) {
        Get-Content -LiteralPath $stagedLog -Encoding UTF8 | ForEach-Object { Write-Host $_ }
    }

    $keepStage = $false
    try { Copy-StagedLogs $StageRoot $PSScriptRoot }
    catch {
        $keepStage = $true
        Write-Host "The setup log could not be copied next to this script ($($_.Exception.Message)). It is kept in $StageRoot"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; KeepStage = $keepStage }
}

function Start-DriverSetup {
    Write-DriverLog "Preparing ApexSenseBridge portable driver setup."
    Write-DriverLog (Get-EnvironmentSummary)

    $plan = Resolve-PrerequisitePlan (Get-PrerequisiteSnapshot)
    Assert-PlanNotBlocked $plan

    $needed = @()
    if ($plan.UsbipAction -eq "Install") {
        $needed += [pscustomobject]@{
            Name = $usbipInstallerName
            Sha256 = $usbipInstallerSha256
            Label = "usbip-win2 $usbipVersion"
        }
    }
    if ($plan.HidHideAction -eq "Install") {
        $needed += [pscustomobject]@{
            Name = $hidHideInstallerName
            Sha256 = $hidHideInstallerSha256
            Label = "HidHide $hidHideVersion"
        }
    }
    if ($needed.Count -eq 0) {
        Write-DriverLog "usbip-win2 $usbipVersion and HidHide $hidHideVersion are already installed and ready. Nothing to do."
        return 0
    }
    if ($plan.UsbipAction -eq "Ready") {
        Write-DriverLog "usbip-win2 $usbipVersion is already installed and will be kept; only HidHide $hidHideVersion is missing."
    }
    else {
        Write-UsbHubNotice
    }

    Remove-StaleStages ([System.IO.Path]::GetTempPath())
    $stageRoot = New-DriverStage $PSCommandPath (Join-Path $PSScriptRoot "Drivers") $needed `
        ([System.IO.Path]::GetTempPath())
    Write-DriverLog "Prerequisite installers were copied to $stageRoot and verified against their pinned SHA-256."
    $keepStage = $false
    try {
        if (Test-IsAdministrator) {
            Invoke-DriverInstallation (Join-Path $stageRoot "Drivers")
            return 0
        }
        $outcome = Invoke-ElevatedStage $stageRoot
        $keepStage = $outcome.KeepStage
        return [int]$outcome.ExitCode
    }
    finally {
        if (-not $keepStage) { Remove-StageDirectory $stageRoot }
    }
}

try {
    if ($ElevatedRelaunch) {
        if (-not (Test-IsAdministrator)) {
            throw "The elevated session does not have administrator rights. Nothing was installed."
        }
        Invoke-DriverInstallation (Join-Path $PSScriptRoot "Drivers")
        Set-Content -LiteralPath $resultPath -Value "0" -Encoding ASCII
        exit 0
    }
    $setupResult = @(Start-DriverSetup)
    exit ([int]$setupResult[-1])
}
catch {
    try { Write-DriverLog ("ERROR: " + $_.Exception.Message) } catch {}
    if ($ElevatedRelaunch) {
        try { Set-Content -LiteralPath $resultPath -Value "1" -Encoding ASCII } catch {}
    }
    exit 1
}
