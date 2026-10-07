param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path,
    [switch]$AsJson,
    [switch]$ConfirmEffectBoundaries
)

$ErrorActionPreference = 'Stop'

function Get-CaptureHash {
    param([string]$CapturePath)
    $stream = [System.IO.File]::OpenRead($CapturePath)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Get-MarkerSummary {
    param([object[]]$Markers)
    $values = @($Markers | ForEach-Object { [int]$_.rt } | Sort-Object)
    if ($values.Count -eq 0) { return $null }
    $middle = [int][Math]::Floor($values.Count / 2)
    $median = [double]$values[$middle]
    if ($values.Count % 2 -eq 0) { $median = ($values[$middle - 1] + $values[$middle]) / 2.0 }
    [pscustomobject]@{
        count = $values.Count
        min = $values[0]
        max = $values[-1]
        median = $median
    }
}

function Get-MarkerQuality {
    param([object]$Marker, [object[]]$Samples)
    $window = @($Samples | Where-Object {
        $_.elapsed_us -ge ($Marker.elapsed_us - 150000) -and $_.elapsed_us -le $Marker.elapsed_us
    })
    $range = $window | Measure-Object -Property rt -Minimum -Maximum
    $times = $window | Measure-Object -Property elapsed_us -Minimum -Maximum
    $matching = @($window | Where-Object { $_.elapsed_us -eq $Marker.elapsed_us -and $_.rt -eq $Marker.rt }).Count -gt 0
    $held = $matching -and $window.Count -ge 5 -and ($times.Maximum - $times.Minimum) -ge 100000 -and
        ($range.Maximum - $range.Minimum) -le 2
    [pscustomobject]@{
        label = $Marker.label
        elapsed_us = $Marker.elapsed_us
        rt = $Marker.rt
        samples_before_press = $window.Count
        spread_rt_before_press = if ($window.Count -gt 0) { $range.Maximum - $range.Minimum } else { $null }
        held_before_press = $held
    }
}

$results = @(foreach ($capturePath in $Path) {
    $resolved = (Resolve-Path -LiteralPath $capturePath).Path
    $records = @(Get-Content -LiteralPath $resolved | ForEach-Object { $_ | ConvertFrom-Json })
    $metadata = @($records | Where-Object kind -eq 'metadata')
    $ending = @($records | Where-Object kind -eq 'end')
    if ($metadata.Count -ne 1 -or $ending.Count -ne 1) { throw "Incomplete capture: $resolved" }
    $writes = @($records | Where-Object kind -eq 'hid_write')
    $failedWrites = @($writes | Where-Object { $_.success -ne $true }).Count
    $transportValid = $metadata[0].activate -eq $true -and $ending[0].success -eq $true -and
        $ending[0].neutralized -eq $true -and $ending[0].interrupted -ne $true -and
        $writes.Count -gt 0 -and $failedWrites -eq 0
    $phaseReports = @(foreach ($phase in @($records | Where-Object { $_.kind -eq 'phase' -and $_.mode -ne 0 })) {
        $samples = @($records | Where-Object { $_.kind -eq 'axis' -and $_.phase -eq $phase.phase })
        $markers = @($records | Where-Object { $_.kind -eq 'position_marker' -and $_.phase -eq $phase.phase })
        $onsets = @($markers | Where-Object { $_.label -eq 'onset' -and $_.source -eq 'human_button' -and $_.rt -ge 0 -and $_.rt -le 255 })
        $releases = @($markers | Where-Object { $_.label -eq 'release' -and $_.source -eq 'human_button' -and $_.rt -ge 0 -and $_.rt -le 255 })
        $blocks = @($markers | Where-Object { $_.label -eq 'block' -and $_.source -eq 'human_button' -and $_.rt -ge 0 -and $_.rt -le 255 })
        $afterBreaks = @($markers | Where-Object { $_.label -eq 'after_break' -and $_.source -eq 'human_button' -and $_.rt -ge 0 -and $_.rt -le 255 })
        $quality = @(@($onsets + $releases + $blocks + $afterBreaks) | Sort-Object elapsed_us | ForEach-Object {
            Get-MarkerQuality -Marker $_ -Samples $samples
        })
        $heldOnsets = @($quality | Where-Object { $_.label -eq 'onset' -and $_.held_before_press })
        $heldReleases = @($quality | Where-Object { $_.label -eq 'release' -and $_.held_before_press })
        $pendingOnset = $null
        $intervals = @(foreach ($marker in $quality) {
            if ($marker.label -eq 'onset') { $pendingOnset = $marker }
            elseif ($marker.label -eq 'release' -and $null -ne $pendingOnset) {
                [pscustomobject]@{
                    onset_rt = $pendingOnset.rt
                    release_rt = $marker.rt
                    width_rt = $marker.rt - $pendingOnset.rt
                    positions_held = $pendingOnset.held_before_press -and $marker.held_before_press -and
                        $marker.elapsed_us -gt $pendingOnset.elapsed_us -and $marker.rt -ge $pendingOnset.rt
                }
                $pendingOnset = $null
            }
        })
        $range = $samples | Measure-Object -Property rt -Minimum -Maximum
        $leftUsed = @($samples | Where-Object { $_.lt -ne 0 }).Count -gt 0
        $moved = $samples.Count -gt 1 -and $range.Maximum -gt $range.Minimum
        $observationsUsable = $transportValid -and $moved -and !$leftUsed -and $heldOnsets.Count -gt 0
        [pscustomobject]@{
            name = $phase.name
            mode = $phase.mode
            params = $phase.params
            samples = $samples.Count
            rt_min = $range.Minimum
            rt_max = $range.Maximum
            lt_used = $leftUsed
            onset = Get-MarkerSummary -Markers $onsets
            release = Get-MarkerSummary -Markers $releases
            block = Get-MarkerSummary -Markers $blocks
            after_break = Get-MarkerSummary -Markers $afterBreaks
            marker_quality = $quality
            held_onsets = $heldOnsets.Count
            held_releases = $heldReleases.Count
            intervals = $intervals
            marker_acquisition_usable = $observationsUsable
            marker_pair_acquisition_usable = $observationsUsable -and $phase.mode -eq 3 -and
                @($intervals | Where-Object positions_held).Count -gt 0
            subjective_positions_usable = $observationsUsable -and $ConfirmEffectBoundaries.IsPresent
            subjective_interval_usable = $observationsUsable -and $ConfirmEffectBoundaries.IsPresent -and
                $phase.mode -eq 3 -and @($intervals | Where-Object positions_held).Count -gt 0
        }
    })
    [pscustomobject]@{
        path = $resolved
        sha256 = Get-CaptureHash -CapturePath $resolved
        model = $metadata[0].model
        connection_raw = $metadata[0].connection_raw
        firmware_raw = $metadata[0].firmware_raw
        axis_source = $metadata[0].axis_source
        effect_boundary_meaning_confirmed = $ConfirmEffectBoundaries.IsPresent
        transport_valid = $transportValid
        failed_writes = $failedWrites
        phases = $phaseReports
        limits = @('Human markers, not measured force or angle.',
            'Block and after-break positions do not measure the length of a resistant zone.',
            'Held markers require human confirmation of their meaning before interpretation.',
            'Mapped XInput axes may contain dead zones or nonlinear curves.',
            'Button timing includes human reaction delay; no automatic calibration.',
            'Firmware zero is unknown, not a verified firmware version.')
    }
})

if ($AsJson) { ConvertTo-Json -InputObject $results -Depth 8 }
else { $results }
