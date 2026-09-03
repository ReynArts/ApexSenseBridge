<#
.SYNOPSIS
    Runs the first hardware validation pass for the Apex 4 port.
.DESCRIPTION
    Identification and input collection are read-only. Gentle rumble and
    FORCEADAPT tests are only run after explicit confirmation by the tester.
    A small ZIP containing all command output is written to the Desktop.
#>

[CmdletBinding()]
param(
    [string]$BridgeExecutable = "",
    [string]$OutputDirectory = [Environment]::GetFolderPath("Desktop"),
    [switch]$NonInteractive
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$script:TestVersion = "1.3.0"

if ([string]::IsNullOrWhiteSpace($BridgeExecutable)) {
    $BridgeExecutable = Join-Path $PSScriptRoot "ApexSenseBridge.exe"
}

function Write-Utf8File([string]$Path, [string[]]$Lines) {
    $encoding = New-Object System.Text.UTF8Encoding($true)
    [IO.File]::WriteAllLines($Path, $Lines, $encoding)
}

function Invoke-BridgeCommand(
    [string[]]$Arguments,
    [string]$LogName,
    [string]$WorkingDirectory) {
    Write-Host ""
    Write-Host ("> ApexSenseBridge.exe " + ($Arguments -join " ")) -ForegroundColor Cyan
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell can turn native stderr into a terminating
        # NativeCommandError when the script-wide preference is Stop. Capture
        # it as normal command output and decide from the native exit code.
        $ErrorActionPreference = "Continue"
        $lines = @(& $script:BridgePath @Arguments 2>&1 |
            ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    foreach ($line in $lines) {
        Write-Host $line
    }
    Write-Utf8File (Join-Path $WorkingDirectory $LogName) `
        (@("command=ApexSenseBridge.exe " + ($Arguments -join " "),
           "exit_code=$exitCode", "") + $lines)
    return $exitCode
}

function Confirm-Test([string]$Prompt) {
    if ($NonInteractive.IsPresent) {
        return $false
    }
    $answer = Read-Host ($Prompt + " [o/N]")
    return $answer -match "^(?i:o|oui|y|yes)$"
}

$script:BridgePath = (Resolve-Path -LiteralPath $BridgeExecutable -ErrorAction Stop).Path
if ([IO.Path]::GetFileName($script:BridgePath) -ine "ApexSenseBridge.exe") {
    throw "BridgeExecutable doit pointer vers ApexSenseBridge.exe."
}
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$workingDirectory = Join-Path $env:TEMP ("Apex4-Port-Test-" + $stamp + "-" + [guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $workingDirectory)
$resultLines = New-Object System.Collections.Generic.List[string]
$resultLines.Add("test_version=$script:TestVersion")
$resultLines.Add("started_at=$([DateTimeOffset]::Now.ToString('o'))")
$resultLines.Add("bridge=$script:BridgePath")

try {
    Clear-Host
    Write-Host "ApexSenseBridge - validation Apex 4" -ForegroundColor Green
    Write-Host ""
    Write-Host "Preparation :"
    Write-Host "  1. Brancher uniquement l'Apex 4 par cable USB ou dongle 2,4 GHz."
    Write-Host "  2. Activer le mode DInput (FN + A environ 3 s, ou menu de l'ecran)."
    Write-Host "  3. Fermer Flydigi Space Station."
    Write-Host "  4. Aucun droit administrateur n'est necessaire pour ces tests."
    if (-not $NonInteractive.IsPresent) {
        [void](Read-Host "Appuyer sur Entree quand la manette est prete")
    }

    $listCode = Invoke-BridgeCommand @("list") "01-list.txt" $workingDirectory
    $resultLines.Add("list_exit_code=$listCode")
    if ($listCode -ne 0) {
        throw "L'interface Apex 4 n'a pas ete detectee."
    }

    $runRumble = Confirm-Test `
        "Apres les entrees, lancer une vibration douce des poignees pendant environ 1 seconde ?"
    $runForceAdapt = Confirm-Test `
        "Puis lancer une resistance DOUCE sur RT pendant environ 1,5 seconde ?"
    $resultLines.Add("rumble_requested=$($runRumble.ToString().ToLowerInvariant())")
    $resultLines.Add("forceadapt_requested=$($runForceAdapt.ToString().ToLowerInvariant())")

    Write-Host ""
    Write-Host "Une seule session va verifier l'identite puis conserver cette autorisation." -ForegroundColor Yellow
    Write-Host "Pendant les 10 premieres secondes, bouger les deux sticks, presser LT/RT," -ForegroundColor Yellow
    Write-Host "la croix directionnelle et plusieurs boutons." -ForegroundColor Yellow
    if ($runForceAdapt) {
        Write-Host "La gachette RT sera automatiquement remise en mode Normal." -ForegroundColor Yellow
    }

    $portArguments = @("apex4-port-test", "--seconds", "10")
    if ($runRumble) { $portArguments += "--rumble" }
    if ($runForceAdapt) { $portArguments += "--forceadapt" }
    $portCode = Invoke-BridgeCommand `
        $portArguments "02-port-session.txt" $workingDirectory
    $resultLines.Add("port_session_exit_code=$portCode")

    if ($runRumble) {
        $resultLines.Add("rumble_felt=" + (Read-Host "Vibration ressentie ? [oui/non]"))
    }
    if ($runForceAdapt) {
        $resultLines.Add("forceadapt_felt=" + (Read-Host "Resistance ressentie sur RT ? [oui/non]"))
    }
    if ($portCode -ne 0) {
        throw "La session materielle APEX 4 a signale un echec ; consulter 02-port-session.txt."
    }
}
catch {
    $resultLines.Add("fatal_error=$($_.Exception.Message)")
    Write-Host ""
    Write-Host ("ERREUR : " + $_.Exception.Message) -ForegroundColor Red
}
finally {
    $resultLines.Add("finished_at=$([DateTimeOffset]::Now.ToString('o'))")
    Write-Utf8File (Join-Path $workingDirectory "00-result.txt") $resultLines.ToArray()

    $zipPath = Join-Path $OutputDirectory ("Apex4-Port-Test-" + $stamp + ".zip")
    Compress-Archive -Path (Join-Path $workingDirectory "*") `
        -DestinationPath $zipPath -CompressionLevel Optimal -Force
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    $resolvedWorking = [IO.Path]::GetFullPath($workingDirectory)
    if (-not $resolvedWorking.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refus de supprimer un dossier de travail hors du repertoire temporaire."
    }
    Remove-Item -LiteralPath $resolvedWorking -Recurse -Force

    Write-Host ""
    Write-Host "Termine. Renvoyer ce fichier :" -ForegroundColor Green
    Write-Host $zipPath -ForegroundColor Green
    if (-not $NonInteractive.IsPresent) {
        [void](Read-Host "Appuyer sur Entree pour fermer")
    }
}
