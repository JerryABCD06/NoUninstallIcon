#include "app.h"

#include "apply.h"
#include "autostart.h"
#include "i18n.h"
#include "session.h"
#include "tray.h"
#include "util.h"

#include <chrono>
#include <ctime>
#include <stop_token>
#include <thread>

namespace {

constexpr wchar_t kWndClass[] = L"NoUninstallIcon.Watch";

long long NowSeconds()
{
    return static_cast<long long>(std::time(nullptr));
}

}  // namespace

App::~App()
{
    // The scan thread captures `this`; it must not outlive this object.
    if (scanThread_.joinable()) scanThread_.join();
}

bool App::Initialize(HINSTANCE instance)
{
    instance_ = instance;
    store_.Load();
    i18n::Init(store_.Settings().language);
    LogLineF(L"Data directory: %s%s", GetDataDir().c_str(), IsPortableMode() ? L" (portable)" : L"");
    window_.SetModel(&model_);
    window_.SetAutostartMode(store_.Settings().autostart);
    return true;
}

void App::StartScan()
{
    if (scanning_) return;
    scanning_ = true;
    model_.loading = true;
    model_.statusText.clear();
    window_.SetLoading(true);

    if (scanThread_.joinable()) scanThread_.join();

    scanThread_ = std::thread([this]() {
        auto outcome = std::make_unique<ScanOutcome>();
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ScanStartMenu(*outcome);
        CoUninitialize();

        {
            std::lock_guard<std::mutex> guard(scanMutex_);
            pendingScan_ = std::move(outcome);
        }
        if (window_.Handle()) PostMessageW(window_.Handle(), WM_APP_SCAN_DONE, 0, 0);
    });
}

void App::FinishScan()
{
    std::unique_ptr<ScanOutcome> outcome;
    {
        std::lock_guard<std::mutex> guard(scanMutex_);
        outcome = std::move(pendingScan_);
    }
    scanning_ = false;
    if (!outcome) return;

    model_.items = std::move(outcome->items);
    model_.loading = false;
    MergeRecords();
    window_.ModelChanged();
    LogLineF(L"Scan finished: %d shortcuts", static_cast<int>(model_.items.size()));
}

void App::MergeRecords()
{
    for (auto& item : model_.items)
    {
        StoredRecord* record = store_.Find(item.key);
        if (!record)
        {
            item.managed = false;
            item.desiredHidden = false;
            continue;
        }
        item.managed = true;
        item.desiredHidden = record->desiredHidden;
        record->path = item.path;
        record->displayName = item.displayName;
        record->target = item.target;
    }
}

