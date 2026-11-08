[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string[]]$InstanceId,
    [string]$UsbipPath = (Join-Path $env:ProgramFiles 'USBip\usbip.exe')
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
    $port = 0
    $sony = $false
    foreach ($line in $lines) {
        if ("$line" -match '^Port\s+(\d+):') { $port = [int]$Matches[1]; $sony = $false }
        if ("$line" -match '\(054c:0ce6\)') { $sony = $true }
        if ($sony -and "$line" -match 'usbip://(127\.0\.0\.1|localhost):([0-9]+)/([0-9]+-[0-9]+)') {
            [pscustomobject]@{ Port=$port; ServerPort=[int]$Matches[2]; Bus=$Matches[3] }
        }
    }
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
    if ($imports.Count -ne 1) { throw "Cannot uniquely match USBip port $port to the identified leftover." }
    $serverPort = $imports[0].ServerPort
    # A listening server may belong to a live emulator. Leave it alone.
    $listeners = @(Get-NetTCPConnection -State Listen -ErrorAction Stop | Where-Object LocalPort -eq $serverPort)
    if ($listeners.Count) { throw "Port $port still has a live server on TCP $serverPort; nothing removed." }
    if ($PSCmdlet.ShouldProcess($id, "Detach orphaned USBip port $port (dead localhost:$serverPort)")) {
        & $UsbipPath detach -p "$port"
        if ($LASTEXITCODE -ne 0) { throw "USBip refused to detach port $port." }
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 200
            $remaining = Get-PnpDevice -PresentOnly -InstanceId $id -ErrorAction SilentlyContinue
        } while ($remaining -and [DateTime]::UtcNow -lt $deadline)
        if ($remaining) { throw "Port detached but Windows still reports $id present; collect diagnostics." }
        Write-Host "Removed legacy virtual controller: $id"
    }
}
