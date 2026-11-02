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
    bool showAbout = false;
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
        else if (EqualsI(arg, L"--watch"))
        {
            options.mode = Options::Mode::Watch;
        }
        else if (EqualsI(arg, L"--cleanup"))
        {
            options.mode = Options::Mode::Cleanup;
        }
        else if (EqualsI(arg, L"--restore"))
        {
            options.mode = Options::Mode::Restore;
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

bool RelaunchElevated()
{
    const std::wstring exePath = GetExePath();  // keep the buffer alive for the call
    if (exePath.empty()) return false;
    LogLineF(L"Elevating: %s", exePath.c_str());

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = exePath.c_str();
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
        if (options.previewFocus >= 0) app.SetPreviewFocus(options.previewFocus);
        return app.RenderScreenshot(options.path, dark) ? 0 : 1;
    }
    case Options::Mode::Dump:
        app.WriteDump(options.path);
        return 0;
    case Options::Mode::Apply:
        return app.RunSilentApply();
    case Options::Mode::Cleanup:
        return app.RunCleanup();
    case Options::Mode::Restore:
        return app.RunRestoreAll();
    case Options::Mode::SelfTest:
        return app.RunSelfTest();
    case Options::Mode::Watch:
        return app.RunWatch();
    case Options::Mode::CrashTest:
    {
        volatile int* pointer = nullptr;
        *pointer = 1;  // deliberately fault to verify the minidump handler
        return 1;
    }
    case Options::Mode::Preview:
        app.SetCapturePath(options.capturePath);
        return app.RunGui(showCommand);
    case Options::Mode::Gui:
    default:
        break;
    }

    if (!IsElevated())
    {
        bool launched = RelaunchElevated();
        if (!launched)
            MessageBoxW(nullptr, i18n::T(L"error.elevation"), i18n::T(L"app.title"), MB_OK | MB_ICONWARNING);
        return launched ? 0 : 1;
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
    LocalFree(argv);

    int result = RunApplication(instance, showCommand, options);
    CoUninitialize();
    return result;
}