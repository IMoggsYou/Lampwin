; SPDX-License-Identifier: GPL-3.0-or-later
; MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

; MacWin installer (Inno Setup 6.5+). Build: cmake --build --preset x64 --target MacWinSetup
; Output: build\installer\MacWinSetup-<version>.exe
; Copyright (C) 2026 IMoggsYou - GPL-3.0-or-later - https://github.com/IMoggsYou/MacWin

; The version comes from the repository's VERSION file (CMake also passes /DAppVersion).
#ifndef AppVersion
  #define VersionFile FileOpen(AddBackslash(SourcePath) + "..\VERSION")
  #define AppVersion Trim(FileRead(VersionFile))
  #expr FileClose(VersionFile)
#endif
#define Bin "..\build\x64\bin"
#define RepoUrl "https://github.com/IMoggsYou/MacWin"

[Setup]
AppId={{6F0B3E9A-2C47-4E7B-9C1D-5A8E2B7F4D10}
AppName=MacWin
AppVersion={#AppVersion}
AppVerName=MacWin {#AppVersion}
AppPublisher=IMoggsYou
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
AppCopyright=Copyright (C) 2026 IMoggsYou
AppComments={cm:AppComments}
DefaultDirName={autopf}\MacWin
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
; ARM64 PCs pass this check (x64 emulation) and are turned away in InitializeSetup with
; a clear message: the hooks cannot reach their native programs.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\build\installer
OutputBaseFilename=MacWinSetup-{#AppVersion}
SetupIconFile=..\assets\MacWin.ico
UninstallDisplayIcon={app}\MacWin.exe
UninstallDisplayName=MacWin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; English unless Windows' display language is one of these.
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
; MacWinHook64.dll is loaded into (nearly) every program: Restart Manager would offer to
; close them all. MacWin is stopped explicitly instead and an in-use DLL is set aside.
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoCompany=IMoggsYou
VersionInfoCopyright=Copyright (C) 2026 IMoggsYou. Licensed under GPL-3.0-or-later.
VersionInfoDescription=MacWin Setup
VersionInfoProductName=MacWin
VersionInfoProductVersion={#AppVersion}

[Languages]
; Inno Setup's own texts first, then MacWin's (installer/languages). English is the
; fallback for every other language.
Name: "english"; MessagesFile: "compiler:Default.isl,languages\English.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl,languages\Polish.isl"
Name: "chinesesimplified"; MessagesFile: "languages\base\ChineseSimplified.isl,languages\ChineseSimplified.isl"
Name: "hindi"; MessagesFile: "languages\base\Hindi.islu,languages\Hindi.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl,languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl,languages\French.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl,languages\Arabic.isl"
Name: "bengali"; MessagesFile: "languages\base\Bengali.islu,languages\Bengali.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl,languages\Russian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl,languages\BrazilianPortuguese.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl,languages\Japanese.isl"

[Tasks]
Name: "autostart"; Description: "{cm:TaskAutostart}"; GroupDescription: "{cm:GroupStartup}"

[Files]
Source: "{#Bin}\MacWin.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWinHook64.dll"; DestDir: "{app}"; Flags: ignoreversion uninsrestartdelete
; 32-bit programs: the helper puts MacWinHook32.dll into them (drawing stays in MacWin.exe).
Source: "{#Bin}\MacWinHook32.dll"; DestDir: "{app}"; Flags: ignoreversion uninsrestartdelete
Source: "{#Bin}\MacWinHelper32.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWin.Settings.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWin.Settings.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWin.Settings.runtimeconfig.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWin.Settings.deps.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\MacWinTestWindow.exe"; DestDir: "{app}"; Flags: ignoreversion
; App icons that come with MacWin; the settings app imports them on its first start.
Source: "{#Bin}\app-icons\*"; DestDir: "{app}\app-icons"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\NOTICE"; DestDir: "{app}"; DestName: "NOTICE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\minhook\LICENSE.txt"; DestDir: "{app}"; DestName: "MinHook-LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MacWin"; Filename: "{app}\MacWin.Settings.exe"; Comment: "{cm:ShortcutComment}"

[Run]
; The scheduled task runs MacWin with administrator rights without a UAC prompt (so
; programs started as administrator are animated too); its logon trigger is the autostart.
Filename: "{app}\MacWin.exe"; Parameters: "--install-task"; Flags: runhidden waituntilterminated; Tasks: autostart; StatusMsg: "{cm:RegisteringTask}"
Filename: "{app}\MacWin.exe"; Parameters: "--install-task --no-logon"; Flags: runhidden waituntilterminated; Tasks: not autostart; StatusMsg: "{cm:RegisteringTask}"
; The choice on the "Automatic updates" page, into the settings of the user who installs
; (not the administrator's). A silent install is an update: the user's choice stays.
Filename: "{app}\MacWin.exe"; Parameters: "--configure {code:UpdateSettings}"; Flags: runhidden waituntilterminated runasoriginaluser; Check: not WizardSilent
; An update (installed silently by MacWin itself) starts the new version right away.
Filename: "{app}\MacWin.exe"; Flags: nowait runasoriginaluser; Check: WizardSilent
Filename: "{app}\MacWin.exe"; Description: "{cm:LaunchMacWin}"; Flags: postinstall nowait runasoriginaluser skipifsilent
Filename: "{app}\MacWin.Settings.exe"; Description: "{cm:OpenSettings}"; Flags: postinstall nowait runasoriginaluser unchecked skipifsilent

[UninstallRun]
Filename: "{app}\MacWin.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitMacWin"
Filename: "{app}\MacWin.exe"; Parameters: "--remove-task"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveTask"

[UninstallDelete]
Type: files; Name: "{app}\*.old"

[Code]
const
  MOVEFILE_DELAY_UNTIL_REBOOT = 4;
  DesktopRuntimeUrl = 'https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x64.exe';
  DesktopRuntimePage = 'https://dotnet.microsoft.com/download/dotnet/10.0';
  DesktopRuntimeFile = 'windowsdesktop-runtime-win-x64.exe';

var
  DownloadPage: TDownloadWizardPage;
  UpdatesPage: TInputOptionWizardPage;
  RuntimeReady: Boolean;

function MoveFileExDelete(lpExistingFileName: String; lpNewFileName: Cardinal; dwFlags: Cardinal): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';

// .NET 10 Desktop Runtime (x64) installed? The settings window needs it; the
// animations themselves (C++) do not.
function HasDesktopRuntime10: Boolean;
var
  Names: TArrayOfString;
  I: Integer;
  FindRec: TFindRec;
begin
  Result := False;
  if RegGetValueNames(HKLM64, 'SOFTWARE\dotnet\Setup\InstalledVersions\x64\sharedfx\Microsoft.WindowsDesktop.App', Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if Pos('10.', Names[I]) = 1 then Result := True;
  if not Result then
  begin
    Result := FindFirst(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.WindowsDesktop.App\10.*'), FindRec);
    if Result then FindClose(FindRec);
  end;
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if IsArm64 then
  begin
    SuppressibleMsgBox(CustomMessage('Arm64NotSupported'), mbCriticalError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;
  RuntimeReady := HasDesktopRuntime10;
end;

// Automatic installs were switched off in an earlier install (settings.json is written
// by MacWin and the settings app, always as "autoInstall": true/false).
function AutoInstallWasOff: Boolean;
var
  Json: AnsiString;
  Text: String;
begin
  Result := False;
  if LoadStringFromFile(ExpandConstant('{userappdata}\MacWin\settings.json'), Json) then
  begin
    Text := String(Json);
    StringChangeEx(Text, ' ', '', True);
    Result := Pos('"autoInstall":false', Text) > 0;
  end;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), CustomMessage('DotNetDownloading'), nil);
  // After the tasks: one check box, on unless this user turned it off before.
  UpdatesPage := CreateInputOptionPage(wpSelectTasks, CustomMessage('UpdatesTitle'), CustomMessage('UpdatesSubtitle'),
                                       CustomMessage('UpdatesText'), False, False);
  UpdatesPage.Add(CustomMessage('UpdatesAuto'));
  UpdatesPage.Values[0] := not AutoInstallWasOff;
end;

// --configure arguments for the "Automatic updates" page. Off keeps the daily check: the
// settings window then offers each new version.
function UpdateSettings(Param: String): String;
begin
  if UpdatesPage.Values[0] then
    Result := 'updateCheck=1 updateAutoInstall=1'
  else
    Result := 'updateAutoInstall=0';
end;

// Runs the Microsoft runtime installer quietly. 3010 = done, restart pending; 1638 =
// a newer one is already there.
procedure InstallDesktopRuntime(const Installer: String);
var
  ResultCode: Integer;
begin
  WizardForm.StatusLabel.Caption := CustomMessage('DotNetInstalling');
  if Exec(Installer, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and
     ((ResultCode = 0) or (ResultCode = 3010) or (ResultCode = 1638)) then
    RuntimeReady := True
  else
    SuppressibleMsgBox(FmtMessage(CustomMessage('DotNetFailed'), [IntToStr(ResultCode), DesktopRuntimePage]),
                       mbInformation, MB_OK, IDOK);
end;

// Interactive install: download the runtime (with a progress page) after "Install".
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and not RuntimeReady then
  begin
    DownloadPage.Clear;
    DownloadPage.Add(DesktopRuntimeUrl, DesktopRuntimeFile, '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
      except
        // Offline or blocked: MacWin installs anyway, the message says where to get it.
        if not DownloadPage.AbortedByUser then
          SuppressibleMsgBox(FmtMessage(CustomMessage('DotNetFailed'), [GetExceptionMessage, DesktopRuntimePage]),
                             mbInformation, MB_OK, IDOK);
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

// Stops a running MacWin (any copy, also one started from elsewhere) and closes the
// settings window, so their files can be replaced.
procedure StopMacWin;
var
  ResultCode: Integer;
begin
  ExtractTemporaryFile('MacWin.exe');
  Exec(ExpandConstant('{tmp}\MacWin.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM MacWin.Settings.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  // The helper leaves with MacWin; this only covers one orphaned by an older version.
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM MacWinHelper32.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// Programs keep MacWinHook64.dll mapped until they exit. A mapped file can't be
// overwritten or deleted, but it can be renamed: move it aside and delete it at reboot.
procedure SetAsideInUse(const FileName: String);
var
  Aside: String;
begin
  if FileExists(FileName) and not DeleteFile(FileName) then
  begin
    Aside := FileName + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old';
    if RenameFile(FileName, Aside) then
      MoveFileExDelete(Aside, 0, MOVEFILE_DELAY_UNTIL_REBOOT);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  // Silent installs (MacWin's own updates) skip the wizard pages: download here.
  if WizardSilent and not RuntimeReady then
  try
    DownloadTemporaryFile(DesktopRuntimeUrl, DesktopRuntimeFile, '', nil);
  except
    Log('.NET runtime download failed: ' + GetExceptionMessage);
  end;
  if not RuntimeReady and FileExists(ExpandConstant('{tmp}\' + DesktopRuntimeFile)) then
    InstallDesktopRuntime(ExpandConstant('{tmp}\' + DesktopRuntimeFile));
  StopMacWin;
  SetAsideInUse(ExpandConstant('{app}\MacWinHook64.dll'));
  SetAsideInUse(ExpandConstant('{app}\MacWinHook32.dll'));
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM MacWin.Settings.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    SetAsideInUse(ExpandConstant('{app}\MacWinHook64.dll'));
    SetAsideInUse(ExpandConstant('{app}\MacWinHook32.dll'));
    // A development build may have used the per-user Run key for autostart.
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'MacWin');
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    // Settings and logs stay unless the user says otherwise (a reinstall keeps them).
    if not UninstallSilent and
       (MsgBox(CustomMessage('RemoveSettings'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    begin
      DelTree(ExpandConstant('{userappdata}\MacWin'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\MacWin'), True, True, True);
    end;
  end;
end;
