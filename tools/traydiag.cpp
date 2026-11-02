// Double-click friendly tray diagnostic. Writes everything it learns next to
// itself, because the application's own log cannot be trusted in every process
// context (a restricted token can make both the log write and the icon
// registration fail without an error anywhere).
#include <windows.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

static void Say(const wchar_t* format, ...);

static LRESULT CALLBACK DiagProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    // Version 4 of the notification protocol reports a right click as
    // WM_CONTEXTMENU rather than through the callback message, so record every
    // message that could possibly be a click.
    switch (message)
    {
    case WM_APP + 10:
    case WM_CONTEXTMENU:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP:
    case WM_RBUTTONDOWN:
        Say(L"window message     : 0x%04X wparam=0x%04X lparam=0x%04X", message,
            static_cast<unsigned>(wparam), static_cast<unsigned>(lparam));
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static FILE* gOut = nullptr;

static void Say(const wchar_t* format, ...)
{
    wchar_t buffer[1024]{};
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, format, args);
    va_end(args);
    if (gOut)
    {
        fwprintf(gOut, L"%s\n", buffer);
        fflush(gOut);
    }
    wprintf(L"%s\n", buffer);
}

static void ReportIdentity()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        Say(L"OpenProcessToken failed, err=%lu", GetLastError());
        return;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
        Say(L"elevated           : %d", elevation.TokenIsElevated ? 1 : 0);

    TOKEN_ELEVATION_TYPE type{};
    size = sizeof(type);
    if (GetTokenInformation(token, TokenElevationType, &type, size, &size))
        Say(L"elevation type     : %d (1=default 2=full 3=limited)", static_cast<int>(type));

    BYTE buffer[256]{};
    TOKEN_GROUPS_AND_PRIVILEGES* groups = reinterpret_cast<TOKEN_GROUPS_AND_PRIVILEGES*>(buffer);
    DWORD needed = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, &buffer, sizeof(buffer), &needed))
    {
        const TOKEN_MANDATORY_LABEL* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(&buffer);
        DWORD* rid = GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
        if (rid) Say(L"integrity          : 0x%lX", *rid);
    }
    (void)groups;
    CloseHandle(token);

    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    Say(L"session            : %lu", session);

    wchar_t name[256]{};
    DWORD length = 256;
    if (GetUserNameW(name, &length)) Say(L"user               : %s", name);

    // A restricted token is what a security product's sandbox looks like, and it
    // is the one thing that makes every shell-touching call fail with access
    // denied while everything else keeps working.
    HANDLE query = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &query))
    {
        DWORD count = 0;
        GetTokenInformation(query, TokenRestrictedSids, nullptr, 0, &count);
        Say(L"restricted sids    : %lu bytes of data", count);

        DWORD restrictions = 0;
        if (GetTokenInformation(query, TokenType, &restrictions, sizeof(restrictions), &count) || true)
        {
            TOKEN_TYPE type{};
            if (GetTokenInformation(query, TokenType, &type, sizeof(type), &count))
                Say(L"token type         : %d (1=primary 2=impersonation)", static_cast<int>(type));
        }

        wchar_t station[64]{};
        wchar_t desk[64]{};
        DWORD needed = 0;
        if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &needed))
            Say(L"window station     : %s", station);
        if (GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desk, sizeof(desk), &needed))
            Say(L"desktop            : %s", desk);

        BOOL inJob = FALSE;
        IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
        JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
        if (inJob && QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions, &ui, sizeof(ui), nullptr))
            Say(L"job ui restrictions: 0x%lX", ui.UIRestrictionsClass);
        else
            Say(L"job ui restrictions: none");

        CloseHandle(query);
    }
}

int wmain()
{
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring folder(exePath);
    const size_t slash = folder.find_last_of(L'\\');
    if (slash != std::wstring::npos) folder.resize(slash);
    const std::wstring outPath = folder + L"\\traytest-result.txt";

    _wfopen_s(&gOut, outPath.c_str(), L"w, ccs=UTF-8");
    Say(L"--- tray diagnostic ---");
    ReportIdentity();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DiagProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TrayDiagWindow";
    const ATOM atom = RegisterClassExW(&wc);
    Say(L"class registered   : %d (err=%lu)", atom ? 1 : 0, GetLastError());

    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"TrayDiagWindow", L"diag", WS_POPUP,
                                  0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    Say(L"window created     : %p (err=%lu)", static_cast<void*>(window), GetLastError());
    if (!window) return 1;

    HICON icon = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!icon) icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    Say(L"icon handle        : %p", static_cast<void*>(icon));

    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    Say(L"TaskbarCreated msg : 0x%04X", taskbarCreated);
    SetLastError(0);
    const BOOL filtered = ChangeWindowMessageFilterEx(window, WM_APP + 10, MSGFLT_ALLOW, nullptr);
    Say(L"filterEx Allow     : %d (err=%lu)", filtered ? 1 : 0, GetLastError());

    SetLastError(0);
    const BOOL legacy = ChangeWindowMessageFilter(WM_APP + 10, MSGFLT_ADD);
    Say(L"filter legacy Add  : %d (err=%lu)", legacy ? 1 : 0, GetLastError());

    NOTIFYICONDATAW data{};
    data.hWnd = window;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = WM_APP + 10;
    data.hIcon = icon;
    wcscpy_s(data.szTip, L"诊断");

    // The shell validates cbSize strictly, so try every documented size.
    const struct { const wchar_t* name; DWORD size; } sizes[] = {
        {L"sizeof (current)", static_cast<DWORD>(sizeof(NOTIFYICONDATAW))},
        {L"V2 size", static_cast<DWORD>(NOTIFYICONDATAW_V2_SIZE)},
        {L"V1 size", static_cast<DWORD>(NOTIFYICONDATAW_V1_SIZE)},
    };

    BOOL added = FALSE;
    DWORD usedSize = 0;
    for (const auto& variant : sizes)
    {
        data.cbSize = variant.size;
        SetLastError(0);
        const BOOL ok = Shell_NotifyIconW(NIM_ADD, &data);
        Say(L"NIM_ADD %-16s: %d (err=%lu)", variant.name, ok ? 1 : 0, GetLastError());
        if (ok)
        {
            added = TRUE;
            usedSize = variant.size;
            break;
        }
    }
    Say(L"NIM_ADD result     : %d (cbSize used=%lu)", added ? 1 : 0, usedSize);

    if (added)
    {
        data.cbSize = usedSize;
        data.uVersion = NOTIFYICON_VERSION_4;
        SetLastError(0);
        const BOOL versioned = Shell_NotifyIconW(NIM_SETVERSION, &data);
        Say(L"NIM_SETVERSION v4  : %d (err=%lu)", versioned ? 1 : 0, GetLastError());
    }

    if (added)
    {
        Say(L"icon added; it stays for 20 seconds so you can right-click it");
        Say(L"every call below is logged, so a click will appear in this file");
        const DWORD until = GetTickCount() + 20000;
        while (GetTickCount() < until)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_APP + 10)
                    Say(L"callback           : 0x%04X", static_cast<unsigned>(LOWORD(message.lParam)));
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(20);
        }
        Shell_NotifyIconW(NIM_DELETE, &data);
        Say(L"icon removed");
    }
    else
    {
        Say(L"registration refused, so there is nothing to click");
    }

    if (gOut) fclose(gOut);
    return 0;
}
