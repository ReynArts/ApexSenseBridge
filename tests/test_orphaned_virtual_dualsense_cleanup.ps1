$ErrorActionPreference='Stop'
$global:asbTestRemoved=$false
$global:asbTestDetachCalls=0
$global:asbTestLive=$true
$global:asbTestImportMode='sony'
$global:asbTestPnpCalls=0
function Get-Process { param($Name,$ErrorAction) }
function Get-PnpDevice { param([switch]$PresentOnly,$InstanceId,$ErrorAction) if(-not $global:asbTestRemoved){ [pscustomobject]@{InstanceId=$InstanceId} } }
function Get-PnpDeviceProperty { param($InstanceId,$KeyName) if($KeyName -eq 'DEVPKEY_Device_Stack'){[pscustomobject]@{Data=@('\Driver\usbip2_filter')}}else{[pscustomobject]@{Data=1}} }
function Get-NetTCPConnection { param($State,$ErrorAction) if($global:asbTestLive){[pscustomobject]@{LocalPort=4567}} }
$fake=Join-Path ([IO.Path]::GetTempPath()) ('asb-usbip-fixture-'+[Guid]::NewGuid().ToString('N')+'.ps1')
@'
$command,$p,$port=$args
if($command -eq 'port'){
 if($global:asbTestImportMode -eq 'empty'){ $global:LASTEXITCODE=0; return }
 if($global:asbTestImportMode -eq 'failure'){ $global:LASTEXITCODE=1; return }
 if($global:asbTestImportMode -eq 'unknown'){ 'Unexpected output'; $global:LASTEXITCODE=0; return }
 'Imported USB devices'
 if($global:asbTestImportMode -eq 'none'){ $global:LASTEXITCODE=0; return }
 'Port 01: device in use'
 if($global:asbTestImportMode -eq 'sony'){ 'Sony : DualSense (054c:0ce6)' }else{ 'Other controller (ffff:eeee)' }
 ' -> usbip://127.0.0.1:4567/1-1'
}elseif($command -eq '/remove-device'){
 if($p -ne 'USB\VID_054C&PID_0CE6\fixture' -or $port){throw 'Unsafe PnP removal arguments'}
 $global:asbTestPnpCalls++
 $global:asbTestRemoved=$true
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
$global:asbTestRemoved=$false
$global:asbTestImportMode='other'
try { & $helper -InstanceId $id -UsbipPath $fake -PnpUtilPath $fake; throw 'Other import removed' } catch { if($_ -notmatch 'unrecognized import'){throw} }
if($global:asbTestDetachCalls -ne 1 -or $global:asbTestPnpCalls){throw 'Removed another import'}
$global:asbTestImportMode='none'
& $helper -InstanceId $id -UsbipPath $fake -PnpUtilPath $fake -WhatIf
if($global:asbTestPnpCalls){throw 'WhatIf removed a Windows device'}
& $helper -InstanceId $id -UsbipPath $fake -PnpUtilPath $fake
if($global:asbTestPnpCalls -ne 1 -or $global:asbTestDetachCalls -ne 1){throw 'Expected exactly one targeted PnP removal without detaching a port'}
$global:asbTestRemoved=$false
foreach($mode in @('failure','unknown')){
 $global:asbTestImportMode=$mode
 $caught=$false
 try { & $helper -InstanceId $id -UsbipPath $fake -PnpUtilPath $fake } catch { $caught=$true }
 if(-not $caught -or $global:asbTestPnpCalls -ne 1){throw 'Invalid enumeration allowed a removal'}
}
$global:asbTestImportMode='empty'
& $helper -InstanceId $id -UsbipPath $fake -PnpUtilPath $fake
if($global:asbTestPnpCalls -ne 2 -or $global:asbTestDetachCalls -ne 1){throw 'Successful empty enumeration did not remove only the identified Windows node'}
try { & $helper -InstanceId 'USB\VID_37D7&PID_2501\physical' -UsbipPath $fake; throw 'Physical controller accepted' } catch { if($_ -notmatch 'Not a DualSense USB root'){throw} }
'Legacy cleanup safety checks passed.'

Remove-Item -LiteralPath $fake -Force
