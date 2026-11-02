#include "tray.h"

#include "common.h"
#include "i18n.h"
#include "util.h"

#include <shellapi.h>

namespace {

constexpr wchar_t kClassName[] = L"NoUninstallIcon.Tray";
constexpr UINT kCallbackMessage = WM_APP + 10;
constexpr UINT kOpenCommand = 2001;
constexpr UINT kQuitCommand = 2002;

HWND gWindow = nullptr;
NOTIFYICONDATAW gIcon{};
HICON gIconHandle = nullptr;
tray::Callbacks gCallbacks;
UINT gTaskbarCreated = 0;

void ShowMenu(POINT anchor)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kOpenCommand, i18n::T(L"tray.show"));
    SetMenuDefaultItem(menu, kOpenCommand, FALSE);
    AppendMenuW(menu, MF_STRING, kQuitCommand, i18n::T(L"tray.quit"));

    // Without this the menu will not close properly when the user clicks away,
    // and the following clicks are lost.
    SetForegroundWindow(gWindow);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, gWindow,
                                       nullptr);
    DestroyMenu(menu);
    PostMessageW(gWindow, WM_NULL, 0, 0);
    LogLineF(L"tray: menu command=%d", command);

    if (command == kOpenCommand && gCallbacks.openWindow) gCallbacks.openWindow();
    else if (command == kQuitCommand && gCallbacks.quit) gCallbacks.quit();
}

bool AddIcon()
{
    if (!gIconHandle) return false;

    for (int attempt = 1; attempt <= 5; ++attempt)
    {
        if (Shell_NotifyIconW(NIM_ADD, &gIcon))
        {
            // Version 4 is what makes the shell report a right click as
            // WM_CONTEXTMENU and put the anchor point in wParam.
            NOTIFYICONDATAW version = gIcon;
            version.uVersion = NOTIFYICON_VERSION_4;
            const BOOL versioned = Shell_NotifyIconW(NIM_SETVERSION, &version);
            LogLineF(L"tray: added (attempt=%d version4=%d)", attempt, versioned ? 1 : 0);
            return true;
        }
        LogLineF(L"tray: add failed (attempt=%d err=%lu)", attempt, GetLastError());
        Sleep(300);
    }
    return false;
}

void HandleClick(POINT anchor, WORD event)
{
    LogLineF(L"tray: event=0x%04X anchor=%ld,%ld", event, anchor.x, anchor.y);

    switch (event)
    {
    case WM_CONTEXTMENU:
    case WM_RBUTTONUP:
        ShowMenu(anchor);
        break;
    case WM_LBUTTONDBLCLK:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        if (gCallbacks.openWindow) gCallbacks.openWindow();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == kCallbackMessage)
    {
        POINT anchor{};
        if (wparam)
        {
            anchor.x = static_cast<short>(LOWORD(wparam));
            anchor.y = static_cast<short>(HIWORD(wparam));
        }
        else
        {
            GetCursorPos(&anchor);
        }
        HandleClick(anchor, LOWORD(lparam));
        return 0;
    }

    if (message == WM_CONTEXTMENU)
    {
        // Delivered straight to the window by some shell paths.
        POINT anchor{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        if (lparam == static_cast<LPARAM>(-1)) GetCursorPos(&anchor);
        HandleClick(anchor, WM_CONTEXTMENU);
        return 0;
    }

    if (gTaskbarCreated && message == gTaskbarCreated)
    {
        // Explorer restarted and dropped the icon; put it back.
        LogLineF(L"tray: shell restarted, re-adding");
        AddIcon();
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

bool tray::Start(HINSTANCE instance, const std::wstring& tooltip, Callbacks callbacks)
{
    if (gWindow) return true;
    gCallbacks = std::move(callbacks);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    // A real top-level window that is never shown: the shell cannot deliver tray
    // callbacks to a message-only window.
    gWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"NoUninstallIcon", WS_POPUP, 0, 0, 0, 0, nullptr,
                              nullptr, instance, nullptr);
    if (!gWindow)
    {
        LogLineF(L"tray: window creation failed err=%lu", GetLastError());
        return false;
    }

    // The watcher runs elevated, so the shell (medium integrity) needs explicit
    // permission to post the click and the shell-restart notice.
    SetLastError(0);
    const BOOL perWindow = ChangeWindowMessageFilterEx(gWindow, kCallbackMessage, MSGFLT_ALLOW, nullptr);
    const DWORD filterError = GetLastError();
    const BOOL legacy = ChangeWindowMessageFilter(kCallbackMessage, MSGFLT_ADD);
    gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (gTaskbarCreated) ChangeWindowMessageFilterEx(gWindow, gTaskbarCreated, MSGFLT_ALLOW, nullptr);
    LogLineF(L"tray: filter perWindow=%d err=%lu legacy=%d elevated=%d", perWindow ? 1 : 0, filterError,
             legacy ? 1 : 0, IsElevated() ? 1 : 0);

    gIconHandle = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!gIconHandle) gIconHandle = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    gIcon = {};
    gIcon.cbSize = sizeof(gIcon);
    gIcon.hWnd = gWindow;
    gIcon.uID = 1;
    gIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gIcon.uCallbackMessage = kCallbackMessage;
    gIcon.hIcon = gIconHandle;
    wcscpy_s(gIcon.szTip, tooltip.c_str());

    return AddIcon();
}

void tray::Stop()
{
    if (gWindow)
    {
        Shell_NotifyIconW(NIM_DELETE, &gIcon);
        LogLineF(L"tray: removed");
        DestroyWindow(gWindow);
        gWindow = nullptr;
    }
    if (gIconHandle)
    {
        DestroyIcon(gIconHandle);
        gIconHandle = nullptr;
    }
}
