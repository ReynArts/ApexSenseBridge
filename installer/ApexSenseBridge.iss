#define AppName "ApexSenseBridge"
#define AppVersion "0.4.0"
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
UninstallDisplayIcon={app}\Resources\app.ico
SetupIconFile=..\assets\app.ico
LicenseFile=..\LICENSE
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.InstallingUsbip=Installation de usbip-win2 0.9.7.7…
english.InstallingUsbip=Installing usbip-win2 0.9.7.7…
french.InstallingHidHide=Installation de HidHide 1.5.230…
english.InstallingHidHide=Installing HidHide 1.5.230…

[Tasks]
Name: "startwithwindows"; Description: "Démarrer ApexSenseBridge Tray au démarrage de Windows (Recommandé si vous n'utilisez pas Playnite)"; GroupDescription: "Options de démarrage :"
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau pour ApexSenseBridge Tray"; GroupDescription: "Raccourcis :"; Flags: unchecked

[Files]
Source: "..\build-win\Release\ApexSenseBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\ApexSenseBridgeControl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\ApexSenseBridgeTray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\ApexSenseBridgeTray.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ApexSenseBridgeTray\Resources\*"; DestDir: "{app}\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\data\supported_games.json"; DestDir: "{app}\Data"; Flags: ignoreversion
Source: "..\build-win\Release\viiper.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build-win\Release\VIIPER-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\build-win\Release\VIIPER-SOURCE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\build-win\Release\VIIPER-v0.7.0-asb.patch"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\Licenses"; DestName: "ApexSenseBridge-LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "driver-manifest.json"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\third_party\prerequisites\USBIP-WIN2-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion
Source: "..\third_party\prerequisites\HIDHIDE-LICENSE.txt"; DestDir: "{app}\Licenses"; Flags: ignoreversion

; The two official prerequisite installers are compressed inside the setup and
; extracted only when the corresponding pinned version must be installed.
Source: "..\third_party\prerequisites\USBip-0.9.7.7-x64.exe"; DestDir: "{tmp}\ApexSenseBridge"; Flags: deleteafterinstall
Source: "..\third_party\prerequisites\HidHide_1.5.230_x64.exe"; DestDir: "{tmp}\ApexSenseBridge"; Flags: deleteafterinstall
Source: "install-usbip.ps1"; DestDir: "{tmp}\ApexSenseBridge"; Flags: deleteafterinstall

