; Inno Setup script for "No Uninstall Icon".
; Build with:  ISCC.exe installer\setup.iss
; The installer must run elevated because the application writes the hidden
; attribute on shortcuts that live under %ProgramData%.

#define AppName "我不需要卸载图标"
#define AppVersion "0.1.0"
#define AppExeName "NoUninstallIcon.exe"
#define AppId "{{8E4B7C2A-3F1D-4A6B-9C5E-2D7F1A8B4C60}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=王云正
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

; The cleanup step runs from [Code] rather than [UninstallRun] so that a failure
; is visible: a silent failure would delete the program with shortcuts still
; hidden and a sign-in task still installed.
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    if not FileExists(ExpandConstant('{app}\{#AppExeName}')) then
      Exit;
    if not Exec(ExpandConstant('{app}\{#AppExeName}'), '--cleanup', '', SW_HIDE,
                ewWaitUntilTerminated, ResultCode) then
    begin
      Log('Cleanup could not be started.');
      if not UninstallSilent then
        MsgBox('The cleanup step could not be started. Some Start menu entries may stay hidden.',
               mbError, MB_OK);
    end
    else if ResultCode <> 0 then
    begin
      Log('Cleanup failed with code ' + IntToStr(ResultCode) + '.');
      // A silent uninstall must never wait on a dialog, and a message box would
      // be lost anyway; the log and the exit code are all we can offer there.
      if not UninstallSilent then
        MsgBox('The cleanup step reported a problem (code ' + IntToStr(ResultCode) +
               '). Some Start menu entries may still be hidden.', mbError, MB_OK);
    end;
  end;
end;
