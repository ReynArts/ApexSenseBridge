[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string[]]$InstanceId,
    [string]$UsbipPath = (Join-Path $env:ProgramFiles 'USBip\usbip.exe'),
    [string]$PnpUtilPath = (Join-Path $env:SystemRoot 'System32\pnputil.exe')
)

$ErrorActionPreference = 'Stop'
# Explicit instance IDs are required. Never remove every Sony controller or
# detach every USBip port; this helper is for identified legacy ASB leftovers.
if (-not (Test-Path -LiteralPath $UsbipPath)) { throw 'USBip executable not found.' }
if (Get-Process ApexSenseBridge,ApexSenseBridgeTray,viiper -ErrorAction SilentlyContinue) {
    throw 'Quit ApexSenseBridge from its tray menu before removing legacy devices.'
}

function Read-Imports {
    $lines = @(& $UsbipPath port 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate USBip imports.' }
    Write-Host ("USBip port output:`n" + ($lines -join "`n"))
    if (-not ($lines -match '^Imported USB devices\s*$')) { throw 'Unrecognized USBip enumeration; nothing removed.' }
    $entry = $null
    foreach ($line in $lines) {
        if ("$line" -match '^Port\s+(\d+):') {
            if ($entry) { $entry }
            $entry = [pscustomobject]@{ Port=[int]$Matches[1]; Sony=$false; Loopback=$false; ServerPort=0 }
        }
        if (-not $entry) { continue }
        if ("$line" -match '\(054c:0ce6\)') { $entry.Sony=$true }
        if ("$line" -match 'usbip://(127\.0\.0\.1|localhost):([0-9]+)/([0-9]+-[0-9]+)') {
            $entry.Loopback=$true
            $entry.ServerPort=[int]$Matches[2]
        }
    }
    if ($entry) { $entry }
}

foreach ($id in $InstanceId) {
    if ($id -notmatch '^USB\\VID_054C&PID_0CE6\\[^\\]+$') { throw "Not a DualSense USB root: $id" }
    $root = Get-PnpDevice -PresentOnly -InstanceId $id -ErrorAction SilentlyContinue
    if (-not $root) { Write-Host "Already detached: $id"; continue }
    $stack = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Stack).Data
    if (@($stack) -notcontains '\Driver\usbip2_filter') { throw "Refusing non-USBip controller: $id" }
    $port = [int](Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Address).Data
    if ($port -le 0) { throw 'Refusing an invalid or wildcard port.' }
    $imports = @(Read-Imports | Where-Object Port -eq $port)
    if ($imports.Count -gt 1) { throw "Cannot uniquely match USBip port $port to the identified leftover." }
    if ($imports.Count -eq 1) {
        if (-not $imports[0].Sony -or -not $imports[0].Loopback -or $imports[0].ServerPort -le 0) {
            throw "Port $port belongs to an unrecognized import; nothing removed."
        }
        $serverPort = $imports[0].ServerPort
        # A listening server may belong to a live emulator. Leave it alone.
        $listeners = @(Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object LocalPort -eq $serverPort)
        if ($listeners.Count) { throw "Port $port still has a live server on TCP $serverPort; nothing removed." }
        $action = "Detach orphaned USBip port $port (dead localhost:$serverPort)"
    } else {
        # The Windows devnode can outlive its USBip import. There is no port to
        # detach in that case. Remove only the explicit, verified USBip root.
        if (-not (Test-Path -LiteralPath $PnpUtilPath)) { throw 'PnPUtil executable not found.' }
        $action = 'Remove identified leftover Windows device with no USBip import'
    }
    if ($PSCmdlet.ShouldProcess($id, $action)) {
        if ($imports.Count -eq 1) {
            & $UsbipPath detach -p "$port"
            if ($LASTEXITCODE -ne 0) { throw "USBip refused to detach port $port." }
        } else {
            & $PnpUtilPath /remove-device "$id"
            if ($LASTEXITCODE -ne 0) { throw "PnPUtil refused to remove $id (exit $LASTEXITCODE)." }
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 200
            $remaining = Get-PnpDevice -PresentOnly -InstanceId $id -ErrorAction SilentlyContinue
        } while ($remaining -and [DateTime]::UtcNow -lt $deadline)
        if ($remaining) { throw "Port detached but Windows still reports $id present; collect diagnostics." }
        Write-Host "Removed legacy virtual controller: $id"
    }
}
