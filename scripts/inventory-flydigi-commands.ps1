[CmdletBinding()]
param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '../work/flydigi-windows-4.2.2.3/decompiled/ControllerSdk'),
    [switch]$AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $SourceDirectory).Path.TrimEnd('\', '/')
$methodPattern = 'public override byte CommandId\(\)\s*\{(?<body>[^}]+)\}'
$classPattern = '(?:public|private|internal|protected) (?:sealed |abstract )?class\s+(?<name>\w+)'
$commands = @(
    foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Filter '*.cs') {
        $source = [IO.File]::ReadAllText($sourceFile.FullName)
        foreach ($methodMatch in [regex]::Matches($source, $methodPattern)) {
            $classMatches = [regex]::Matches($source.Substring(0, $methodMatch.Index), $classPattern)
            if ($classMatches.Count -eq 0) {
                throw "Cannot identify command class in $($sourceFile.FullName)"
            }
            $expression = ($methodMatch.Groups['body'].Value -replace '\s+', ' ').Trim()
            $literalMatch = [regex]::Match($expression, '^return (?<id>\d+);$')
            $commandId = if ($literalMatch.Success) { [int]$literalMatch.Groups['id'].Value } else { $null }
            [pscustomobject]@{
                Source = $sourceFile.FullName.Substring($sourceRoot.Length + 1).Replace('\', '/')
                Class = $classMatches[$classMatches.Count - 1].Groups['name'].Value
                CommandId = $commandId
                Expression = $expression
                Line = [regex]::Matches($source.Substring(0, $methodMatch.Index), '\n').Count + 1
            }
        }
    }
) | Sort-Object Source, Class

if ($commands.Count -eq 0) {
    throw "No block-bodied CommandId overrides found in $sourceRoot"
}
if ($AsJson) {
    ConvertTo-Json -InputObject @($commands) -Depth 3
} else {
    $commands
}
