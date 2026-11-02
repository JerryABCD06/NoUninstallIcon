// Minimal tray probe. Reports the environment that decides whether the shell
// will accept a notification icon, then tries four flag combinations through the
// same kind of window the application uses.
#include <windows.h>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

static void ReportEnvironment()
{
    BOOL inJob = FALSE;
    IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    if (inJob && QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions, &ui, sizeof(ui), nullptr))
        std::printf("job ui restrictions = 0x%lX\n", ui.UIRestrictionsClass);
    else
        std::printf("job = %s\n", inJob ? "yes, no ui limits readable" : "none");

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
            std::printf("elevated = %d\n", elevation.TokenIsElevated ? 1 : 0);
        CloseHandle(token);
    }

    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::printf("session = %lu\n", session);
}

int main()
{
    ReportEnvironment();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TrayProbeWindow";
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"TrayProbeWindow", L"probe", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window)
    {
        std::printf("window creation failed, err=%lu\n", GetLastError());
        return 1;
    }
    std::printf("window=%p\n", static_cast<void*>(window));

    HICON icon = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!icon) icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    std::printf("icon=%p\n", static_cast<void*>(icon));

    const struct { const char* name; UINT flags; } steps[] = {
        {"message", NIF_MESSAGE},
        {"message+icon", NIF_MESSAGE | NIF_ICON},
        {"message+icon+tip", NIF_MESSAGE | NIF_ICON | NIF_TIP},
        {"message+tip", NIF_MESSAGE | NIF_TIP},
    };

    for (const auto& step : steps)
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = 7;
        data.uFlags = step.flags;
        data.uCallbackMessage = WM_APP + 7;
        data.hIcon = icon;
        wcscpy_s(data.szTip, L"probe");
        const BOOL added = Shell_NotifyIconW(NIM_ADD, &data);
        const DWORD error = GetLastError();
        std::printf("%-18s add=%d err=%lu\n", step.name, added ? 1 : 0, error);
        if (added) Shell_NotifyIconW(NIM_DELETE, &data);
        Sleep(200);
    }

    DestroyWindow(window);
    return 0;
}
