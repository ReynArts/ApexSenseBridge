$ErrorActionPreference='Stop'
$global:asbTestRemoved=$false
$global:asbTestDetachCalls=0
$global:asbTestLive=$true
function Get-Process { param($Name,$ErrorAction) }
function Get-PnpDevice { param([switch]$PresentOnly,$InstanceId,$ErrorAction) if(-not $global:asbTestRemoved){ [pscustomobject]@{InstanceId=$InstanceId} } }
function Get-PnpDeviceProperty { param($InstanceId,$KeyName) if($KeyName -eq 'DEVPKEY_Device_Stack'){[pscustomobject]@{Data=@('\Driver\usbip2_filter')}}else{[pscustomobject]@{Data=1}} }
function Get-NetTCPConnection { param($State,$ErrorAction) if($global:asbTestLive){[pscustomobject]@{LocalPort=4567}} }
$fake=Join-Path ([IO.Path]::GetTempPath()) ('asb-usbip-fixture-'+[Guid]::NewGuid().ToString('N')+'.ps1')
@'
$command,$p,$port=$args
if($command -eq 'port'){
 'Port 01: device in use'
 'Sony : DualSense (054c:0ce6)'
 ' -> usbip://127.0.0.1:4567/1-1'
}else{
 if($command -ne 'detach' -or $p -ne '-p' -or $port -ne '1'){throw 'Unsafe detach arguments'}
 $global:asbTestDetachCalls++
 $global:asbTestRemoved=$true
}
$global:LASTEXITCODE=0
'@ | Set-Content $fake
$helper=Join-Path $PSScriptRoot '..\scripts\Remove-OrphanedVirtualDualSense.ps1'
$id='USB\VID_054C&PID_0CE6\fixture'
try { & $helper -InstanceId $id -UsbipPath $fake; throw 'Live server was removed' } catch { if($_ -notmatch 'live server'){throw} }
if($global:asbTestDetachCalls){throw 'Unexpected detach'}
$global:asbTestLive=$false
& $helper -InstanceId $id -UsbipPath $fake -WhatIf
if($global:asbTestDetachCalls){throw 'WhatIf detached device'}
& $helper -InstanceId $id -UsbipPath $fake
if($global:asbTestDetachCalls -ne 1){throw 'Expected exactly one scoped detach'}
try { & $helper -InstanceId 'USB\VID_37D7&PID_2501\physical' -UsbipPath $fake; throw 'Physical controller accepted' } catch { if($_ -notmatch 'Not a DualSense USB root'){throw} }
'Legacy cleanup safety checks passed.'

Remove-Item -LiteralPath $fake -Force
