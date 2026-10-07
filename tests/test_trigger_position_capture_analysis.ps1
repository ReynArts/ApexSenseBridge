$ErrorActionPreference = 'Stop'
$capturePath = Join-Path ([System.IO.Path]::GetTempPath()) ('asb-position-' + [guid]::NewGuid().ToString('N') + '.jsonl')
$scriptPath = Join-Path $PSScriptRoot '../scripts/analyze-trigger-position-captures.ps1'

try {
    $fixture = @'
{"kind":"metadata","activate":true,"model":"fixture","connection_raw":2,"firmware_raw":0,"axis_source":"xinput_mapped"}
{"kind":"phase","phase":0,"name":"normal","mode":0,"params":[0,0,0,0,0]}
{"kind":"phase","phase":1,"name":"feedback_start38_force8","mode":1,"params":[38,8,0,0,0]}
{"kind":"hid_write","success":true}
{"kind":"axis","elapsed_us":0,"phase":1,"lt":0,"rt":0}
{"kind":"axis","elapsed_us":100000,"phase":1,"lt":0,"rt":50}
{"kind":"axis","elapsed_us":125000,"phase":1,"lt":0,"rt":50}
{"kind":"axis","elapsed_us":150000,"phase":1,"lt":0,"rt":50}
{"kind":"axis","elapsed_us":175000,"phase":1,"lt":0,"rt":50}
{"kind":"axis","elapsed_us":200000,"phase":1,"lt":0,"rt":50}
{"kind":"axis","elapsed_us":300000,"phase":1,"lt":0,"rt":60}
{"kind":"axis","elapsed_us":325000,"phase":1,"lt":0,"rt":60}
{"kind":"axis","elapsed_us":350000,"phase":1,"lt":0,"rt":60}
{"kind":"axis","elapsed_us":375000,"phase":1,"lt":0,"rt":60}
{"kind":"axis","elapsed_us":400000,"phase":1,"lt":0,"rt":60}
{"kind":"axis","elapsed_us":500000,"phase":1,"lt":0,"rt":255}
{"kind":"position_marker","elapsed_us":200000,"phase":1,"source":"human_button","label":"onset","rt":50}
{"kind":"position_marker","elapsed_us":400000,"phase":1,"source":"human_button","label":"onset","rt":60}
{"kind":"position_marker","phase":0,"source":"human_button","label":"onset","rt":200}
{"kind":"end","success":true,"neutralized":true,"interrupted":false}
'@
    Set-Content -LiteralPath $capturePath -Value $fixture -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if (!$report[0].transport_valid -or !$report[0].phases[0].marker_acquisition_usable) { throw 'Valid capture rejected' }
    if ($report[0].phases[0].subjective_positions_usable) { throw 'Unconfirmed marker meaning accepted' }
    $confirmed = @(& $scriptPath -Path $capturePath -ConfirmEffectBoundaries)
    if (!$confirmed[0].phases[0].subjective_positions_usable) { throw 'Confirmed onset rejected' }
    if ($report[0].phases[0].onset.count -ne 2 -or $report[0].phases[0].onset.median -ne 55) { throw 'Wrong marker summary' }
    if ($null -ne $report[0].phases[0].release) { throw 'Invented release marker' }
    $json = & $scriptPath -Path $capturePath -AsJson | ConvertFrom-Json
    if ($json[0].phases[0].rt_max -ne 255) { throw 'JSON round trip failed' }
    Set-Content -LiteralPath $capturePath -Value ($fixture.Replace('"lt":0', '"lt":1')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].phases[0].marker_acquisition_usable) { throw 'LT movement not rejected' }
    Set-Content -LiteralPath $capturePath -Value ($fixture.Replace('"rt":255', '"rt":0').Replace('"rt":60}', '"rt":0}').Replace('"rt":50}', '"rt":0}')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].phases[0].marker_acquisition_usable) { throw 'No movement not rejected' }
    Set-Content -LiteralPath $capturePath -Value ($fixture.Replace('"neutralized":true', '"neutralized":false')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].transport_valid) { throw 'Failed reset not rejected' }
    Set-Content -LiteralPath $capturePath -Value ($fixture.Replace('"interrupted":false', '"interrupted":true')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].transport_valid) { throw 'Interrupted capture not rejected' }
    Set-Content -LiteralPath $capturePath -Value ($fixture.Replace('"elapsed_us":100000,"phase":1,"lt":0,"rt":50', '"elapsed_us":100000,"phase":1,"lt":0,"rt":30')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].phases[0].held_onsets -ne 1 -or $report[0].phases[0].marker_quality[0].held_before_press) { throw 'Moving marker accepted as held' }
    $weapon = $fixture.Replace('"mode":1', '"mode":3').Replace('"label":"onset","rt":60', '"label":"release","rt":60')
    Set-Content -LiteralPath $capturePath -Value $weapon -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if (!$report[0].phases[0].marker_pair_acquisition_usable -or $report[0].phases[0].intervals[0].width_rt -ne 10) { throw 'Valid interval rejected' }
    if ($report[0].phases[0].subjective_interval_usable) { throw 'Unconfirmed interval meaning accepted' }
    $confirmed = @(& $scriptPath -Path $capturePath -ConfirmEffectBoundaries)
    if (!$confirmed[0].phases[0].subjective_interval_usable) { throw 'Confirmed interval rejected' }
    Set-Content -LiteralPath $capturePath -Value ($weapon.Replace('"elapsed_us":300000,"phase":1,"lt":0,"rt":60', '"elapsed_us":300000,"phase":1,"lt":0,"rt":40')) -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].phases[0].marker_pair_acquisition_usable) { throw 'Moving release accepted as held interval' }
    $snap = $weapon.Replace('"label":"onset"', '"label":"block"').Replace('"label":"release"', '"label":"after_break"')
    Set-Content -LiteralPath $capturePath -Value $snap -Encoding UTF8
    $report = @(& $scriptPath -Path $capturePath)
    if ($report[0].phases[0].block.median -ne 50 -or $report[0].phases[0].after_break.median -ne 60) { throw 'Snap markers not reported' }
    if ($report[0].phases[0].subjective_interval_usable -or $report[0].phases[0].intervals.Count -ne 0) { throw 'Snap distance mistaken for resistant zone length' }
    Write-Output 'Trigger position capture analysis passed.'
} finally {
    Remove-Item -LiteralPath $capturePath -ErrorAction SilentlyContinue
}
