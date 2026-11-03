#include "common.h"

#include "app.h"
#include "crash.h"
#include "i18n.h"
#include "util.h"

#include <shellapi.h>

namespace {

struct Options
{
    enum class Mode
    {
        Gui,
        Apply,
        Watch,
        Screenshot,
        Dump,
        Preview,
        CrashTest,
        Restore,
        Cleanup,
        SelfTest,
    };

    Mode mode = Mode::Gui;
    std::wstring path;
    std::wstring capturePath;
    std::wstring previewQuery;
    int previewFocus = -1;
    int previewScroll = -1;
    bool showAbout = false;
    bool log = false;      // --log: write log.txt, off by default
    bool noTray = false;   // --background --no-tray: watch without a tray icon
    bool themeSpecified = false;
    bool darkTheme = false;
};

Options ParseOptions(int argc, wchar_t** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (EqualsI(arg, L"--screenshot") && i + 1 < argc)
        {
            options.mode = Options::Mode::Screenshot;
            options.path = argv[++i];
        }
        else if (EqualsI(arg, L"--dump") && i + 1 < argc)
        {
            options.mode = Options::Mode::Dump;
            options.path = argv[++i];
        }
        else if (EqualsI(arg, L"--apply") || EqualsI(arg, L"--silent"))
        {
            options.mode = Options::Mode::Apply;
        }
        else if (EqualsI(arg, L"--watch") || EqualsI(arg, L"--background"))
        {
            options.mode = Options::Mode::Watch;
        }
        else if (EqualsI(arg, L"--no-tray"))
            options.noTray = true;
        else if (EqualsI(arg, L"--cleanup"))
        {
            options.mode = Options::Mode::Cleanup;
        }
        else if (EqualsI(arg, L"--restore"))
        {
            options.mode = Options::Mode::Restore;
        }
        else if (EqualsI(arg, L"--log"))
        {
            options.log = true;
        }
        else if (EqualsI(arg, L"--selftest"))
        {
            options.mode = Options::Mode::SelfTest;
        }
        else if (EqualsI(arg, L"--crash-test"))
        {
            options.mode = Options::Mode::CrashTest;
        }
        else if (EqualsI(arg, L"--preview"))
        {
            options.mode = Options::Mode::Preview;
        }
        else if (EqualsI(arg, L"--capture") && i + 1 < argc)
        {
            options.mode = Options::Mode::Preview;
            options.capturePath = argv[++i];
        }
        else if (EqualsI(arg, L"--query") && i + 1 < argc)
        {
            options.previewQuery = argv[++i];
        }
        else if (EqualsI(arg, L"--focus") && i + 1 < argc)
        {
            options.previewFocus = _wtoi(argv[++i]);
        }
        else if (EqualsI(arg, L"--scroll") && i + 1 < argc)
        {
            options.previewScroll = _wtoi(argv[++i]);
        }
        else if (EqualsI(arg, L"--about"))
        {
            options.showAbout = true;
        }
        else if (EqualsI(arg, L"--theme") && i + 1 < argc)
        {
            options.themeSpecified = true;
            options.darkTheme = EqualsI(argv[++i], L"dark");
        }
    }
    return options;
}

bool RelaunchElevated(const wchar_t* parameters)
{
    const std::wstring exePath = GetExePath();  // keep the buffer alive for the call
    if (exePath.empty()) return false;
    LogLineF(L"Elevating: %s", exePath.c_str());

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = exePath.c_str();
    info.lpParameters = parameters;
    info.nShow = SW_SHOWNORMAL;
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    if (!ShellExecuteExW(&info))
    {
        LogLineF(L"Elevation failed: %s (error %lu)", exePath.c_str(), GetLastError());
        return false;
    }
    if (info.hProcess) CloseHandle(info.hProcess);
    return true;
}

}  // namespace

// Everything that touches COM lives in this function, so the App object (and the
// COM objects it owns) is destroyed before CoUninitialize runs.
int RunApplication(HINSTANCE instance, int showCommand, const Options& options)
{
    App app;
    if (!app.Initialize(instance)) return 1;

    switch (options.mode)
    {
    case Options::Mode::Screenshot:
    {
        bool dark = options.themeSpecified ? options.darkTheme : IsSystemDarkMode();
        app.ScanBlocking();
        if (options.showAbout) app.SetAboutOpen(true);
        if (!options.previewQuery.empty()) app.SetPreviewQuery(options.previewQuery);
        if (options.previewScroll >= 0) app.SetPreviewScroll(options.previewScroll);
        if (options.previewFocus >= 0) app.SetPreviewFocus(options.previewFocus);
        return app.RenderScreenshot(options.path, dark) ? 0 : 1;
    }
    case Options::Mode::Dump:
        return app.WriteDump(options.path) ? 0 : 1;
    case Options::Mode::Apply:
        return app.RunSilentApply();
    case Options::Mode::Cleanup:
        return app.RunCleanup();
    case Options::Mode::Restore:
        return app.RunRestoreAll();
    case Options::Mode::SelfTest:
        return app.RunSelfTest();
    case Options::Mode::Watch:
        return app.RunWatch(!options.noTray);
    case Options::Mode::CrashTest:
    {
        volatile int* pointer = nullptr;
        *pointer = 1;  // deliberately fault to verify the minidump handler
        return 1;
    }
    case Options::Mode::Preview:
        app.SetCapturePath(options.capturePath);
        if (options.previewFocus >= 0) app.SetPreviewFocus(options.previewFocus);
        if (options.showAbout) app.SetAboutOpen(true);
        return app.RunGui(showCommand);
    case Options::Mode::Gui:
    default:
        break;
    }

    // NOUI_NO_ELEVATE=1 runs without the UAC prompt, for automated checks.
    wchar_t elevateFlag[8]{};
    const bool skipElevation =
        GetEnvironmentVariableW(L"NOUI_NO_ELEVATE", elevateFlag, 8) > 0 && elevateFlag[0] == L'1';

    if (!IsElevated() && !skipElevation)
    {
        bool launched = RelaunchElevated(LogEnabled() ? L"--log" : nullptr);
        if (!launched)
            MessageBoxW(nullptr, i18n::T(L"error.elevation"), i18n::T(L"app.title"), MB_OK | MB_ICONWARNING);
        return launched ? 0 : 1;
    }

    // One window at a time. A second launch activates the existing window
    // instead of letting two processes race over the same state file.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\NoUninstallIcon.MainWindow");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(L"NoUninstallIcon.MainWindow", nullptr))
        {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            const DWORD targetThread = GetWindowThreadProcessId(existing, nullptr);
            const DWORD thisThread = GetCurrentThreadId();
            if (targetThread && targetThread != thisThread)
                AttachThreadInput(thisThread, targetThread, TRUE);
            SetForegroundWindow(existing);
            BringWindowToTop(existing);
            if (targetThread && targetThread != thisThread)
                AttachThreadInput(thisThread, targetThread, FALSE);
            FlashWindow(existing, TRUE);
        }
        CloseHandle(instanceMutex);
        return 0;
    }

    return app.RunGui(showCommand);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    crash::Install();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    Options options = ParseOptions(argc, argv);
    // The log is a debugging aid: off unless asked for, here or by environment.
    bool logging = options.log;
    if (!logging)
    {
        wchar_t value[8]{};
        logging = GetEnvironmentVariableW(L"NOUI_LOG", value, 8) > 0 && value[0] != L'0';
    }
    SetLogEnabled(logging);
    LocalFree(argv);

    int result = RunApplication(instance, showCommand, options);
    CoUninitialize();
    return result;
}