void App::HandleToggle(int itemIndex, bool hidden)
{
    StateLock stateGuard;  // serialise with the other process
    if (itemIndex < 0 || itemIndex >= static_cast<int>(model_.items.size())) return;
    ShortcutItem& item = model_.items[itemIndex];

    DWORD attributes = 0;
    ReadAttributes(item.path, attributes);

    if (hidden)
    {
        StoredRecord* record = store_.Find(item.key);
        bool created = false;
        if (!record)
        {
            record = &store_.Ensure(item.key);
            record->path = item.path;
            record->originalAttributes = attributes;
            record->displayName = item.displayName;
            record->target = item.target;
            record->firstSeen = NowSeconds();
            created = true;
        }

        ApplyStatus status = SetShortcutHidden(item.path, true);
        if (status == ApplyStatus::Ok)
        {
            record->desiredHidden = true;
            record->lastApplied = NowSeconds();
            item.managed = true;
            item.desiredHidden = true;
            item.hiddenOnDisk = true;
            last_ = {{item.key}, true, true};
            NotifyShellItem(item.path);
            store_.Save();
            ShowToast(i18n::Tf(L"toast.hiddenItem", {item.displayName}), true);
            if (window_.Handle())
                window_.Announce(i18n::Tf(L"a11y.announce.hidden", {item.displayName}));
        }
        else
        {
            if (created) store_.Erase(item.key);
            ShowToast(i18n::Tf(L"toast.failed", {L"1"}), false);
        }
    }
    else
    {
        StoredRecord* record = store_.Find(item.key);
        bool originallyHidden = record && (record->originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        ApplyStatus status = SetShortcutHidden(item.path, originallyHidden);
        if (status == ApplyStatus::Ok)
        {
            store_.Erase(item.key);
            item.managed = false;
            item.desiredHidden = false;
            item.hiddenOnDisk = originallyHidden;
            last_ = {{item.key}, false, true};
            NotifyShellItem(item.path);
            store_.Save();
            // A shortcut that was already hidden before this tool touched it stays
            // hidden; say so instead of letting the user think nothing happened.
            ShowToast(i18n::Tf(originallyHidden ? L"toast.stillHiddenItem" : L"toast.shownItem",
                               {item.displayName}),
                      true);
            if (window_.Handle())
                window_.Announce(i18n::Tf(originallyHidden ? L"a11y.announce.stillHidden" : L"a11y.announce.shown",
                                          {item.displayName}));
        }
        else
        {
            ShowToast(i18n::Tf(L"toast.failed", {L"1"}), false);
        }
    }

    window_.ModelChanged();
}

void App::HandleRestoreAll()
{
    StateLock stateGuard;  // serialise with the other process
    int restored = 0;
    std::vector<std::wstring> restoredKeys;
    for (auto& record : store_.Records())
    {
        bool originallyHidden = (record.originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (SetShortcutHidden(record.path, originallyHidden) == ApplyStatus::Ok)
        {
            NotifyShellItem(record.path);
            restoredKeys.push_back(record.key);
            ++restored;
        }
    }
    // Only forget the ones that really came back: a failed restore must stay
    // fixable instead of leaving a hidden shortcut nobody manages.
    for (const auto& key : restoredKeys) store_.Erase(key);
    store_.Save();
    last_.valid = false;
    MergeRecords();
    window_.ModelChanged();
    ShowToast(i18n::Tf(L"toast.unhidden", {std::to_wstring(restored)}), false);
}

void App::HandleHideGroup(bool hide)
{
    StateLock stateGuard;  // serialise with the other process
    int changed = 0;
    std::vector<std::wstring> touched;
    // Act on what the list is showing: the header count is the filtered count,
    // so the button must not reach entries the user cannot see.
    for (int index : model_.rows)
    {
        ShortcutItem& item = model_.items[index];
        if (!item.isUninstall) continue;
        if (hide == (item.managed && item.desiredHidden)) continue;
        if (hide)
        {
            StoredRecord* record = store_.Find(item.key);
            DWORD attributes = 0;
            ReadAttributes(item.path, attributes);
            if (!record)
            {
                record = &store_.Ensure(item.key);
                record->path = item.path;
                record->originalAttributes = attributes;
                record->displayName = item.displayName;
                record->target = item.target;
                record->firstSeen = NowSeconds();
            }
            if (SetShortcutHidden(item.path, true) == ApplyStatus::Ok)
            {
                record->desiredHidden = true;
                record->lastApplied = NowSeconds();
                item.managed = true;
                item.desiredHidden = true;
                item.hiddenOnDisk = true;
                NotifyShellItem(item.path);
                touched.push_back(item.key);
                ++changed;
            }
        }
        else
        {
            StoredRecord* record = store_.Find(item.key);
            bool originallyHidden = record && (record->originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
            if (SetShortcutHidden(item.path, originallyHidden) == ApplyStatus::Ok)
            {
                store_.Erase(item.key);
                item.managed = false;
                item.desiredHidden = false;
                item.hiddenOnDisk = originallyHidden;
                NotifyShellItem(item.path);
                touched.push_back(item.key);
                ++changed;
            }
        }
    }
    store_.Save();
    if (changed > 0)
        last_ = {touched, hide, true};
    else
        last_.valid = false;
    window_.ModelChanged();
    ShowToast(i18n::Tf(hide ? L"toast.hidden" : L"toast.unhidden", {std::to_wstring(changed)}), changed > 0);
}

void App::HandleAutostart(AutostartMode mode)
{
    // Re-selecting the mode that is already running must not restart the watcher:
    // that would blink the tray icon away just as the user reaches for it.
    if (mode == store_.Settings().autostart &&
        (mode == AutostartMode::Watch || mode == AutostartMode::Silent))
    {
        HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\NoUninstallIcon.Background");
        if (running)
        {
            CloseHandle(running);
            window_.SetAutostartMode(mode);
            return;
        }
    }

    // Registering a task talks to the Task Scheduler service; doing that on the
    // UI thread can leave the window unresponsive if the service is busy. The
    // result comes back as WM_APP_AUTOSTART_DONE.
    HWND hwnd = window_.Handle();
    window_.SetAutostartMode(mode);

    std::thread([hwnd, mode]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool ok = (mode == AutostartMode::Off) ? RemoveAutostart() : InstallAutostart(mode);
        CoUninitialize();
        if (hwnd) PostMessageW(hwnd, WM_APP_AUTOSTART_DONE, ok ? 1u : 0u, static_cast<LPARAM>(mode));
    }).detach();
}

void App::HandleOpenLocation(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(model_.items.size())) return;
    std::wstring args = L"/select,\"" + model_.items[itemIndex].path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::HandleUndo()
{
    StateLock stateGuard;  // serialise with the other process
    if (!last_.valid) return;
    const LastAction action = last_;
    last_.valid = false;
    for (const auto& key : action.keys)
    {
        for (int i = 0; i < static_cast<int>(model_.items.size()); ++i)
        {
            if (model_.items[i].key != key) continue;
            HandleToggle(i, !action.state);
            break;
        }
    }
    last_.valid = false;
}

void App::HandleRescan()
{
    StartScan();
}

void App::ShowToast(const std::wstring& text, bool undoable)
{
    if (window_.Handle()) window_.ShowToast(text, undoable);
}

int App::ApplyDesired(bool notify)
{
    int changed = 0;
    for (auto& record : store_.Records())
    {
        if (!record.desiredHidden) continue;
        DWORD attributes = 0;
        if (!ReadAttributes(record.path, attributes))
        {
            LogLineF(L"  missing: %s", record.path.c_str());
            continue;
        }
        if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) continue;
        if (SetShortcutHidden(record.path, true) == ApplyStatus::Ok)
        {
            record.lastApplied = NowSeconds();
            if (notify) NotifyShellItem(record.path);
            ++changed;
        }
    }
    // Deliberately not saved: a watcher that rewrites the file also rewrites
    // the settings it does not own, and can revert a mode change made meanwhile.
    return changed;
}

int App::RunGui(int showCommand)
{
    window_.callbacks.toggleItem = [this](int index, bool hidden) { HandleToggle(index, hidden); };
    window_.callbacks.rescan = [this]() { HandleRescan(); };
    window_.callbacks.restoreAll = [this]() { HandleRestoreAll(); };
    window_.callbacks.hideUninstallGroup = [this](bool hide) { HandleHideGroup(hide); };
    window_.callbacks.autostartChanged = [this](AutostartMode mode) { HandleAutostart(mode); };
    window_.callbacks.languageChanged = [this](const std::wstring& code) {
        store_.Settings().language = code;
        store_.Save();
    };
    window_.callbacks.undo = [this]() { HandleUndo(); };
    window_.callbacks.openLocation = [this](int index) { HandleOpenLocation(index); };
    window_.callbacks.scanDone = [this]() { FinishScan(); };
    window_.callbacks.autostartDone = [this](bool ok, AutostartMode mode) {
        if (!ok)
        {
            LogLineF(L"Autostart change failed: %s", AutostartLastError().c_str());
            window_.SetAutostartMode(AutostartMode::Off);
            window_.ShowToast(i18n::T(L"error.task"), false);
            return;
        }
        store_.Settings().autostart = mode;
        store_.Save();
        window_.SetAutostartMode(mode);
        // Tell the watcher what it should be. It adjusts its own icon and exits
        // when the mode stops wanting one, so nothing is restarted here and the
        // icon never blinks. A watcher is started only when none is running.
        session::Publish(static_cast<int>(mode));
        if (mode == AutostartMode::Watch || mode == AutostartMode::Silent)
        {
            bool alreadyRunning = false;
            if (HANDLE slot = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\NoUninstallIcon.Background"))
            {
                alreadyRunning = true;
                CloseHandle(slot);
            }
            if (!alreadyRunning)
                ShellExecuteW(nullptr, L"open", GetExePath().c_str(), L"--background", nullptr, SW_SHOWNORMAL);
        }
        if (mode == AutostartMode::Watch || mode == AutostartMode::Silent)
            window_.ShowToast(i18n::T(L"toast.watchOn"), false);
        if (mode == AutostartMode::Once) window_.ShowToast(i18n::T(L"toast.onceOn"), false);
        if (mode == AutostartMode::Off) window_.ShowToast(i18n::T(L"error.task_removed"), false);
    };

    if (!window_.Create(instance_, showCommand)) return 1;
    if (previewFocus_ >= 0) window_.SetPreviewFocus(previewFocus_);

    model_.loading = true;
    StartScan();

    // If the sign-in task disappeared (Windows update, cleanup tool), recreate it
    // in the background so a slow Task Scheduler service cannot delay startup.
    const AutostartMode savedMode = store_.Settings().autostart;
    if (savedMode != AutostartMode::Off)
    {
        std::thread([savedMode]() {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (!IsAutostartInstalled()) InstallAutostart(savedMode);
            CoUninitialize();
        }).detach();
    }

    // Bring the runtime in line with the saved mode: publish it so a running
    // watcher adjusts, and start one when the mode wants it but none is running.
    session::Publish(static_cast<int>(savedMode));
    if (savedMode == AutostartMode::Watch || savedMode == AutostartMode::Silent)
    {
        bool alreadyRunning = false;
        if (HANDLE slot = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\NoUninstallIcon.Background"))
        {
            alreadyRunning = true;
            CloseHandle(slot);
        }
        if (!alreadyRunning)
            ShellExecuteW(nullptr, L"open", GetExePath().c_str(), L"--background", nullptr, SW_SHOWNORMAL);
    }

    if (!capturePath_.empty())
    {
        HWND hwnd = window_.Handle();
        SetForegroundWindow(hwnd);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 1200)
        {
            MSG pending{};
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&pending);
                DispatchMessageW(&pending);
            }
            Sleep(10);
        }
        CaptureWindowToPng(hwnd, capturePath_);
        PostQuitMessage(0);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

int App::RunSilentApply()
{
    StateLock stateGuard;  // serialise with the other process
    int changed = ApplyDesired(true);
    LogLineF(L"Silent apply finished, %d shortcut(s) re-hidden", changed);
    return 0;
}

int App::RunRestoreAll()
{
    StateLock stateGuard;  // serialise with the other process
    int restored = 0;
    std::vector<std::wstring> restoredKeys;
    for (auto& record : store_.Records())
    {
        bool originallyHidden = (record.originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (SetShortcutHidden(record.path, originallyHidden) == ApplyStatus::Ok)
        {
            NotifyShellItem(record.path);
            restoredKeys.push_back(record.key);
            ++restored;
        }
    }
    // Only forget the ones that really came back: a failed restore must stay
    // fixable instead of leaving a hidden shortcut nobody manages.
    for (const auto& key : restoredKeys) store_.Erase(key);
    store_.Save();
    LogLineF(L"Restore all finished, %d shortcut(s) restored", restored);
    return 0;
}

int App::RunSelfTest()
{
    std::wstring report;
    ScanBlocking();
    report += Fmt(L"scanned=%d\r\n", static_cast<int>(model_.items.size()));
    if (model_.items.empty())
    {
        report += L"FAIL: nothing scanned\r\n";
        WriteWholeFileAtomic(JoinPath(GetDataDir(), L"selftest.txt"), Narrow(report));
        return 1;
    }

    int index = 0;
    for (int i = 0; i < static_cast<int>(model_.items.size()); ++i)
    {
        if (model_.items[i].isUninstall) { index = i; break; }
    }
    const std::wstring path = model_.items[index].path;
    const std::wstring key = model_.items[index].key;
    report += Fmt(L"target=%s\r\n", path.c_str());

    DWORD before = 0;
    ReadAttributes(path, before);

    HandleToggle(index, true);
    DWORD afterHide = 0;
    ReadAttributes(path, afterHide);
    report += Fmt(L"hide   : bit=%d managed=%d record=%d\r\n", (afterHide & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0,
                  model_.items[index].managed ? 1 : 0, store_.Find(key) ? 1 : 0);

    HandleToggle(index, false);
    DWORD afterShow = 0;
    ReadAttributes(path, afterShow);
    report += Fmt(L"unhide : bit=%d managed=%d record=%d\r\n", (afterShow & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0,
                  model_.items[index].managed ? 1 : 0, store_.Find(key) ? 1 : 0);

    bool ok = (before & FILE_ATTRIBUTE_HIDDEN) == (afterShow & FILE_ATTRIBUTE_HIDDEN);

    // Case 2: a shortcut that was already hidden before this tool touched it
    // must stay hidden when the user unchecks it again.
    int second = -1;
    for (int i = 0; i < static_cast<int>(model_.items.size()); ++i)
    {
        if (i != index) { second = i; break; }
    }
    if (second >= 0)
    {
        const std::wstring path2 = model_.items[second].path;
        SetFileAttributesW(path2.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
        HandleToggle(second, true);
        HandleToggle(second, false);
        DWORD afterSecond = 0;
        ReadAttributes(path2, afterSecond);
        bool hiddenKept = (afterSecond & FILE_ATTRIBUTE_HIDDEN) != 0;
        report += Fmt(L"external: bit=%d managed=%d\r\n", hiddenKept ? 1 : 0,
                      model_.items[second].managed ? 1 : 0);
        ok = ok && hiddenKept && !model_.items[second].managed;
        SetFileAttributesW(path2.c_str(), FILE_ATTRIBUTE_ARCHIVE);
    }

    report += ok ? L"RESULT: ok\r\n" : L"RESULT: mismatch\r\n";
    WriteWholeFileAtomic(JoinPath(GetDataDir(), L"selftest.txt"), Narrow(report));
    LogLine(L"Self test finished");
    return ok ? 0 : 1;
}

int App::RunCleanup()
{
    // Uninstalling must leave nothing running: tell the watcher the mode is off
    // and wait for it to go, so it cannot re-hide shortcuts during the restore.
    // Persist Off as well: cleanup runs before the uninstaller deletes the
    // executable, and a stored Watch would recreate the task on a later launch.
    store_.Settings().autostart = AutostartMode::Off;
    store_.Save();

    session::Publish(static_cast<int>(AutostartMode::Off));
    for (int tick = 0; tick < 40; ++tick)
    {
        HANDLE slot = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\NoUninstallIcon.Background");
        if (!slot) break;
        CloseHandle(slot);
        Sleep(100);
    }

    RemoveAutostart();
    return RunRestoreAll();
}

int App::RunWatch(bool showTray)
{
    // Only one watcher may run; the sign-in task normally owns it.
    SetLastError(0);
    HANDLE slot = CreateMutexW(nullptr, FALSE, L"Local\\NoUninstallIcon.Background");
    const DWORD slotError = GetLastError();
    if (!slot || slotError == ERROR_ALREADY_EXISTS)
    {
        // NULL means we could not tell (access denied between integrity levels),
        // and starting a second watcher is worse than not starting one.
        if (slot) CloseHandle(slot);
        LogLineF(L"watcher: not starting (err=%lu)", slotError);
        return 0;
    }
    // Read the mode once, before the worker threads start touching the store.
    const bool initialTray = showTray && store_.Settings().autostart == AutostartMode::Watch;

    {
        StateLock stateGuard;  // the first apply must not overlap a restore
        ApplyDesired(true);
    }

    std::stop_source stop;
    std::mutex applyMutex;

    // Resolved here, on a thread with COM initialised: the worker threads have
    // none, and SHGetKnownFolderPath fails there, which would silently leave the
    // watcher doing nothing at all.
    auto resolveRoot = [](const KNOWNFOLDERID& folder) -> std::wstring {
        wchar_t overridePath[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"NOUI_SCAN_ROOT", overridePath, MAX_PATH) > 0) return overridePath;
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path)) || !path) return std::wstring();
        std::wstring root = path;
        CoTaskMemFree(path);
        return root;
    };

    const std::wstring userRoot = resolveRoot(FOLDERID_Programs);
    const std::wstring machineRoot = resolveRoot(FOLDERID_CommonPrograms);

    auto watcher = [&](std::wstring root)
    {
        if (root.empty())
        {
            LogLine(L"watcher: no folder to watch");
            return;
        }

        HANDLE notification = FindFirstChangeNotificationW(
            root.c_str(), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (notification == INVALID_HANDLE_VALUE)
        {
            LogLineF(L"watcher: cannot watch %s err=%lu", root.c_str(), GetLastError());
            return;
        }

        while (!stop.stop_requested())
        {
            if (WaitForSingleObject(notification, 400) != WAIT_OBJECT_0) continue;
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            {
                std::lock_guard<std::mutex> guard(applyMutex);
                StateLock stateGuard;  // do not apply while a restore is running
                store_.Load();
                ApplyDesired(true);
            }
            if (!FindNextChangeNotification(notification)) break;
        }
        FindCloseChangeNotification(notification);
    };


    std::thread userWatcher(watcher, userRoot);
    std::thread machineWatcher(watcher, machineRoot);


    // The tray window is always created; whether an icon is shown follows the
    // mode, so switching flavours never needs a restart.
    {
        tray::Callbacks callbacks;
        callbacks.openWindow = []() {
            // Bring the existing window forward instead of starting a second one.
            const HWND existing = FindWindowW(L"NoUninstallIcon.MainWindow", nullptr);
            if (existing)
            {
                if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
                // A background process is usually not allowed to steal the
                // foreground, so attach to the window thread for the call.
                const DWORD targetThread = GetWindowThreadProcessId(existing, nullptr);
                const DWORD thisThread = GetCurrentThreadId();
                if (targetThread && targetThread != thisThread)
                    AttachThreadInput(thisThread, targetThread, TRUE);
                SetForegroundWindow(existing);
                BringWindowToTop(existing);
                if (targetThread && targetThread != thisThread)
                    AttachThreadInput(thisThread, targetThread, FALSE);
                return;
            }
            ShellExecuteW(nullptr, L"open", GetExePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        };
        callbacks.quit = []() {
            // Close the window first and wait for it to go: otherwise its own
            // reconciliation would notice a missing watcher and start a new one
            // just as this process exits.
            if (HWND existing = FindWindowW(L"NoUninstallIcon.MainWindow", nullptr))
            {
                PostMessageW(existing, WM_CLOSE, 0, 0);
                for (int tick = 0; tick < 40 && IsWindow(existing); ++tick)
                    Sleep(50);
            }
            PostQuitMessage(0);
        };
        tray::Start(instance_, i18n::T(L"app.title"), initialTray, callbacks);
    }

    // The watcher adjusts itself when the mode is published: no restart, so the
    // tray icon never blinks and no window is left without a watcher.
    HANDLE changeEvent = session::ChangeEvent();
    if (!changeEvent) LogLine(L"watcher: no change event; mode changes will not arrive");
    MSG message{};
    bool running = true;
    while (running)
    {
        const DWORD handleCount = changeEvent ? 1 : 0;
        const DWORD wait = MsgWaitForMultipleObjects(handleCount, &changeEvent, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_FAILED)
        {
            LogLineF(L"watcher: wait failed err=%lu", GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (changeEvent && wait == WAIT_OBJECT_0)
        {
            ResetEvent(changeEvent);  // before reading: a publish now re-sets it
            const int mode = session::Mode();
            const bool wantsTray = mode == static_cast<int>(AutostartMode::Watch);
            const bool wantsWatching = wantsTray || mode == static_cast<int>(AutostartMode::Silent);
            LogLineF(L"watcher: mode=%d tray=%d keep=%d", mode, wantsTray ? 1 : 0, wantsWatching ? 1 : 0);
            if (showTray) tray::SetVisible(wantsTray);
            if (!wantsWatching) running = false;
            continue;
        }

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    tray::Stop();
    stop.request_stop();
    if (userWatcher.joinable()) userWatcher.join();
    if (machineWatcher.joinable()) machineWatcher.join();
    return 0;
}

void App::ScanBlocking()
{
    auto outcome = std::make_unique<ScanOutcome>();
    ScanStartMenu(*outcome);
    model_.items = std::move(outcome->items);
    model_.loading = false;
    MergeRecords();
    window_.ModelChanged();
}

bool App::RenderScreenshot(const std::wstring& path, bool dark)
{
    const float width = 1040.0f;
    const float height = 760.0f;
    if (!window_.InitOffscreen()) return false;

    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;

    Theme theme = MakeTheme(dark);
    return RenderToPng(factory.Get(), path, static_cast<UINT>(width), static_cast<UINT>(height), theme.windowBg,
                       [&](ID2D1RenderTarget* target) { window_.RenderTo(target, width, height, dark); });
}

void App::WriteDump(const std::wstring& path)
{
    ScanOutcome outcome;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ScanStartMenu(outcome);
    CoUninitialize();

    std::wstring text = L"NoUninstallIcon scan dump\r\n";
    text += Fmt(L"total=%d skipped=%d\r\n", static_cast<int>(outcome.items.size()), outcome.skipped);
    for (const auto& item : outcome.items)
    {
        text += (item.isUninstall ? L"[U] " : L"[ ] ");
        text += item.displayName;
        text += L"\t| sort=";
        text += item.sortName;
        text += L"\t| ";
        text += item.target;
        text += L"\t| hidden=";
        text += item.hiddenOnDisk ? L"1" : L"0";
        text += L"\r\n";
    }

    // Second section: the order actually shown in the window.
    Model model;
    model.items = std::move(outcome.items);
    model.loading = false;
    model.Recompute();
    text += L"\r\n=== display order ===\r\n";
    for (int row = 0; row < model.RowCount(); ++row)
    {
        const ShortcutItem* item = model.At(row);
        text += Fmt(L"%3d %s %s  (sort=%s)\r\n", row, item->isUninstall ? L"U" : L"-", item->displayName.c_str(),
                    item->sortName.c_str());
    }
    WriteWholeFileAtomic(path, Narrow(text));
}
