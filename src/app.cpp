#include "app.h"

#include "apply.h"
#include "autostart.h"
#include "i18n.h"
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
        if (status == ApplyStatus::Missing || status == ApplyStatus::Ok)
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
    int restored = 0;
    for (auto& record : store_.Records())
    {
        bool originallyHidden = (record.originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (SetShortcutHidden(record.path, originallyHidden) != ApplyStatus::Failed)
        {
            NotifyShellItem(record.path);
            ++restored;
        }
    }
    store_.Records().clear();
    store_.Save();
    last_.valid = false;
    MergeRecords();
    window_.ModelChanged();
    ShowToast(i18n::Tf(L"toast.unhidden", {std::to_wstring(restored)}), false);
}

void App::HandleHideGroup(bool hide)
{
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
            if (SetShortcutHidden(item.path, originallyHidden) != ApplyStatus::Failed)
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
    if (changed > 0) store_.Save();
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
        // The task only runs at the next sign-in, so start the watcher now. Any
        // instance already running may be the other flavour, so ask it to leave
        // and start the requested one once the slot is free. A generation stamp
        // makes fast back-and-forth switching settle on the last choice instead
        // of racing: a superseded starter gives up.
        const unsigned generation = ++autostartGeneration_;
        std::thread([this, mode, generation]() {
            HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\NoUninstallIcon.Stop");
            if (stop)
            {
                SetEvent(stop);
                CloseHandle(stop);
            }

            // Wait for the previous instance to release the slot, rather than
            // guessing a duration.
            for (int attempt = 0; attempt < 40; ++attempt)
            {
                HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\NoUninstallIcon.Background");
                if (!running) break;
                CloseHandle(running);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (generation != autostartGeneration_) return;  // a newer choice won
            if (mode != AutostartMode::Watch && mode != AutostartMode::Silent) return;
            const wchar_t* arguments =
                (mode == AutostartMode::Silent) ? L"--background --no-tray" : L"--background";
            ShellExecuteW(nullptr, L"open", GetExePath().c_str(), arguments, nullptr, SW_SHOWNORMAL);
        }).detach();
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
    int changed = ApplyDesired(true);
    LogLineF(L"Silent apply finished, %d shortcut(s) re-hidden", changed);
    return 0;
}

int App::RunRestoreAll()
{
    int restored = 0;
    for (auto& record : store_.Records())
    {
        bool originallyHidden = (record.originalAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        if (SetShortcutHidden(record.path, originallyHidden) != ApplyStatus::Failed)
        {
            NotifyShellItem(record.path);
            ++restored;
        }
    }
    store_.Records().clear();
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
    RemoveAutostart();
    return RunRestoreAll();
}

int App::RunWatch(bool showTray)
{

    // One background instance only: the sign-in task and a manual launch must
    // not both own a tray icon.
    HANDLE slot = CreateMutexW(nullptr, FALSE, L"Local\\NoUninstallIcon.Background");
    if (slot && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(slot);
        return 0;
    }
    ApplyDesired(true);

    // Hidden window so the tray icon gets its messages.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance_;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);
    // A real (but never shown) top-level window: the shell cannot deliver tray
    // callbacks to a message-only window, which is why clicks did nothing.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, L"NoUninstallIcon", WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = hwnd;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = WM_APP + 10;
    icon.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!icon.hIcon) icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon.szTip, i18n::T(L"app.title"));
    // The shell may not be ready yet (early logon start, Explorer restart), and
    // then the registration is refused; retry, and re-add when TaskbarCreated
    // arrives.
    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    auto addIcon = [&]() {
        if (!showTray) return TRUE;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            if (Shell_NotifyIconW(NIM_ADD, &icon)) return TRUE;
            const DWORD error = GetLastError();
            LogLineF(L"tray: add failed attempt=%d err=%lu", attempt + 1, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return FALSE;
    };
    LogLineF(L"tray: add=%d hwnd=%p showTray=%d", addIcon() ? 1 : 0, reinterpret_cast<void*>(hwnd),
             showTray ? 1 : 0);

    std::stop_source stop;
    std::mutex applyMutex;

    auto watcher = [&](const KNOWNFOLDERID& folder) {
        // NOUI_SCAN_ROOT points the watcher at a fixture folder for tests.
        wchar_t overridePath[MAX_PATH]{};
        std::wstring root;
        if (GetEnvironmentVariableW(L"NOUI_SCAN_ROOT", overridePath, MAX_PATH) > 0)
        {
            root = overridePath;
        }
        else
        {
            PWSTR path = nullptr;
            if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path)) || !path) return;
            root = path;
            CoTaskMemFree(path);
        }

        HANDLE notification = FindFirstChangeNotificationW(
            root.c_str(), TRUE,
            // A shortcut rewritten in place changes only its size and timestamp,
            // so those have to be watched as well as names and attributes.
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (notification == INVALID_HANDLE_VALUE) return;

        while (!stop.stop_requested())
        {
            DWORD wait = WaitForSingleObject(notification, 400);
            if (wait == WAIT_OBJECT_0)
            {
                // Let the installer finish writing before re-applying.
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
                {
                    std::lock_guard<std::mutex> guard(applyMutex);
                    // The GUI may have added or removed records since we started;
                    // reload so a re-apply never writes back a stale list.
                    store_.Load();
                    ApplyDesired(true);
                }
                if (!FindNextChangeNotification(notification)) break;
            }
        }
        FindCloseChangeNotification(notification);
    };
    std::thread userWatcher(watcher, FOLDERID_Programs);
    std::thread machineWatcher(watcher, FOLDERID_CommonPrograms);

    // The window process signals this event when the mode no longer needs a
    // background, so switching the setting off also stops the running instance.
    const DWORD loopThread = GetCurrentThreadId();
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\NoUninstallIcon.Stop");
    if (stopEvent)
    {
        ResetEvent(stopEvent);
        std::thread([stopEvent, loopThread]() {
            WaitForSingleObject(stopEvent, INFINITE);
            PostThreadMessageW(loopThread, WM_QUIT, 0, 0);
        }).detach();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (taskbarCreated && message.message == taskbarCreated)
        {
            // Explorer restarted and dropped the icon: put it back.
            LogLineF(L"tray: shell restarted, re-adding");
            addIcon();
        }
        else if (message.message == WM_APP + 10)
        {
            const WORD event = LOWORD(message.lParam);
            LogLineF(L"tray: event=0x%04X hwnd=%p", static_cast<unsigned>(event),
                     reinterpret_cast<void*>(message.hwnd));
            if (event == WM_RBUTTONUP)
            {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 2001, i18n::T(L"tray.show"));
                SetMenuDefaultItem(menu, 2001, FALSE);  // bold, the usual default
                AppendMenuW(menu, MF_STRING, 2002, i18n::T(L"tray.quit"));

                POINT point{};
                GetCursorPos(&point);
                SetForegroundWindow(hwnd);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                                   hwnd, nullptr);
                DestroyMenu(menu);
                // Required so the menu closes cleanly and the next click arrives.
                PostMessageW(hwnd, WM_NULL, 0, 0);

                LogLineF(L"tray: menu command=%d", command);
                if (command == 2001)
                {
                    ShellExecuteW(nullptr, L"open", GetExePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                else if (command == 2002)
                {
                    PostQuitMessage(0);
                }
            }
            else if (event == WM_LBUTTONDBLCLK)
            {
                LogLineF(L"tray: open main window");
                ShellExecuteW(nullptr, L"open", GetExePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    stop.request_stop();
    if (userWatcher.joinable()) userWatcher.join();
    if (machineWatcher.joinable()) machineWatcher.join();

    if (showTray) Shell_NotifyIconW(NIM_DELETE, &icon);
    LogLineF(L"tray: removed");
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
