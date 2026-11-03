$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$helperPath = Join-Path $projectRoot "portable\Install-Drivers.ps1"
$launcherPath = Join-Path $projectRoot "portable\Install-Drivers.cmd"
$readmePath = Join-Path $projectRoot "portable\README-PORTABLE.txt"
$buildPath = Join-Path $projectRoot "scripts\build-portable.ps1"
$helperText = [IO.File]::ReadAllText($helperPath)
$launcherBytes = [IO.File]::ReadAllBytes($launcherPath)
$launcherText = [IO.File]::ReadAllText($launcherPath)
$readmeText = [IO.File]::ReadAllText($readmePath)
$buildText = [IO.File]::ReadAllText($buildPath)
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("asb-portable-helper-test-" + [Guid]::NewGuid().ToString("N"))

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Portable driver helper test failed: $Message"
    }
}

function New-TestDirectory([string]$Name) {
    $path = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}

function New-Snapshot([hashtable]$Overrides) {
    $snapshot = [ordered]@{
        UsbipRegistered = $false
        UsbipVersion = ""
        UsbipUde = $false
        UsbipFilter = $false
        HidHideRegistered = $false
        HidHideVersion = ""
        HidHideService = $false
    }
    foreach ($key in $Overrides.Keys) { $snapshot[$key] = $Overrides[$key] }
    return [pscustomobject]$snapshot
}

function Test-Throws([scriptblock]$Action, [string]$Pattern, [string]$Message) {
    $thrown = $null
    try { & $Action | Out-Null } catch { $thrown = $_.Exception.Message }
    Assert-Test ($null -ne $thrown) "$Message (nothing was thrown)"
    Assert-Test ($thrown -match $Pattern) "$Message (got: $thrown)"
}

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($helperPath, [ref]$tokens, [ref]$parseErrors)
Assert-Test ($parseErrors.Count -eq 0) "Install-Drivers.ps1 does not parse"

$functionAsts = @($ast.FindAll({
    param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst]
}, $false))
$functionNames = @($functionAsts | ForEach-Object { $_.Name })

function Get-FunctionText([string]$Name) {
    $match = @($functionAsts | Where-Object { $_.Name -eq $Name })
    Assert-Test ($match.Count -eq 1) "function $Name is not defined exactly once"
    return $match[0].Extent.Text
}

foreach ($name in @(
    "Resolve-PrerequisitePlan", "Assert-PlanNotBlocked", "Assert-InstallerHash",
    "Start-DriverSetup", "Invoke-DriverInstallation", "Invoke-ElevatedStage",
    "New-DriverStage", "Copy-StagedLogs", "Remove-StageDirectory",
    "Invoke-BoundedInstaller", "Remove-StaleStages", "Get-InnermostMessage", "Get-NativeErrorCode", "Get-LaunchFailureHint",
    "Get-InstallerExitHint", "Get-PrerequisiteSnapshot", "Write-DriverLog",
    "Write-UsbHubNotice", "Get-EnvironmentSummary"
)) {
    Assert-Test ($functionNames -contains $name) "function $name is missing"
}

$allowedCommands = @(
    "Add-Content", "Copy-Item", "ForEach-Object", "Get-ChildItem", "Get-Content", "Get-Date", "Get-FileHash",
    "Get-ItemProperty", "Get-Process", "Join-Path", "New-Item", "New-Object", "Out-Null",
    "Remove-Item", "Set-Content", "Split-Path", "Test-Path", "Unblock-File", "Write-Host"
)
$usedCommands = @($ast.FindAll({
    param($node) $node -is [System.Management.Automation.Language.CommandAst]
}, $true) | ForEach-Object { $_.GetCommandName() } | Where-Object { $_ } | Sort-Object -Unique)
$unexpectedCommands = @($usedCommands | Where-Object {
    $_ -notin $allowedCommands -and $_ -notin $functionNames
})
Assert-Test ($unexpectedCommands.Count -eq 0) (
    "the helper uses commands outside the install-only allow list: " + ($unexpectedCommands -join ", "))

