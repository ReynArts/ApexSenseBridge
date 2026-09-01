#define AppName "ApexSenseBridge"
#define AppVersion "0.3.0"
#define AppPublisher "ApexSenseBridge contributors"
#define AppId "{{5F8B1901-93E1-41E2-96B4-F1B278A5A630}"
#define ExtensionId "ApexSenseBridge_e41b1737-6753-4b59-bc65-4fdd6a7df7f4"
#define UsbipProductKey "{{199505b0-b93d-4521-a8c7-897818e0205a}_is1"
#define HidHideProductCode "{{01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\ApexSenseBridge
DefaultGroupName=ApexSenseBridge
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
PrivilegesRequired=admin
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=ApexSenseBridge-Setup
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
RestartIfNeededByRun=yes
UninstallDisplayIcon={app}\ApexSenseBridgeControl.exe
LicenseFile=..\LICENSE
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build-win\Release\ApexSenseBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\ApexSenseBridgeControl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\viiper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\VIIPER-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\build-win\Release\VIIPER-SOURCE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\Licenses"; DestName: "ApexSenseBridge-LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "driver-manifest.json"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\third_party\prerequisites\USBIP-WIN2-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\third_party\prerequisites\HIDHIDE-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion

; The two official prerequisite installers are compressed inside the setup and
; extracted only when the corresponding pinned version must be installed.
Source: "..\third_party\prerequisites\USBip-0.9.7.7-x64.exe"; DestDir: "{tmp}\ApexSenseBridge"; Flags: deleteafterinstall
Source: "..\third_party\prerequisites\HidHide_1.5.230_x64.exe"; DestDir: "{tmp}\ApexSenseBridge"; Flags: deleteafterinstall