; Install the unpacked extension directly. Playnite keeps its profiles under
; ExtensionsData, so upgrades migrate them without requiring a .pext prompt.
Source: "..\playnite\ApexSenseBridge\bin\Release\ApexSenseBridge.dll"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\extension.yaml"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\icon.png"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}"; Flags: ignoreversion
Source: "..\playnite\ApexSenseBridge\bin\Release\Localization\*"; DestDir: "{userappdata}\Playnite\Extensions\{#ExtensionId}\Localization"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; The upstream USBip setup always launches the previous package's uninstaller
; when its AppId is already registered. We reject that unsafe upgrade path in
; InitializeSetup and use a bounded wrapper only for a genuinely fresh install.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{tmp}\ApexSenseBridge\install-usbip.ps1"" -InstallerPath ""{tmp}\ApexSenseBridge\USBip-0.9.7.7-x64.exe"" -LogPath ""{commonappdata}\ApexSenseBridge\usbip-install.log"""; StatusMsg: "{cm:InstallingUsbip}"; Flags: runhidden waituntilterminated; Check: NeedUsbip; AfterInstall: VerifyUsbipInstall
Filename: "{tmp}\ApexSenseBridge\HidHide_1.5.230_x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "{cm:InstallingHidHide}"; Flags: runhidden waituntilterminated; Check: NeedHidHide
Filename: "{app}\ApexSenseBridgeTray.exe"; Description: "Lancer ApexSenseBridge Tray (Barre des tâches)"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "ExecutablePath"; ValueData: "{app}\ApexSenseBridge.exe"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "ControlPanelPath"; ValueData: "{app}\ApexSenseBridgeControl.exe"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "TrayExecutablePath"; ValueData: "{app}\ApexSenseBridgeTray.exe"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UninstallExecutable"; ValueData: "{uninstallexe}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipVersion"; ValueData: "{code:UsbipInstalledVersion}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipProductCode"; ValueData: "{#UsbipProductKey}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "UsbipOriginalInf"; ValueData: "usbip2_ude.inf;usbip2_filter.inf"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: dword; ValueName: "OwnsUsbip"; ValueData: "{code:UsbipOwnership}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideVersion"; ValueData: "1.5.230"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideProductCode"; ValueData: "{#HidHideProductCode}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "HidHideOriginalInf"; ValueData: "HidHide.inf"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: dword; ValueName: "OwnsHidHide"; ValueData: "{code:HidHideOwnership}"; Flags: uninsdeletevalue
Root: HKLM64; Subkey: "SOFTWARE\ApexSenseBridge"; ValueType: string; ValueName: "CertificatesInstalled"; ValueData: "none (upstream packages use signed drivers)"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "ApexSenseBridgeTray"; ValueType: string; ValueData: """{app}\ApexSenseBridgeTray.exe"""; Flags: uninsdeletevalue; Tasks: startwithwindows
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\RunOnce"; ValueName: "!ApexSenseBridgeRestoreControllerVisibility"; Flags: uninsdeletevalue

[Icons]
Name: "{group}\ApexSenseBridge Tray (Barre des tâches)"; Filename: "{app}\ApexSenseBridgeTray.exe"; WorkingDir: "{app}"
Name: "{group}\ApexSenseBridge — Contrôle et diagnostic"; Filename: "{app}\ApexSenseBridgeControl.exe"; WorkingDir: "{app}"
Name: "{group}\Désinstaller ApexSenseBridge"; Filename: "{uninstallexe}"
Name: "{commondesktop}\ApexSenseBridge Tray"; Filename: "{app}\ApexSenseBridgeTray.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[UninstallRun]
; Request the normal neutralize/detach/restore path first. taskkill is retained
; only as a bounded fallback for a hung or damaged engine.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /T /IM ApexSenseBridgeTray.exe"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "StopTray"
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
  UsbipUninstallEntryPresent: Boolean;
  UsbipUdeServicePresent: Boolean;
  UsbipFilterServicePresent: Boolean;
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

function UsbipMessage(const FrenchMessage, EnglishMessage: String): String;
begin
  if CompareText(ActiveLanguage, 'french') = 0 then
    Result := FrenchMessage
  else
    Result := EnglishMessage;
end;

function IsSupportedUsbipVersion(const Version: String): Boolean;
begin
  Result := (CompareText(Version, '0.9.7.5') = 0) or
            (CompareText(Version, '0.9.7.6') = 0) or
            (CompareText(Version, '0.9.7.7') = 0);
end;

function InitializeSetup: Boolean;
var
  PreviousOwnership: Cardinal;
begin
  UsbipUninstallEntryPresent := RegKeyExists(HKLM64, UsbipUninstallKey);
  UsbipUdeServicePresent := RegKeyExists(
    HKLM, 'SYSTEM\CurrentControlSet\Services\usbip2_ude');
  UsbipFilterServicePresent := RegKeyExists(
    HKLM, 'SYSTEM\CurrentControlSet\Services\usbip2_filter');
  UsbipWasPresent := UsbipUninstallEntryPresent or
                     UsbipUdeServicePresent or UsbipFilterServicePresent;

  if UsbipUninstallEntryPresent then
    RegQueryStringValue(
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

  UsbipVersionBefore := Trim(UsbipVersionBefore);
  if UsbipUninstallEntryPresent and (UsbipVersionBefore = '') then
  begin
    MsgBox(
      UsbipMessage(
        'Une installation USBip endommagée ou incomplète a été détectée.'#13#10#13#10 +
        'Pour éviter le blocage connu de son désinstalleur imbriqué, ApexSenseBridge ' +
        'ne tentera pas de la remplacer automatiquement. Désinstallez USBip depuis ' +
        'les Paramètres Windows, redémarrez, puis relancez ce programme.',
        'A damaged or incomplete USBip installation was detected.'#13#10#13#10 +
        'To avoid the known nested-uninstaller hang, ApexSenseBridge will not ' +
        'replace it automatically. Uninstall USBip in Windows Settings, restart ' +
        'Windows, then run this setup again.'),
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  if UsbipUninstallEntryPresent and
     (CompareText(UsbipVersionBefore, '0.9.7.8') = 0) then
  begin
    MsgBox(
      UsbipMessage(
        'usbip-win2 0.9.7.8 est installé. Cette version est explicitement refusée ' +
        'en raison de son avertissement officiel de corruption mémoire/BSOD.'#13#10#13#10 +
        'Désinstallez 0.9.7.8, redémarrez Windows, puis relancez ce programme. ' +
        'La version sûre 0.9.7.7 est incluse hors ligne.',
        'usbip-win2 0.9.7.8 is installed. This version is explicitly refused ' +
        'because of its official memory-corruption/BSOD warning.'#13#10#13#10 +
        'Uninstall 0.9.7.8, restart Windows, then run this setup again. ' +
        'The safer 0.9.7.7 release is included offline.'),
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  if UsbipUninstallEntryPresent and
     (not IsSupportedUsbipVersion(UsbipVersionBefore)) then
  begin
    MsgBox(
      UsbipMessage(
        'USBip ' + UsbipVersionBefore + ' est déjà installé.'#13#10#13#10 +
        'Son installateur officiel tente de désinstaller automatiquement toute ' +
        'autre version et peut rester bloqué sur « Uninstalling USBip… ».'#13#10#13#10 +
        'Désinstallez d''abord USBip depuis les Paramètres Windows, redémarrez, ' +
        'puis relancez ApexSenseBridge. La version 0.9.7.7 sûre est incluse hors ligne.',
        'USBip ' + UsbipVersionBefore + ' is already installed.'#13#10#13#10 +
        'Its official setup tries to uninstall every other version automatically ' +
        'and can hang on "Uninstalling USBip...".'#13#10#13#10 +
        'Uninstall USBip in Windows Settings first, restart Windows, then run ' +
        'ApexSenseBridge setup again. The supported 0.9.7.7 release is included offline.'),
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  if UsbipUninstallEntryPresent and
     ((not UsbipUdeServicePresent) or (not UsbipFilterServicePresent)) then
  begin
    MsgBox(
      UsbipMessage(
        'USBip ' + UsbipVersionBefore + ' est enregistré, mais ses pilotes sont incomplets.'#13#10#13#10 +
        'Désinstallez USBip depuis les Paramètres Windows, redémarrez, puis ' +
        'relancez ApexSenseBridge pour effectuer une installation propre.',
        'USBip ' + UsbipVersionBefore + ' is registered, but its drivers are incomplete.'#13#10#13#10 +
        'Uninstall USBip in Windows Settings, restart Windows, then run ' +
        'ApexSenseBridge setup again for a clean installation.'),
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;

  if (not UsbipUninstallEntryPresent) and
     (UsbipUdeServicePresent or UsbipFilterServicePresent) then
  begin
    MsgBox(
      UsbipMessage(
        'Des restes de pilotes USBip ont été détectés sans installation ' +
        'enregistrée.'#13#10#13#10 +
        'Nettoyez ou réparez USBip, redémarrez Windows, puis relancez ' +
        'ApexSenseBridge. Cette protection évite une mise à niveau de pilote ambiguë.',
        'USBip driver remnants were found without a registered installation.'#13#10#13#10 +
        'Clean up or repair USBip, restart Windows, then run ApexSenseBridge ' +
        'setup again. This protection avoids an ambiguous driver upgrade.'),
      mbCriticalError, MB_OK);
    Result := False;
    Exit;
  end;
  Result := True;
end;

function NeedUsbip: Boolean;
begin
  Result := not UsbipUninstallEntryPresent;
end;

function UsbipInstalledVersion(Param: String): String;
begin
  if UsbipUninstallEntryPresent then
    Result := UsbipVersionBefore
  else
    Result := '0.9.7.7';
end;

procedure VerifyUsbipInstall;
var
  InstalledVersion: String;
begin
  if (not RegQueryStringValue(
        HKLM64, UsbipUninstallKey, 'DisplayVersion', InstalledVersion)) or
     (CompareText(Trim(InstalledVersion), '0.9.7.7') <> 0) or
     (not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\usbip2_ude')) or
     (not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\usbip2_filter')) then
  begin
    RaiseException(
      UsbipMessage(
        'L''installation de usbip-win2 0.9.7.7 n''a pas abouti. ' +
        'Consultez le journal dans ' +
        ExpandConstant('{commonappdata}\ApexSenseBridge\usbip-install.log') +
        ', redémarrez Windows, puis relancez l''installation.',
        'usbip-win2 0.9.7.7 did not install successfully. See the log at ' +
        ExpandConstant('{commonappdata}\ApexSenseBridge\usbip-install.log') +
        ', restart Windows, then run setup again.'));
  end;
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