foreach ($forbidden in @(
    "pnputil", "devcon", "devnode", "bcdedit", "taskkill", "Stop-Process", "Start-Process",
    "Disable-PnpDevice", "Remove-PnpDevice", "Enable-PnpDevice", "Restart-Computer",
    "Stop-Computer", "shutdown", "msiexec", "Set-ItemProperty", "New-ItemProperty",
    "Remove-ItemProperty", "Restart-Service", "Stop-Service", "Set-Service", "sc.exe",
    "reg.exe", ".Kill(", "-Wait"
)) {
    Assert-Test (-not $helperText.Contains($forbidden)) "the helper must not contain '$forbidden'"
}
Assert-Test (
    [Regex]::Matches($helperText, '\[System\.Diagnostics\.Process\]::Start\(').Count -eq 2
) "the helper must start exactly two kinds of processes: the vendor installers and the elevated copy of itself"

$waitCalls = @($ast.FindAll({
    param($node)
    $node -is [System.Management.Automation.Language.InvokeMemberExpressionAst] -and
    $node.Member.Value -eq "WaitForExit"
}, $true))
Assert-Test ($waitCalls.Count -ge 2) "installer and elevated waits are missing"
foreach ($call in $waitCalls) {
    Assert-Test (
        $call.Arguments.Count -eq 1 -and $call.Arguments[0].Extent.Text.Contains("* 1000")
    ) "an unbounded WaitForExit call exists: $($call.Extent.Text)"
}

foreach ($pinned in @(
    '$usbipVersion = "0.9.8.0"',
    '$usbipInstallerSha256 = "81F426741F7EE2ED991FEBE24A22DACA8400B6AE2F171054E3FB404897E15D39"',
    '$hidHideVersion = "1.5.230"',
    '$hidHideInstallerSha256 = "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6"'
)) {
    Assert-Test ($helperText.Contains($pinned)) "pinned value changed: $pinned"
}

$installation = Get-FunctionText "Invoke-DriverInstallation"
$firstInstall = $installation.IndexOf("Invoke-BoundedInstaller")
$blockedGuard = $installation.IndexOf("Assert-PlanNotBlocked")
$usbipHash = $installation.IndexOf('Assert-InstallerHash $usbipInstaller $usbipInstallerSha256')
$hidHideHash = $installation.IndexOf('Assert-InstallerHash $hidHideInstaller $hidHideInstallerSha256')
Assert-Test (
    $blockedGuard -ge 0 -and $blockedGuard -lt $firstInstall
) "blocked prerequisite states must be rejected before any installer starts"
Assert-Test (
    $usbipHash -ge 0 -and $usbipHash -lt $firstInstall -and
    $hidHideHash -ge 0 -and $hidHideHash -lt $firstInstall
) "both installer hashes must be verified before the first installer starts"
Assert-Test (
    $installation.Contains("will retry HidHide only")
) "a failed HidHide step must explain that the next run resumes with HidHide only"
Assert-Test ($installation.Contains('"/quiet", "/norestart"')) "HidHide silent switches changed"
Assert-Test (
    $installation.Contains('"/VERYSILENT"') -and $installation.Contains('"/COMPONENTS=main,client"') -and
    $installation.Contains('"/SUPPRESSMSGBOXES"') -and $installation.Contains('"/NORESTART"') -and
    $installation.Contains('"/SP-"')
) "USBip silent switches changed"

$elevation = Get-FunctionText "Invoke-ElevatedStage"

Assert-Test (
    $elevation.Contains("' + `$stagedScript + '")
) "the elevated process must run the staged script"
Assert-Test (-not $elevation.Contains('$PSCommandPath')) `
    "the elevated process must not be pointed back at the original, possibly unreadable, path"
Assert-Test ($elevation.Contains('$startInfo.Verb = "runas"')) "elevation verb changed"
Assert-Test ($elevation.Contains("-ElevatedRelaunch")) "the elevated copy is not marked as such"
Assert-Test ($elevation.Contains('Copy-StagedLogs $StageRoot $PSScriptRoot')) "the log is not copied back"
Assert-Test ($elevation.Contains('$startInfo.UseShellExecute = $true')) "runas needs ShellExecute"

$setup = Get-FunctionText "Start-DriverSetup"
Assert-Test (
    $setup.Contains('New-DriverStage $PSCommandPath (Join-Path $PSScriptRoot "Drivers")')
) "the original script and Drivers folder must be staged"
Assert-Test (
    $setup.IndexOf("New-DriverStage") -lt $setup.IndexOf("Invoke-ElevatedStage")
) "staging must happen before elevation"
Assert-Test ($setup.Contains("finally")) "the temporary folder is not cleaned up on every path"

$stageFunction = Get-FunctionText "New-DriverStage"
Assert-Test (
    $stageFunction.Contains('Assert-InstallerHash $staged $installer.Sha256 $installer.Label')
) "the staged copy is not verified against the pinned SHA-256"
Assert-Test ($stageFunction.Contains("Unblock-File")) "staged files keep their download zone marker"

$relaunchBranch = [Regex]::Match($helperText, '(?s)if \(\$ElevatedRelaunch\) \{(.*?)exit 0').Groups[1].Value

Assert-Test ($relaunchBranch.Contains("Test-IsAdministrator")) `
    "the elevated copy must verify that it is elevated"
