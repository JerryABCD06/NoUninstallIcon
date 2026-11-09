; Inno Setup script for No Uninstall Icon.
; Build with:  powershell -File tools\package.ps1   (or ISCC.exe installer\setup.iss)
;
; The installer must run elevated because the application writes the hidden
; attribute on shortcuts that live under %ProgramData%.
;
; The product name is localized: [Setup] AppName is a custom message, expanded in
; whichever language the user picks in the language dialog, and written into the
; Start menu, the icons and the Apps & Features entry at install time. The
; directory name stays an unlocalized identifier so an upgrade always lands in
; the same place, and AppId is a fixed GUID so the installer is recognized as the
; same product whatever language was chosen last time.

#define AppVersion "0.1.1"
#define AppExeName "NoUninstallIcon.exe"
#define AppId "{{8E4B7C2A-3F1D-4A6B-9C5E-2D7F1A8B4C60}"

[Setup]
AppId={#AppId}
AppName={cm:AppName}
AppVersion={#AppVersion}
AppVerName={cm:AppName} {#AppVersion}
AppPublisher=王云正
AppPublisherURL=https://github.com/JerryABCD06/
AppSupportURL=https://github.com/JerryABCD06/
AppUpdatesURL=https://github.com/JerryABCD06/
VersionInfoProductName=No Uninstall Icon
VersionInfoDescription=No Uninstall Icon Setup
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\NoUninstallIcon
DefaultGroupName={cm:AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=NoUninstallIcon-Setup-{#AppVersion}
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
; "dynamic" follows the Windows light/dark setting; light and dark force one.
WizardStyle=modern dynamic
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
; Always write "%TEMP%\Setup Log *.txt" so a failed install can be diagnosed.
SetupLogging=yes
; /CURRENTUSER installs per user without elevation, which is a useful fallback
; when an elevated install cannot write to %TEMP%.
PrivilegesRequiredOverridesAllowed=commandline
; Restart Manager is turned off: its dialog offers "do not close the applications",
; which would let an installation continue against a locked executable and fail at
; copy time. [Code] stops the app itself and, when that is not possible, asks before
; force-closing or cancels the installation.
CloseApplications=no
RestartApplications=no
AllowNoIcons=yes
MinVersion=10.0.17763

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh_Hans"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "zh_Hant"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "pt_BR"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

; The localized product name, taken from each language's app.title so the
; installer and the program agree. The un-prefixed entries are the fallbacks.
[CustomMessages]
AppName=No Uninstall Icon
en.AppName=No Uninstall Icon
zh_Hans.AppName=我不需要卸载图标
zh_Hant.AppName=我不需要卸載圖標
ja.AppName=アンインストールアイコンは不要
de.AppName=Keine Deinstallations-Symbole
fr.AppName=Pas besoin d'icône de désinstallation
es.AppName=No necesito el icono de desinstalación
ru.AppName=Мне не нужна иконка удаления
ko.AppName=제거 아이콘이 필요 없습니다
pt_BR.AppName=Não preciso do ícone de desinstalação
it.AppName=Non mi serve l'icona di disinstallazione

CleanupNotStarted=The cleanup step could not be started. Some Start menu entries may stay hidden.
en.CleanupNotStarted=The cleanup step could not be started. Some Start menu entries may stay hidden.
zh_Hans.CleanupNotStarted=清理步骤无法启动，部分开始菜单项可能仍处于隐藏状态。
zh_Hant.CleanupNotStarted=清理步驟無法啟動，部分開始功能表項目可能仍為隱藏。
ja.CleanupNotStarted=クリーンアップを開始できませんでした。一部のスタート メニュー項目は非表示のままになる可能性があります。
de.CleanupNotStarted=Der Aufräumschritt konnte nicht gestartet werden. Einige Startmenüeinträge bleiben möglicherweise ausgeblendet.
fr.CleanupNotStarted=L'étape de nettoyage n'a pas pu démarrer. Certaines entrées du menu Démarrer risquent de rester masquées.
es.CleanupNotStarted=No se pudo iniciar la limpieza. Es posible que algunas entradas del menú Inicio sigan ocultas.
ru.CleanupNotStarted=Не удалось запустить очистку. Некоторые элементы меню «Пуск» могут остаться скрытыми.
ko.CleanupNotStarted=정리를 시작할 수 없습니다. 일부 시작 메뉴 항목이 계속 숨겨져 있을 수 있습니다.
pt_BR.CleanupNotStarted=Não foi possível iniciar a limpeza. Alguns itens do menu Iniciar podem continuar ocultos.
it.CleanupNotStarted=Impossibile avviare la pulizia. Alcune voci del menu Start potrebbero restare nascoste.

CleanupFailed=The cleanup step reported a problem (code %1). Some Start menu entries may still be hidden.
en.CleanupFailed=The cleanup step reported a problem (code %1). Some Start menu entries may still be hidden.
zh_Hans.CleanupFailed=清理步骤报告了问题（代码 %1），部分开始菜单项可能仍处于隐藏状态。
zh_Hant.CleanupFailed=清理步驟回報了問題（代碼 %1），部分開始功能表項目可能仍為隱藏。
ja.CleanupFailed=クリーンアップで問題が報告されました (コード %1)。一部のスタート メニュー項目は非表示のままになる可能性があります。
de.CleanupFailed=Der Aufräumschritt hat ein Problem gemeldet (Code %1). Einige Startmenüeinträge bleiben möglicherweise ausgeblendet.
fr.CleanupFailed=L'étape de nettoyage a signalé un problème (code %1). Certaines entrées du menu Démarrer risquent de rester masquées.
es.CleanupFailed=La limpieza informó de un problema (código %1). Es posible que algunas entradas del menú Inicio sigan ocultas.
ru.CleanupFailed=Очистка сообщила о проблеме (код %1). Некоторые элементы меню «Пуск» могут остаться скрытыми.
ko.CleanupFailed=정리 중 문제가 보고되었습니다(코드 %1). 일부 시작 메뉴 항목이 계속 숨겨져 있을 수 있습니다.
pt_BR.CleanupFailed=A limpeza relatou um problema (código %1). Alguns itens do menu Iniciar podem continuar ocultos.
it.CleanupFailed=La pulizia ha segnalato un problema (codice %1). Alcune voci del menu Start potrebbero restare nascoste.

ForceClosePrompt=%1 cannot be shut down safely. Force it to close?
en.ForceClosePrompt=%1 cannot be shut down safely. Force it to close?
zh_Hans.ForceClosePrompt=无法安全退出 %1，是否强制关闭？
zh_Hant.ForceClosePrompt=無法安全退出 %1，是否強制關閉？
ja.ForceClosePrompt=%1 を安全に終了できません。強制終了しますか？
de.ForceClosePrompt=%1 kann nicht sauber beendet werden. Erzwungen beenden?
fr.ForceClosePrompt=Impossible d'arrêter %1 proprement. Forcer la fermeture ?
es.ForceClosePrompt=No se puede cerrar %1 de forma segura. ¿Forzar el cierre?
ru.ForceClosePrompt=Не удалось безопасно закрыть %1. Принудительно завершить?
ko.ForceClosePrompt=%1을(를) 안전하게 종료할 수 없습니다. 강제로 종료할까요?
pt_BR.ForceClosePrompt=Não foi possível encerrar %1 com segurança. Forçar o encerramento?
it.ForceClosePrompt=Impossibile chiudere %1 correttamente. Forzarne la chiusura?

StillRunning=%1 is still running. Close it, then click Retry.
en.StillRunning=%1 is still running. Close it, then click Retry.
zh_Hans.StillRunning=%1 仍在运行。请关闭它，然后点击“重试”。
zh_Hant.StillRunning=%1 仍在執行。請關閉它，然後按一下「重試」。
ja.StillRunning=%1 はまだ実行中です。終了してから［再試行］をクリックしてください。
de.StillRunning=%1 läuft noch. Schließen Sie es und klicken Sie dann auf „Wiederholen“.
fr.StillRunning=%1 est toujours en cours d'exécution. Fermez-le, puis cliquez sur « Réessayer ».
es.StillRunning=%1 sigue en ejecución. Ciérrelo y haga clic en «Reintentar».
ru.StillRunning=%1 всё ещё выполняется. Закройте его и нажмите «Повторить».
ko.StillRunning=%1이(가) 아직 실행 중입니다. 종료한 후 [다시 시도]를 클릭하세요.
pt_BR.StillRunning=%1 ainda está em execução. Feche-o e clique em "Tentar novamente".
it.StillRunning=%1 è ancora in esecuzione. Chiudilo e fai clic su «Riprova».
en.ForceClosePrompt=%1 cannot be shut down safely. Force it to close?
en.ForceClosePrompt=%1 cannot be shut down safely. Force it to close?
zh_Hans.ForceClosePrompt=无法安全退出 %1，是否强制关闭？
zh_Hant.ForceClosePrompt=無法安全退出 %1，是否強制關閉？
ja.ForceClosePrompt=%1 を安全に終了できません。強制終了しますか？
de.ForceClosePrompt=%1 kann nicht sauber beendet werden. Erzwungen beenden?
fr.ForceClosePrompt=Impossible d'arrêter %1 proprement. Forcer la fermeture ?
es.ForceClosePrompt=No se puede cerrar %1 de forma segura. ¿Forzar el cierre?
ru.ForceClosePrompt=Не удалось безопасно закрыть %1. Принудительно завершить?
ko.ForceClosePrompt=%1을(를) 안전하게 종료할 수 없습니다. 강제로 종료할까요?
pt_BR.ForceClosePrompt=Não foi possível encerrar %1 com segurança. Forçar o encerramento?
it.ForceClosePrompt=Impossibile chiudere %1 correttamente. Forzarne la chiusura?

StillRunning=%1 is still running. Close it, then click Retry.
en.StillRunning=%1 is still running. Close it, then click Retry.
zh_Hans.StillRunning=%1 仍在运行。请关闭它，然后点击“重试”。
zh_Hant.StillRunning=%1 仍在執行。請關閉它，然後按一下「重試」。
ja.StillRunning=%1 はまだ実行中です。終了してから［再試行］をクリックしてください。
de.StillRunning=%1 läuft noch. Schließen Sie es und klicken Sie dann auf „Wiederholen“.
fr.StillRunning=%1 est toujours en cours d'exécution. Fermez-le, puis cliquez sur « Réessayer ».
es.StillRunning=%1 sigue en ejecución. Ciérrelo y haga clic en «Reintentar».
ru.StillRunning=%1 всё ещё выполняется. Закройте его и нажмите «Повторить».
ko.StillRunning=%1이(가) 아직 실행 중입니다. 종료한 후 [다시 시도]를 클릭하세요.
pt_BR.StillRunning=%1 ainda está em execução. Feche-o e clique em "Tentar novamente".
it.StillRunning=%1 è ancora in esecuzione. Chiudilo e fai clic su «Riprova».
zh_Hans.ForceClosePrompt=无法安全退出 %1，是否强制关闭？
zh_Hant.ForceClosePrompt=無法安全退出 %1，是否強制關閉？
ja.ForceClosePrompt=%1 を安全に終了できません。強制終了しますか？
de.ForceClosePrompt=%1 kann nicht sauber beendet werden. Erzwungen beenden?
fr.ForceClosePrompt=Impossible d'arrêter %1 proprement. Forcer la fermeture ?
es.ForceClosePrompt=No se puede cerrar %1 de forma segura. ¿Forzar el cierre?
ru.ForceClosePrompt=Не удалось безопасно закрыть %1. Принудительно завершить?
ko.ForceClosePrompt=%1을(를) 안전하게 종료할 수 없습니다. 강제로 종료할까요?
pt_BR.ForceClosePrompt=Não foi possível encerrar %1 com segurança. Forçar o encerramento?
it.ForceClosePrompt=Impossibile chiudere %1 correttamente. Forzarne la chiusura?

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:AppName}"; Filename: "{app}\{#AppExeName}"
; Deliberately no "uninstall" shortcut: hiding uninstall entries is what this
; program does. It is removed through Settings > Apps, or by running unins000.exe
; from the installation folder.
Name: "{autodesktop}\{cm:AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{cm:AppName}}"; Flags: nowait postinstall skipifsilent

; The cleanup step runs from [Code] rather than [UninstallRun] so that a failure
; is visible: a silent failure would delete the program with shortcuts still
; hidden and a sign-in task still installed.
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
const
  // Fixed in ui.cpp: the window class the program registers.
  MainWindowClass = 'NoUninstallIcon.MainWindow';
  WM_CLOSE = $0010;

function FindWindowW(ClassName, WindowName: String): THandle;
  external 'FindWindowW@user32.dll stdcall';
function PostMessageW(Window: THandle; Msg: Cardinal; WParam, LParam: LongInt): Boolean;
  external 'PostMessageW@user32.dll stdcall';

function InitializeSetup(): Boolean;
begin
  Result := True;
end;

// An upgrade does not delete shortcuts that the older script created and this
// one no longer lists, so an installation made before the uninstall shortcut was
// dropped would keep it forever. Delete any shortcut in the group that points at
// this product's uninstaller; the application shortcut points at the exe and is
// left alone.
procedure RemoveStaleUninstallShortcut();
var
  FindRec: TFindRec;
  Group, ItemPath, Target, Uninstaller: String;
  Shell: Variant;
begin
  Uninstaller := ExpandConstant('{uninstallexe}');
  Group := ExpandConstant('{group}');
  // Both constants are used as the deletion criterion, so refuse to run if either
  // is empty rather than treat every unreadable shortcut as a match.
  if (Uninstaller = '') or (Group = '') then
    Exit;
  if not FindFirst(AddBackslash(Group) + '*.lnk', FindRec) then
    Exit;

  try
    Shell := CreateOleObject('WScript.Shell');
  except
    Log('Shortcut cleanup skipped: WScript.Shell is unavailable.');
    FindClose(FindRec);
    Exit;
  end;

  try
    repeat
      if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
      begin
        ItemPath := AddBackslash(Group) + FindRec.Name;
        Target := '';
        try
          Target := Shell.CreateShortcut(ItemPath).TargetPath;
        except
          Log('Shortcut cleanup could not read the target of ' + FindRec.Name + '.');
        end;
        if (Target <> '') and (CompareText(Target, Uninstaller) = 0) then
        begin
          if DeleteFile(ItemPath) then
            Log('Removed a stale uninstall shortcut: ' + FindRec.Name)
          else
            Log('Shortcut cleanup could not remove ' + FindRec.Name + '.');
        end;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

// A running watcher keeps its executable mapped, so Setup cannot replace it
// until the watcher leaves. It is asked to leave through --stop-watcher, which
// travels over the shared section and changes nothing on disk. Versions before
// 0.1.1 do not know that switch, and an unknown argument makes them open the
// window instead, so the switch is only used when the installed build supports
// it. When the watcher will not go, the user is offered a forced close.
function CanStopSafely(const ExePath: String): Boolean;
var
  MS, LS: Cardinal;
begin
  // 0.1.1.0 packs as MS = $00000001 (major 0, minor 1) and LS = $00010000
  // (build 1, revision 0), which is how GetVersionNumbers reports a file version.
  Result := False;
  if not FileExists(ExePath) then
    Exit;
  if not GetVersionNumbers(ExePath, MS, LS) then
    Exit;
  Result := (MS > $00000001) or ((MS = $00000001) and (LS >= $00010000));
end;

procedure AskWatcherToLeave(const ExePath: String);
var
  ResultCode: Integer;
begin
  if Exec(ExePath, '--stop-watcher', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Log('Asked the watcher to stop (exit code ' + IntToStr(ResultCode) + ').')
  else
    Log('Could not run --stop-watcher.');
end;

procedure ForceCloseRunningCopies();
var
  ResultCode: Integer;
begin
  Log('Forcing every running copy of {#AppExeName} to close.');
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#AppExeName}', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
  Log('taskkill returned ' + IntToStr(ResultCode) + '.');
  // Give the process a moment to release its image file and its mutex.
  Sleep(1500);
end;

// The window is an ordinary application and closes on request, so there is no reason
// to kill it. The class name is fixed in ui.cpp.
procedure CloseAppWindow();
var
  Attempt: Integer;
  Window: THandle;
begin
  Window := FindWindowW(MainWindowClass, '');
  if Window = 0 then
    Exit;

  Log('Asking the open window to close.');
  PostMessageW(Window, WM_CLOSE, 0, 0);
  for Attempt := 1 to 30 do
  begin
    Sleep(100);
    if FindWindowW(MainWindowClass, '') = 0 then
      Break;
  end;
end;

function AppStillRunning(): Boolean;
begin
  // The watcher holds the mutex; the window holds the image file. Either one blocks
  // the copy that follows.
  Result := CheckForMutexes('Local\NoUninstallIcon.Background') or
            (FindWindowW(MainWindowClass, '') <> 0);
end;

// False means "the app is still running and the user would not have it forced".
// The caller cancels the installation rather than copying over a locked executable,
// which is what Inno's own Restart Manager dialog allowed.
function EnsureAppStopped(): Boolean;
var
  ExePath: String;
begin
  Result := True;
  ExePath := ExpandConstant('{app}\{#AppExeName}');
  if not FileExists(ExePath) then
    Exit;  // a first install has no running copy to worry about

  if CanStopSafely(ExePath) then
    AskWatcherToLeave(ExePath);
  CloseAppWindow();

  if not AppStillRunning() then
    Exit;

  if WizardSilent then
  begin
    // Nobody to ask; an unattended upgrade has to make progress on its own.
    Log('The app is still running and Setup is silent; forcing it closed.');
    ForceCloseRunningCopies();
    Result := not AppStillRunning();
    Exit;
  end;

  if MsgBox(FmtMessage(ExpandConstant('{cm:ForceClosePrompt}'), [ExpandConstant('{cm:AppName}')]),
            mbConfirmation, MB_YESNO) = IDYES then
  begin
    ForceCloseRunningCopies();
    Result := not AppStillRunning();
    if not Result then
      Log('The app is still running even after the forced close.');
  end
  else
  begin
    Log('The user declined to force the running instance closed; cancelling Setup.');
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not EnsureAppStopped() then
    Result := FmtMessage(ExpandConstant('{cm:StillRunning}'), [ExpandConstant('{cm:AppName}')]);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    RemoveStaleUninstallShortcut();
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
        MsgBox(ExpandConstant('{cm:CleanupNotStarted}'), mbError, MB_OK);
    end
    else if ResultCode <> 0 then
    begin
      Log('Cleanup failed with code ' + IntToStr(ResultCode) + '.');
      // A silent uninstall must never wait on a dialog, and a message box would
      // be lost anyway; the log and the exit code are all we can offer there.
      if not UninstallSilent then
        MsgBox(FmtMessage(ExpandConstant('{cm:CleanupFailed}'), [IntToStr(ResultCode)]), mbError, MB_OK);
    end;
  end;
end;
