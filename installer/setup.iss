; Inno Setup script for "No Uninstall Icon".
; Build with:  ISCC.exe installer\setup.iss
; The installer must run elevated because the application writes the hidden
; attribute on shortcuts that live under %ProgramData%.

#define AppName "我不需要卸载图标"
#define AppVersion "1.0.0"
#define AppExeName "NoUninstallIcon.exe"
#define AppId "{{8E4B7C2A-3F1D-4A6B-9C5E-2D7F1A8B4C60}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=NoUninstallIcon
DefaultDirName={autopf}\NoUninstallIcon
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=NoUninstallIcon-Setup-{#AppVersion}
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Always write "%TEMP%\Setup Log *.txt" so a failed install can be diagnosed.
SetupLogging=yes
; /CURRENTUSER installs per user without elevation, which is a useful fallback
; when an elevated install cannot write to %TEMP%.
PrivilegesRequiredOverridesAllowed=commandline
CloseApplications=yes
RestartApplications=no
AllowNoIcons=yes
MinVersion=10.0.17763

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesetraditional"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; Ask the application to remove its sign-in task and to give every managed
; shortcut its original attributes back before the files are deleted.
[UninstallRun]
Filename: "{app}\{#AppExeName}"; Parameters: "--cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "Cleanup"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;