Assert-Test (
    -not $relaunchBranch.Contains("Start-DriverSetup") -and
    -not $relaunchBranch.Contains("Invoke-ElevatedStage")
) "the elevated copy must never relaunch itself"

Assert-Test ((Get-FunctionText "Get-LaunchFailureHint").Contains("1223")) `
    "error 1223 has no dedicated hint"
Assert-Test ($elevation.Contains("1223")) "a declined administrator prompt has no dedicated message"


$bareLineFeeds = 0
for ($i = 0; $i -lt $launcherBytes.Length; $i++) {
    if ($launcherBytes[$i] -eq 10 -and ($i -eq 0 -or $launcherBytes[$i - 1] -ne 13)) { $bareLineFeeds++ }
}
Assert-Test ($bareLineFeeds -eq 0) "Install-Drivers.cmd must keep CRLF line endings so labels parse"
Assert-Test ($launcherText.Contains('if not exist "%script%" goto :missing')) `
    "the launcher does not detect a missing Install-Drivers.ps1"
Assert-Test ($launcherText.Contains('-File "%script%"')) "the launcher does not use the checked path"
Assert-Test ($launcherText.Contains("C:\ApexSenseBridge-Portable")) "the launcher gives no local-drive advice"

Assert-Test ($readmeText.Contains("1223")) "the portable README does not explain error 1223"
Assert-Test ($readmeText.Contains("restarts every USB 3.0 hub")) "the portable README has no USB hub notice"
Assert-Test (
    $buildText.Contains("driver-manifest.json") -and $buildText.Contains("Get-FileHash -LiteralPath `$packaged")
) "the portable build does not verify the packaged installers against the manifest"

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

    function Write-Host { }

    $loadedLines = New-Object System.Collections.Generic.List[string]
    foreach ($statement in $ast.EndBlock.Statements) {
        if ($statement -is [System.Management.Automation.Language.AssignmentStatementAst] -and
            $statement.Right.Expression -is [System.Management.Automation.Language.ConstantExpressionAst]) {
            $loadedLines.Add($statement.Extent.Text)
        }
    }
    foreach ($function in $functionAsts) { $loadedLines.Add($function.Extent.Text) }
    $loadedPath = Join-Path $testRoot "loaded-helper.ps1"
    [IO.File]::WriteAllText($loadedPath, ($loadedLines -join "`r`n"), (New-Object Text.UTF8Encoding($false)))
    . $loadedPath
    $logPath = Join-Path $testRoot "driver-install.log"

    $fresh = Resolve-PrerequisitePlan (New-Snapshot @{})
    Assert-Test ($fresh.UsbipAction -eq "Install" -and $fresh.HidHideAction -eq "Install") `
        "a clean machine must install both prerequisites"

    $reporterSecondRun = Resolve-PrerequisitePlan (New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.8.0"; UsbipUde = $true; UsbipFilter = $true
    })
    Assert-Test ($reporterSecondRun.UsbipAction -eq "Ready" -and $reporterSecondRun.HidHideAction -eq "Install") `
        "USBip installed and HidHide missing must resume with HidHide only"

    $allReady = Resolve-PrerequisitePlan (New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.8.0"; UsbipUde = $true; UsbipFilter = $true
        HidHideRegistered = $true; HidHideVersion = "1.5.230"; HidHideService = $true
    })
    Assert-Test ($allReady.UsbipAction -eq "Ready" -and $allReady.HidHideAction -eq "Ready") `
        "an intact installation must be preserved"

    $oldUsbip = Resolve-PrerequisitePlan (New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.7.7"; UsbipUde = $true; UsbipFilter = $true
    })
    Assert-Test ($oldUsbip.UsbipAction -eq "Blocked") "an unsupported USBip version must be refused"
    Test-Throws { Assert-PlanNotBlocked $oldUsbip } "0\.9\.7\.7" "the refusal must name the version"

    $incompleteUsbip = Resolve-PrerequisitePlan (New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.8.0"; UsbipUde = $true; UsbipFilter = $false
    })
    Assert-Test ($incompleteUsbip.UsbipAction -eq "Blocked") "an incomplete USBip must be refused"

    $orphanUsbip = Resolve-PrerequisitePlan (New-Snapshot @{ UsbipFilter = $true })
    Assert-Test ($orphanUsbip.UsbipAction -eq "Blocked") "orphaned USBip services must be refused"

    $wrongHidHide = Resolve-PrerequisitePlan (New-Snapshot @{
        HidHideRegistered = $true; HidHideVersion = "1.5.0"; HidHideService = $true
    })
    Assert-Test ($wrongHidHide.HidHideAction -eq "Blocked") "an unsupported HidHide version must be refused"

    $orphanHidHide = Resolve-PrerequisitePlan (New-Snapshot @{ HidHideService = $true })
    Assert-Test ($orphanHidHide.HidHideAction -eq "Blocked") "an orphaned HidHide service must be refused"

    $blockedHidHideWithFreshUsbip = Resolve-PrerequisitePlan (New-Snapshot @{ HidHideService = $true })
    Assert-Test ($blockedHidHideWithFreshUsbip.UsbipAction -eq "Install") "test setup is wrong"
    Test-Throws { Assert-PlanNotBlocked $blockedHidHideWithFreshUsbip } "HidHide" `
        "a blocked HidHide must stop the run before USBip is installed"

    $wrapped = New-Object System.InvalidOperationException(
        "wrapper", (New-Object System.ComponentModel.Win32Exception(1223)))
    Assert-Test ((Get-NativeErrorCode $wrapped) -eq 1223) "the native error code is not unwrapped"
    Assert-Test ((Get-NativeErrorCode (New-Object System.Exception("plain"))) -eq 0) `
        "a plain exception must report no native code"

    $hint = Get-LaunchFailureHint "HidHide 1.5.230" 1223
    Assert-Test (
        $hint.Contains("Protection history") -and $hint.Contains("Run as administrator") -and
        $hint.Contains("run Install-Drivers.cmd again")
    ) "the 1223 hint is not actionable"
    Assert-Test ((Get-LaunchFailureHint "x" 740).Contains("administrator")) "the 740 hint is missing"
    Assert-Test ((Get-LaunchFailureHint "x" 12345) -eq "") "unknown launch errors must not invent advice"
    Assert-Test ((Get-InstallerExitHint "x" 1618).Contains("Windows Installer")) "the 1618 hint is missing"

    $logPath = Join-Path $testRoot "missing-directory\driver-install.log"
    Write-DriverLog "an unwritable log must not stop the setup"
    $logPath = Join-Path $testRoot "driver-install.log"
    Write-DriverLog "first"
    Assert-Test ((Get-Content -LiteralPath $logPath -Raw).Contains("first")) "the log is not written"
    Assert-Test ((Get-EnvironmentSummary).Contains("administrator")) "the environment summary is empty"

    $cyrillic = -join ([char[]](0x041F, 0x0430, 0x043A, 0x0435, 0x0442))
    $packageRoot = New-TestDirectory ("$cyrillic ApexSenseBridge [portable]")
    $driversSource = New-TestDirectory ("$cyrillic ApexSenseBridge [portable]\Drivers")
    $sourceScript = Join-Path $packageRoot "Install-Drivers.ps1"
    Set-Content -LiteralPath $sourceScript -Value "exit 0" -Encoding ASCII
    $fakeInstaller = Join-Path $driversSource "Fake-Installer.exe"
    [IO.File]::WriteAllBytes($fakeInstaller, [byte[]](1..250))
    $fakeHash = (Get-FileHash -LiteralPath $fakeInstaller -Algorithm SHA256).Hash
    $zoneMarked = $false
    try {
        Set-Content -LiteralPath $fakeInstaller -Stream Zone.Identifier `
            -Value "[ZoneTransfer]`r`nZoneId=3" -ErrorAction Stop
        $zoneMarked = $true
    }
    catch { }

    $stageTemp = New-TestDirectory "stage-temp"
    $good = [pscustomobject]@{ Name = "Fake-Installer.exe"; Sha256 = $fakeHash; Label = "fake" }
    $stageRoot = New-DriverStage $sourceScript $driversSource @($good) $stageTemp
    Assert-Test ($stageRoot.StartsWith($stageTemp)) "the stage is not created under the requested temp folder"
    Assert-Test ((Split-Path -Leaf $stageRoot).StartsWith("ApexSenseBridge-Drivers-")) "stage name changed"
    Assert-Test (Test-Path -LiteralPath (Join-Path $stageRoot "Install-Drivers.ps1")) "the script was not staged"
    $stagedInstaller = Join-Path $stageRoot "Drivers\Fake-Installer.exe"
    Assert-Test (
        (Get-FileHash -LiteralPath $stagedInstaller -Algorithm SHA256).Hash -eq $fakeHash
    ) "the staged installer differs from the source"
    if ($zoneMarked) {
        $zone = Get-Content -LiteralPath $stagedInstaller -Stream Zone.Identifier -ErrorAction SilentlyContinue
        Assert-Test ($null -eq $zone) "the staged installer still carries its Mark of the Web"
    }

    $bad = [pscustomobject]@{ Name = "Fake-Installer.exe"; Sha256 = ("0" * 64); Label = "fake" }
    $before = @(Get-ChildItem -LiteralPath $stageTemp -Directory).Count
    Test-Throws { New-DriverStage $sourceScript $driversSource @($bad) $stageTemp } "SHA-256 integrity" `
        "a tampered installer must be rejected"
    Assert-Test (@(Get-ChildItem -LiteralPath $stageTemp -Directory).Count -eq $before) `
        "a rejected stage must not leave files behind"

    $missing = [pscustomobject]@{ Name = "Not-There.exe"; Sha256 = $fakeHash; Label = "fake" }
    Test-Throws { New-DriverStage $sourceScript $driversSource @($missing) $stageTemp } "is missing" `
        "a missing installer must be reported"
    Assert-Test (@(Get-ChildItem -LiteralPath $stageTemp -Directory).Count -eq $before) `
        "a failed stage must not leave files behind"

    Remove-StageDirectory $stageRoot
    Assert-Test (-not (Test-Path -LiteralPath $stageRoot)) "the stage was not removed"
    $staleTemp = New-TestDirectory "stale-temp"
    $oldStage = New-TestDirectory "stale-temp\ApexSenseBridge-Drivers-old"
    $newStage = New-TestDirectory "stale-temp\ApexSenseBridge-Drivers-new"
    $unrelated = New-TestDirectory "stale-temp\Something-else"
    (Get-Item -LiteralPath $oldStage).LastWriteTime = (Get-Date).AddDays(-3)
    (Get-Item -LiteralPath $unrelated).LastWriteTime = (Get-Date).AddDays(-3)
    Remove-StaleStages $staleTemp
    Assert-Test (-not (Test-Path -LiteralPath $oldStage)) "an old abandoned stage was not removed"
    Assert-Test (Test-Path -LiteralPath $newStage) "a recent stage may still be in use and must be kept"
    Assert-Test (Test-Path -LiteralPath $unrelated) "unrelated folders must never be removed"
    $keep = New-TestDirectory "keep-me"
    Remove-StageDirectory $keep
    Assert-Test (Test-Path -LiteralPath $keep) "a folder without the stage prefix must never be deleted"
    Remove-StageDirectory $env:SystemRoot
    Assert-Test (Test-Path -LiteralPath $env:SystemRoot) "a folder outside the temp directory must never be deleted"

    $logSource = New-TestDirectory "log-source"
    $logTarget = New-TestDirectory "log-target"
    Set-Content -LiteralPath (Join-Path $logSource "driver-install.log") -Value "child line" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $logSource "usbip-upstream.log") -Value "upstream line" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $logTarget "driver-install.log") -Value "launcher line" -Encoding UTF8
    Copy-StagedLogs $logSource $logTarget
    $merged = Get-Content -LiteralPath (Join-Path $logTarget "driver-install.log") -Raw
    Assert-Test (
        $merged.IndexOf("launcher line") -ge 0 -and $merged.IndexOf("child line") -gt $merged.IndexOf("launcher line")
    ) "the staged log must be appended after the existing log"
    Assert-Test (
        (Get-Content -LiteralPath (Join-Path $logTarget "usbip-upstream.log") -Raw).Contains("upstream line")
    ) "the upstream log was not copied back"
    $freshTarget = New-TestDirectory "log-target-fresh"
    Copy-StagedLogs $logSource $freshTarget
    Assert-Test (Test-Path -LiteralPath (Join-Path $freshTarget "driver-install.log")) `
        "the staged log was not copied to a folder without a log"

    $fakeName = "asb-fake-installer-" + [Guid]::NewGuid().ToString("N").Substring(0, 8)
    $fakeExe = Join-Path $testRoot ($fakeName + ".exe")
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot "System32\cmd.exe") -Destination $fakeExe
    Invoke-BoundedInstaller $fakeExe @("/c", "exit", "0") "fake" 30
    Invoke-BoundedInstaller $fakeExe @("/c", "exit", "3010") "fake" 30
    Test-Throws { Invoke-BoundedInstaller $fakeExe @("/c", "exit", "7") "fake" 30 } "exit code 7" `
        "a failing installer must be reported"
    Test-Throws { Invoke-BoundedInstaller $fakeExe @("/c", "exit", "1618") "fake" 30 } "Windows Installer" `
        "exit code 1618 must be explained"

    Test-Throws {
        Invoke-BoundedInstaller $fakeExe @("/c", "ping", "-n", "6", "127.0.0.1", ">nul") "fake" 1
    } "not terminated" "a slow installer must be abandoned without being killed"
    Assert-Test (@(Get-Process -Name $fakeName -ErrorAction SilentlyContinue).Count -gt 0) `
        "a timed-out installer must be left running"
    Test-Throws { Invoke-BoundedInstaller $fakeExe @("/c", "exit", "0") "fake" 30 } "already running" `
        "a second installer must not start while the first one runs"
    $deadline = (Get-Date).AddSeconds(40)
    while ((@(Get-Process -Name $fakeName -ErrorAction SilentlyContinue).Count -gt 0) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
    }

    $notExecutable = Join-Path $testRoot "not-an-installer.exe"
    [IO.File]::WriteAllBytes($notExecutable, [byte[]](65..120))
    Test-Throws { Invoke-BoundedInstaller $notExecutable @("/quiet") "fake" 30 } "could not be started \(Windows error (193|216)" `
        "a launch failure must report its Windows error code"

    $realDrivers = Join-Path $projectRoot "third_party\prerequisites"
    $realStageTemp = New-TestDirectory "real-stage-temp"
    $realInstallers = @(
        [pscustomobject]@{ Name = $usbipInstallerName; Sha256 = $usbipInstallerSha256; Label = "usbip-win2" },
        [pscustomobject]@{ Name = $hidHideInstallerName; Sha256 = $hidHideInstallerSha256; Label = "HidHide" }
    )
    $realStage = New-DriverStage $sourceScript $realDrivers $realInstallers $realStageTemp
    foreach ($installer in $realInstallers) {
        Assert-Test (
            (Get-FileHash -LiteralPath (Join-Path $realStage ("Drivers\" + $installer.Name)) -Algorithm SHA256).Hash -eq
            $installer.Sha256
        ) "the pinned $($installer.Label) installer in third_party does not match its staged SHA-256"
    }
    Remove-StageDirectory $realStage

    $oddPackage = New-TestDirectory "pkg (x86) & portable"
    Copy-Item -LiteralPath $launcherPath -Destination (Join-Path $oddPackage "Install-Drivers.cmd")
    $cmdInfo = New-Object System.Diagnostics.ProcessStartInfo
    $cmdInfo.FileName = Join-Path $env:SystemRoot "System32\cmd.exe"
    $cmdInfo.Arguments = '/d /c ""' + (Join-Path $oddPackage "Install-Drivers.cmd") + '""'
    $cmdInfo.UseShellExecute = $false
    $cmdInfo.RedirectStandardInput = $true
    $cmdInfo.RedirectStandardOutput = $true
    $cmdProcess = [System.Diagnostics.Process]::Start($cmdInfo)
    $cmdProcess.StandardInput.Close()
    $cmdOutput = $cmdProcess.StandardOutput.ReadToEnd()
    Assert-Test ($cmdProcess.WaitForExit(30000)) "the launcher did not finish"
    Assert-Test ($cmdProcess.ExitCode -eq 2) "a missing Install-Drivers.ps1 must fail with exit code 2"
    Assert-Test (
        $cmdOutput.Contains("was not found next to this file") -and $cmdOutput.Contains("pkg (x86) & portable")
    ) "the launcher must name the missing script even when the path contains special characters"
    $script:calls = New-Object System.Collections.ArrayList
    $script:snapshot = New-Snapshot @{}
    $script:isAdmin = $false
    $script:elevatedExit = 0
    $script:keepStage = $false
    $script:installThrows = $false

    function Get-PrerequisiteSnapshot { return $script:snapshot }
    function Test-IsAdministrator { return $script:isAdmin }
    function New-DriverStage([string]$ScriptPath, [string]$DriversSource, [object[]]$Installers, [string]$TempRoot) {
        [void]$script:calls.Add("stage:" + (($Installers | ForEach-Object { $_.Name }) -join ","))
        return (Join-Path $testRoot "orchestration-stage")
    }
    function Invoke-DriverInstallation([string]$DriversRoot) {
        [void]$script:calls.Add("install:" + (Split-Path -Leaf $DriversRoot))
        if ($script:installThrows) { throw "simulated installer failure" }
    }
    function Invoke-ElevatedStage([string]$StageRoot) {
        [void]$script:calls.Add("elevate")
        return [pscustomobject]@{ ExitCode = $script:elevatedExit; KeepStage = $script:keepStage }
    }
    function Remove-StageDirectory([string]$StageRoot) { [void]$script:calls.Add("remove") }
    function Remove-StaleStages([string]$TempRoot) { }

    function Reset-Orchestration {
        $script:calls.Clear()
        $script:elevatedExit = 0
        $script:keepStage = $false
        $script:installThrows = $false
        if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }
    }

    Reset-Orchestration
    $script:snapshot = New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.8.0"; UsbipUde = $true; UsbipFilter = $true
        HidHideRegistered = $true; HidHideVersion = "1.5.230"; HidHideService = $true
    }
    $result = @(Start-DriverSetup)[-1]
    Assert-Test ($result -eq 0 -and $script:calls.Count -eq 0) "nothing to install must not stage or elevate"

    Reset-Orchestration
    $script:snapshot = New-Snapshot @{ UsbipFilter = $true }
    Test-Throws { Start-DriverSetup } "Orphaned USBip" "a blocked state must stop before staging"
    Assert-Test ($script:calls.Count -eq 0) "a blocked state must not stage, elevate or install"

    Reset-Orchestration
    $script:snapshot = New-Snapshot @{
        UsbipRegistered = $true; UsbipVersion = "0.9.8.0"; UsbipUde = $true; UsbipFilter = $true
    }
    $script:isAdmin = $true
    $result = @(Start-DriverSetup)[-1]
    Assert-Test ($result -eq 0) "the HidHide-only resume failed"
    Assert-Test (
        ($script:calls -join "|") -eq "stage:HidHide_1.5.230_x64.exe|install:Drivers|remove"
    ) "an installed USBip must resume with HidHide only (got: $($script:calls -join '|'))"
    Assert-Test (
        -not (Get-Content -LiteralPath $logPath -Raw).Contains("restarts every USB 3.0 hub")
    ) "the USB hub notice is shown although USBip is not being installed"

    Reset-Orchestration
    $script:snapshot = New-Snapshot @{}
    $script:isAdmin = $false
    $script:elevatedExit = 5
    $result = @(Start-DriverSetup)[-1]
    Assert-Test ($result -eq 5) "the elevated exit code is not propagated"
    Assert-Test (
        ($script:calls -join "|") -eq "stage:USBip-0.9.8.0-x64.exe,HidHide_1.5.230_x64.exe|elevate|remove"
    ) "a clean machine must stage both installers and elevate (got: $($script:calls -join '|'))"
    Assert-Test (
        (Get-Content -LiteralPath $logPath -Raw).Contains("restarts every USB 3.0 hub")
    ) "the USB hub notice is missing before USBip is installed"

    Reset-Orchestration
    $script:elevatedExit = 1
    $script:keepStage = $true
    $result = @(Start-DriverSetup)[-1]
    Assert-Test (
        $result -eq 1 -and ($script:calls -join "|") -eq "stage:USBip-0.9.8.0-x64.exe,HidHide_1.5.230_x64.exe|elevate"
    ) "a stage that must be kept (running installer, uncopied log) was removed"

    Reset-Orchestration
    $script:isAdmin = $true
    $script:installThrows = $true
    Test-Throws { Start-DriverSetup } "simulated installer failure" "an installation failure must propagate"
    Assert-Test (($script:calls -join "|").EndsWith("|remove")) "the stage must be removed after a failure"
}
finally {
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Process -Name "asb-fake-installer-*" -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Output "Portable driver helper contract passed."
