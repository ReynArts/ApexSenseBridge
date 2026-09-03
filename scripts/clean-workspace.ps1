[CmdletBinding(SupportsShouldProcess)]
param(
    [switch]$IncludeBuildOutputs,
    [switch]$IncludeToolCache,
    [switch]$IncludeScratch
)

$ErrorActionPreference = "Stop"

$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$workspacePrefix = $workspaceRoot.TrimEnd('\') + '\'
$protectedRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $workspaceRoot "dist")),
    [System.IO.Path]::GetFullPath((Join-Path $workspaceRoot "notes"))
)

function Assert-SafeTarget([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $fullPath.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a path outside the workspace: $fullPath"
    }

    foreach ($protectedRoot in $protectedRoots) {
        $protectedPrefix = $protectedRoot.TrimEnd('\') + '\'
        if ($fullPath.Equals($protectedRoot, [StringComparison]::OrdinalIgnoreCase) -or
            $fullPath.StartsWith($protectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean protected content: $fullPath"
        }
    }

    return $fullPath
}

function Remove-WorkspaceItem([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }

    $safePath = Assert-SafeTarget $Path
    if ($PSCmdlet.ShouldProcess($safePath, "Remove generated workspace content")) {
        Remove-Item -LiteralPath $safePath -Recurse -Force
    }
}

$temporaryDirectories = Get-ChildItem -LiteralPath $workspaceRoot -Directory -Force |
    Where-Object { $_.Name -like ".tmp-*" }
foreach ($directory in $temporaryDirectories) {
    Remove-WorkspaceItem $directory.FullName
}

@(
    "build-verify",
    "Apex4-Diagnostics-Collector",
    "ApexSenseBridgeTray\bin",
    "ApexSenseBridgeTray\obj",
    "playnite\ApexSenseBridge\bin",
    "playnite\ApexSenseBridge\obj"
) | ForEach-Object {
    Remove-WorkspaceItem (Join-Path $workspaceRoot $_)
}

@("ApexSenseBridgeTray.exe", "diagnose-*.json", "diagnostics-*.json", "tray_*.log") |
    ForEach-Object {
        Get-ChildItem -Path (Join-Path $workspaceRoot $_) -File -Force -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-WorkspaceItem $_.FullName }
    }

if ($IncludeBuildOutputs) {
    Remove-WorkspaceItem (Join-Path $workspaceRoot "build-win")
}
if ($IncludeToolCache) {
    Remove-WorkspaceItem (Join-Path $workspaceRoot ".tools")
}
if ($IncludeScratch) {
    Remove-WorkspaceItem (Join-Path $workspaceRoot "scratch")
}
