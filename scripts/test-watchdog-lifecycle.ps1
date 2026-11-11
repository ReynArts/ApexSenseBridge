param(
    [string]$Engine = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-win\Release\ApexSenseBridge.exe")
)

$ErrorActionPreference = "Stop"
$Engine = (Resolve-Path -LiteralPath $Engine).Path
for ($iteration = 0; $iteration -lt 5; $iteration++) {
    $token = [Guid]::NewGuid().ToString("N")
    $stop = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset,
        "Local\ApexSenseBridge.Session.$token.Stop")
    $owner = $null
    $watchdog = $null
    try {
        # An exited owner with a retained process handle reproduces a detached
        # engine. Keep the stop event unsignalled, as after a disconnection.
        $start = New-Object Diagnostics.ProcessStartInfo($Engine, "help")
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $owner = [Diagnostics.Process]::Start($start)
        if (-not $owner.WaitForExit(5000)) { throw "Test owner did not exit." }
        $start.Arguments = "hidhide-watchdog $($owner.Id) $token"
        $watchdog = [Diagnostics.Process]::Start($start)
        if (-not $watchdog.WaitForExit(3000)) { throw "Retired watchdog remained alive without a stop request." }
        if ($watchdog.ExitCode -ne 0) { throw "Retired watchdog failed with exit code $($watchdog.ExitCode)." }
        if ($stop.WaitOne(0)) { throw "Test stop event was unexpectedly signalled." }
        # No recovery marker belongs to the test owner. A different real
        # session's marker is read only and must never be restored here.
    } finally {
        foreach ($child in @($watchdog, $owner)) {
            if ($null -ne $child) {
                if (-not $child.HasExited) { $child.Kill(); [void]$child.WaitForExit(5000) }
                $child.Dispose()
            }
        }
        $stop.Dispose()
    }
}
Write-Output "PASS: five retired recovery watchdogs exited with unsignalled stop events."
