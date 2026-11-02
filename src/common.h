// Shared includes and small utilities used by every translation unit.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wincodec.h>
#include <dwmapi.h>

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

// Window messages that are private to this application.
constexpr UINT WM_APP_SCAN_DONE = WM_APP + 1;
constexpr UINT WM_APP_APPLY_DONE = WM_APP + 2;

constexpr wchar_t kAppRegKey[] = L"Software\\NoUninstallIcon";
constexpr wchar_t kAppDataFolder[] = L"NoUninstallIcon";
constexpr wchar_t kTaskName[] = L"NoUninstallIcon-AutoHide";

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

inline bool Succeeded(HRESULT hr) { return hr >= 0; }
