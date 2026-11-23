; Inno Setup script for Just Better TaskBar.
; Build src\JustBetterTaskBar.exe first, then compile this file with ISCC.exe.
; Installs per user (no admin rights), so the app can keep dock.ini next to its executable.

#define AppName "Just Better TaskBar"
#define AppVersion "1.0.0"
#define AppExe "JustBetterTaskBar.exe"
#define AppUrl "https://github.com/Moryel/JustBetterTaskBar"

[Setup]
AppId={{CF23F21B-2949-43D6-9CEE-0C80440E3747}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Yüksel
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
DefaultDirName={localappdata}\Programs\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
LicenseFile=..\LICENSE
SetupIconFile=..\src\JustBetterTaskBar.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\dist
OutputBaseFilename=JustBetterTaskBar-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Tasks]
Name: "autostart"; Description: "Start {#AppName} when I sign in to Windows"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\src\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{userprograms}\{#AppName} Settings"; Filename: "{app}\{#AppExe}"; Parameters: "--settings"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; same value the app's own "Start with Windows" switch writes, so the two stay in sync
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "JustBetterTaskBar"; ValueData: """{app}\{#AppExe}"""; Tasks: autostart; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\JustBetterTaskBar"; Flags: uninsdeletekey dontcreatekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\dock.ini"

[Code]
procedure QuitRunningDock();
var
  Exe: String;
  Code: Integer;
begin
  Exe := ExpandConstant('{app}\{#AppExe}');
  if FileExists(Exe) then
  begin
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(1500);
  end;
end;

// An update replaces the running executable, so the old dock is closed the normal way first.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  QuitRunningDock();
  Result := '';
end;

// Quitting normally brings the Windows taskbar back before the files are removed.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    QuitRunningDock();
end;
