; MacWin installer (Inno Setup 6). Build: cmake --build --preset x64 --target MacWinSetup
; Output: build\installer\MacWinSetup.exe

#define AppVersion "0.1.0"
#define Bin "..\build\x64\bin"

[Setup]
AppId={{6F0B3E9A-2C47-4E7B-9C1D-5A8E2B7F4D10}
AppName=MacWin
AppVersion={#AppVersion}
AppVerName=MacWin {#AppVersion}
AppPublisher=MacWin
AppComments=Animacje okien w stylu macOS dla Windows 11
DefaultDirName={autopf}\MacWin
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\build\installer
OutputBaseFilename=MacWinSetup
SetupIconFile=..\assets\MacWin.ico
UninstallDisplayIcon={app}\MacWin.exe
UninstallDisplayName=MacWin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; MacWinHook64.dll is loaded into (nearly) every program: Restart Manager would offer to
; close them all. MacWin is stopped explicitly instead and an in-use DLL is set aside.
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription=MacWin – instalator

[Languages]
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"

[Tasks]
Name: "autostart"; Description: "Uruchamiaj MacWin przy logowaniu"; GroupDescription: "Start:"
Name: "everywhere"; Description: "Animuj wszystkie programy (wyłącz tryb testowy)"; GroupDescription: "Animacje:"

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
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\minhook\LICENSE.txt"; DestDir: "{app}"; DestName: "MinHook-LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MacWin"; Filename: "{app}\MacWin.Settings.exe"; Comment: "Ustawienia animacji okien"

[Run]
; The scheduled task runs MacWin with administrator rights without a UAC prompt (so
; programs started as administrator are animated too); its logon trigger is the autostart.
Filename: "{app}\MacWin.exe"; Parameters: "--install-task"; Flags: runhidden waituntilterminated; Tasks: autostart; StatusMsg: "Rejestrowanie zadania MacWin…"
Filename: "{app}\MacWin.exe"; Parameters: "--install-task --no-logon"; Flags: runhidden waituntilterminated; Tasks: not autostart; StatusMsg: "Rejestrowanie zadania MacWin…"
Filename: "{app}\MacWin.exe"; Parameters: "--configure devMode=0"; Flags: runhidden waituntilterminated runasoriginaluser; Tasks: everywhere
Filename: "{app}\MacWin.exe"; Description: "Uruchom MacWin"; Flags: postinstall nowait runasoriginaluser
Filename: "{app}\MacWin.Settings.exe"; Description: "Otwórz ustawienia"; Flags: postinstall nowait runasoriginaluser unchecked

[UninstallRun]
Filename: "{app}\MacWin.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitMacWin"
Filename: "{app}\MacWin.exe"; Parameters: "--remove-task"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveTask"

[UninstallDelete]
Type: files; Name: "{app}\*.old"

[Messages]
polish.WelcomeLabel2=Zainstaluje [name/ver] – animacje okien w stylu macOS: okna wpadają do paska zadań jak dżin i wylatują z niego z powrotem.%n%nMacWin działa w tle, z ikoną w zasobniku.

[Code]
const
  MOVEFILE_DELAY_UNTIL_REBOOT = 4;
  DesktopRuntimeUrl = 'https://dotnet.microsoft.com/download/dotnet/10.0';

function MoveFileExDelete(lpExistingFileName: String; lpNewFileName: Cardinal; dwFlags: Cardinal): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';

function HasDesktopRuntime10: Boolean;
var
  FindRec: TFindRec;
begin
  Result := FindFirst(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.WindowsDesktop.App\10.*'), FindRec);
  if Result then FindClose(FindRec);
end;

function InitializeSetup: Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;
  if not HasDesktopRuntime10 then
  begin
    if MsgBox('Okno ustawień MacWin potrzebuje .NET 10 Desktop Runtime, którego nie ma na tym komputerze.' + #13#10#13#10 +
              'Otworzyć stronę pobierania? (Instalacja MacWin będzie kontynuowana – animacje działają i bez tego.)',
              mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', DesktopRuntimeUrl, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
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
  StopMacWin;
  SetAsideInUse(ExpandConstant('{app}\MacWinHook64.dll'));
  SetAsideInUse(ExpandConstant('{app}\MacWinHook32.dll'));
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    SetAsideInUse(ExpandConstant('{app}\MacWinHook64.dll'));
    SetAsideInUse(ExpandConstant('{app}\MacWinHook32.dll'));
    // A development build may have used the per-user Run key for autostart.
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'MacWin');
  end;
end;