; Install the unpacked extension directly. Playnite keeps its profiles under
; ExtensionsData, so upgrades migrate them without requiring a .pext prompt.
Source: "..\playnite\ApexSenseBridge\bin\Release\ApexSenseBridge.dll"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\extension.yaml"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\icon.png"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\Localization\*"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}\Localization"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{tmp}\ApexSenseBridge\USBip-0.9.7.7-x64.exe"; Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-"; StatusMsg: "Installation de usbip-win2 0.9.7.7…"; Flags: runhidden waituntilterminated; Check: NeedUsbip
Filename: "{tmp}\ApexSenseBridge\HidHide_1.5.230_x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installation de HidHide 1.5.230…"; Flags: runhidden waituntilterminated; Check: NeedHidHide

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "ExecutablePath"; ValueData: "{app}\ApexSenseBridge.exe"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "ControlPanelPath"; ValueData: "{app}\ApexSenseBridgeControl.exe"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UninstallExecutable"; ValueData: "{uninstallexe}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipVersion"; ValueData: "0.9.7.7"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipProductCode"; ValueData: "{#UsbipProductKey}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipOriginalInf"; ValueData: "usbip2_ude.inf;usbip2_filter.inf"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: dword; ValueName: "OwnsUsbip"; ValueData: "{code:UsbipOwnership}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideVersion"; ValueData: "1.5.230"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideProductCode"; ValueData: "{#HidHideProductCode}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideOriginalInf"; ValueData: "HidHide.inf"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: dword; ValueName: "OwnsHidHide"; ValueData: "{code:HidHideOwnership}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "CertificatesInstalled"; ValueData: "none (upstream packages use signed drivers)"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\RunOnce"; ValueName: "!ApexSenseBridgeRestoreControllerVisibility"; Flags: uninsdeletevalue

[Icons]
Name: "{group}\ApexSenseBridge — Contrôle et diagnostic"; Filename: "{app}\ApexSenseBridgeControl.exe"; WorkingDir: "{app}"
Name: "{group}\Désinstaller ApexSenseBridge"; Filename: "{uninstallexe}"

[UninstallRun]
; Request the normal neutralize/detach/restore path first. taskkill is retained
; only as a bounded fallback for a hung or damaged engine.
Filename: "{app}\ApexSenseBridge.exe"; Parameters: "stop-active-sessions"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "GracefulStopBridge"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /T /IM ApexSenseBridge.exe"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "StopBridge"
Filename: "{app}\ApexSenseBridge.exe"; Parameters: "restore-controller-visibility"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "RestoreVisibility"
Filename: "{code:GetUsbipUninstaller}"; Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-"; Flags: runhidden waituntilterminated skipifdoesntexist; Check: ShouldRemoveUsbip; RunOnceId: "RemoveUsbip"
Filename: "{sys}\msiexec.exe"; Parameters: "/x {#HidHideProductCode} /quiet /norestart"; Flags: runhidden waituntilterminated; Check: ShouldRemoveHidHide; RunOnceId: "RemoveHidHide"

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\Playnite\Extensions\{#ExtensionId}"
Type: filesandordirs; Name: "{userappdata}\Playnite\ExtensionsData\{#ExtensionId}"
Type: filesandordirs; Name: "{userappdata}\Playnite\ExtensionsData\e41b1737-6753-4b59-bc65-4fdd6a7df7f4"
Type: filesandordirs; Name: "{localappdata}\ApexSenseBridge"

[Code]
var
  UsbipWasPresent: Boolean;
  HidHideWasPresent: Boolean;
  OwnsUsbip: Boolean;
  OwnsHidHide: Boolean;
  UsbipVersionBefore: String;
  HidHideVersionBefore: String;

function UsbipUninstallKey: String;
begin
  Result := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#UsbipProductKey}';
end;

function HidHideUninstallKey: String;
begin
  Result := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#HidHideProductCode}';
end;

function HasCommandLineParameter(const Wanted: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 1 to ParamCount do
  begin
    if CompareText(ParamStr(Index), Wanted) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function InitializeSetup: Boolean;
var
  PreviousOwnership: Cardinal;
begin
  UsbipWasPresent := RegQueryStringValue(
    HKLM64, UsbipUninstallKey, 'DisplayVersion', UsbipVersionBefore);
  HidHideWasPresent := RegQueryStringValue(
    HKLM64, HidHideUninstallKey, 'DisplayVersion', HidHideVersionBefore);

  OwnsUsbip := not UsbipWasPresent;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\ApexSenseBridge',
                        'OwnsUsbip', PreviousOwnership) then
    OwnsUsbip := PreviousOwnership <> 0;
  OwnsHidHide := not HidHideWasPresent;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\ApexSenseBridge',
                        'OwnsHidHide', PreviousOwnership) then
    OwnsHidHide := PreviousOwnership <> 0;

  if UsbipWasPresent and (CompareText(UsbipVersionBefore, '0.9.7.8') = 0) then
  begin
    MsgBox(
      'usbip-win2 0.9.7.8 est installé. Cette version est explicitement refusée ' +
      'en raison de son avertissement officiel de corruption mémoire/BSOD.'#13#10#13#10 +
      'Désinstallez 0.9.7.8, redémarrez Windows, puis relancez ce programme. ' +
      'La version sûre 0.9.7.7 est incluse hors ligne.',
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;
  Result := True;
end;

function NeedUsbip: Boolean;
begin
  Result := (not UsbipWasPresent) or
            (CompareText(UsbipVersionBefore, '0.9.7.7') <> 0);
end;

function NeedHidHide: Boolean;
begin
  Result := (not HidHideWasPresent) or
            (CompareText(HidHideVersionBefore, '1.5.230') <> 0);
end;

function NeedRestart: Boolean;
begin
  Result := NeedUsbip or NeedHidHide;
end;

function UsbipOwnership(Param: String): String;
begin
  if OwnsUsbip then Result := '1' else Result := '0';
end;

function HidHideOwnership(Param: String): String;
begin
  if OwnsHidHide then Result := '1' else Result := '0';
end;

function FullDependencyRemovalRequested: Boolean;
begin
  Result := HasCommandLineParameter('/REMOVEDEPENDENCIES');
end;

function ShouldRemoveUsbip: Boolean;
var
  Ownership: Cardinal;
begin
  Result := FullDependencyRemovalRequested or
            (RegQueryDWordValue(HKLM64, 'SOFTWARE\ApexSenseBridge',
                                'OwnsUsbip', Ownership) and (Ownership <> 0));
end;

function ShouldRemoveHidHide: Boolean;
var
  Ownership: Cardinal;
begin
  Result := FullDependencyRemovalRequested or
            (RegQueryDWordValue(HKLM64, 'SOFTWARE\ApexSenseBridge',
                                'OwnsHidHide', Ownership) and (Ownership <> 0));
end;

function GetUsbipUninstaller(Param: String): String;
var
  Location: String;
begin
  if RegQueryStringValue(HKLM64, UsbipUninstallKey, 'InstallLocation', Location) then
    Result := AddBackslash(Location) + 'unins000.exe'
  else
    Result := ExpandConstant('{pf64}\USBip\unins000.exe');
end;
