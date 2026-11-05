<#
.SYNOPSIS
  Isolates Apex 5 rear-button problems: backend (engine) vs UI (Tray).

.DESCRIPTION
  1. Checks that the engine can see an APEX vendor interface ("list").
  2. Runs "test-rear --stream" directly, the same command RearButtonService uses.
  3. Parses REAR: lines and reports press/release counts per button.

  Close the Tray and any running ApexSenseBridge.exe first. Only one process
  can own the verified HID reader.

.EXAMPLE
  .\scripts\test-apex5-rear.ps1 -Seconds 20
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build-win\Release\ApexSenseBridge.exe"),
    [int]$Seconds = 15
)

$ErrorActionPreference = "Continue"
$Exe = [System.IO.Path]::GetFullPath($Exe)

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    Write-Host "FAIL: engine not found: $Exe" -ForegroundColor Red
    exit 10
}

$running = Get-Process -Name "ApexSenseBridge", "ApexSenseBridgeTray", "ApexSenseBridgeControl" -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "WARN: these processes may hold the HID device:" -ForegroundColor Yellow
    $running | ForEach-Object { Write-Host "  $($_.ProcessName) (PID $($_.Id))" }
    Write-Host "Close them and run again for a clean result." -ForegroundColor Yellow
}

# Step 1: device discovery
Write-Host ""
Write-Host "== Step 1: engine device list ==" -ForegroundColor Cyan
$listOutput = & $Exe list 2>&1 | ForEach-Object { "$_" }
$listOutput | ForEach-Object { Write-Host "  $_" }
$listExit = $LASTEXITCODE
if ($listExit -ne 0) {
    Write-Host "FAIL (backend): list exited $listExit. Controller not visible to engine." -ForegroundColor Red
    Write-Host "Check: controller awake (press PS), USB cable, HidHide whitelist." -ForegroundColor Yellow
    exit 2
}
Write-Host "PASS: device list ok" -ForegroundColor Green

# Step 2: rear-button stream (same command as RearButtonService)
Write-Host ""
Write-Host "== Step 2: test-rear --stream ($Seconds s) ==" -ForegroundColor Cyan
Write-Host "Press each rear button (M1, M2, M3, M4, LM, RM) once now." -ForegroundColor Yellow

# A List is a reference type, so adding inside the ForEach-Object scriptblock
# updates this collection. "$rearLines += ..." would create a scriptblock-local
# array and leave this one empty.
$rearLines = New-Object System.Collections.Generic.List[string]
# Stream each line as the engine prints it, so presses are visible while the
# engine is still running instead of after it exits.
& $Exe test-rear --stream --seconds $Seconds 2>&1 | ForEach-Object {
    $raw = "$_"
    $line = $raw
    if ($line.StartsWith("[bridge] ")) { $line = $line.Substring(9) }
    if ($line.StartsWith("REAR:")) {
        $rearLines.Add($line)
        Write-Host "  $line" -ForegroundColor Green
    } else {
        Write-Host "  (engine) $raw" -ForegroundColor DarkGray
    }
}
$rearExit = $LASTEXITCODE

# Step 3: parse edges (same logic as RearButtonService.ParseRearLine)
Write-Host ""
Write-Host "== Step 3: edge summary ==" -ForegroundColor Cyan
$state = @{ RM = 0; LM = 0; M1 = 0; M2 = 0; M3 = 0; M4 = 0 }
$presses = @{ RM = 0; LM = 0; M1 = 0; M2 = 0; M3 = 0; M4 = 0 }
$parseErrors = 0

foreach ($line in $rearLines) {
    $parts = $line.Substring(5).Trim() -split ' '
    $seen = @{}
    foreach ($part in $parts) {
        $kv = $part -split '='
        if ($kv.Count -ne 2) { $parseErrors++; continue }
        $key = $kv[0].Trim()
        if (-not $state.ContainsKey($key)) { $parseErrors++; continue }
        $value = if ($kv[1].Trim() -eq "1") { 1 } else { 0 }
        $seen[$key] = $value
    }
    foreach ($key in $seen.Keys) {
        if ($state[$key] -ne $seen[$key]) {
            if ($seen[$key] -eq 1) { $presses[$key]++ }
            $state[$key] = $seen[$key]
        }
    }
}

foreach ($key in @("M1", "M2", "M3", "M4", "LM", "RM")) {
    $status = if ($presses[$key] -gt 0) { "OK" } else { "--" }
    Write-Host ("  {0,-3} presses={1}  {2}" -f $key, $presses[$key], $status)
}

# Verdict
Write-Host ""
Write-Host "== Verdict ==" -ForegroundColor Cyan
if ($rearExit -ne 0) {
    Write-Host "FAIL (backend): test-rear exited $rearExit." -ForegroundColor Red
    exit 3
}
if ($rearLines.Count -eq 0) {
    Write-Host "FAIL (backend): no REAR: lines received. Did you press the buttons?" -ForegroundColor Red
    exit 4
}
if ($parseErrors -gt 0) {
    Write-Host "WARN: $parseErrors malformed REAR fields. Check the output format." -ForegroundColor Yellow
}
Write-Host "PASS (backend): engine streams rear-button state." -ForegroundColor Green
Write-Host "If the Tray still fails with this same engine, the problem is in the UI/Tray layer:" -ForegroundColor Yellow
Write-Host "  - Tray launches 'test-rear --stream' while another process holds the device." -ForegroundColor Yellow
Write-Host "  - RearButtonService.Start reads the session status and onError message." -ForegroundColor Yellow
exit 0