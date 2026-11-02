#include "util.h"

#include <cstdarg>
#include <share.h>
#include <sys/stat.h>

std::wstring Widen(std::string_view text)
{
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
        count = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    UINT cp = (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0) ? CP_UTF8 : CP_ACP;
    MultiByteToWideChar(cp, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string Narrow(std::wstring_view text)
{
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::string WideToUtf8(std::wstring_view text) { return Narrow(text); }

std::wstring Fmt(const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    wchar_t stackBuffer[1024];
    int written = _vsnwprintf_s(stackBuffer, _TRUNCATE, format, args);
    va_end(args);
    if (written >= 0) return std::wstring(stackBuffer, static_cast<size_t>(written));

    va_start(args, format);
    int needed = _vscwprintf(format, args);
    va_end(args);
    if (needed <= 0) return {};
    std::wstring result(static_cast<size_t>(needed) + 1, L'\0');
    va_start(args, format);
    _vsnwprintf_s(result.data(), result.size(), _TRUNCATE, format, args);
    va_end(args);
    result.resize(static_cast<size_t>(needed));
    return result;
}

const std::wstring& GetExePath()
{
    static std::wstring cached;
    static bool ready = false;
    static const std::wstring kEmpty;

    if (ready) return cached;
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            ready = true;
            return kEmpty;
        }
        if (written < buffer.size() - 1)
        {
            buffer.resize(written);
            cached = std::move(buffer);
            ready = true;
            return cached;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetExeDir()
{
    std::wstring path = GetExePath();
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring GetDataDir()
{
    // NOUI_DATA_DIR lets the tool run in portable mode and keeps tests isolated.
    wchar_t overridePath[MAX_PATH]{};
    DWORD overrideLength = GetEnvironmentVariableW(L"NOUI_DATA_DIR", overridePath, MAX_PATH);
    if (overrideLength > 0 && overrideLength < MAX_PATH) return overridePath;

    PWSTR localAppData = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)) && localAppData)
        result = std::wstring(localAppData) + L"\\" + kAppDataFolder;
    CoTaskMemFree(localAppData);
    if (result.empty()) result = GetExeDir() + L"\\" + kAppDataFolder;
    return result;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf)
{
    if (dir.empty()) return leaf;
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + leaf;
    return dir + L"\\" + leaf;
}

std::wstring FileNameOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ExtensionOf(const std::wstring& path)
{
    std::wstring name = FileNameOf(path);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    return name.substr(dot);
}

bool EnsureDirectory(const std::wstring& path)
{
    if (path.empty()) return false;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 64ll * 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
    CloseHandle(file);
    return ok;
}

bool WriteWholeFileAtomic(const std::wstring& path, std::string_view data)
{
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size());
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok)
    {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD& out)
{
    DWORD type = 0, size = sizeof(DWORD), value = 0;
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_DWORD, &type, &value, &size) != ERROR_SUCCESS) return false;
    out = value;
    return true;
}

bool RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, std::wstring& out)
{
    DWORD type = 0, size = 0;
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) return false;
    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, buffer.data(), &size) != ERROR_SUCCESS) return false;
    buffer.resize(wcslen(buffer.c_str()));
    out = std::move(buffer);
    return true;
}

bool RegWriteDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS status = RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool IsSystemDarkMode()
{
    DWORD light = 1;
    if (RegReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", light))
        return light == 0;
    return false;
}

DWORD GetAccentColorBgra()
{
    DWORD packed = 0;  // 0xAABBGGRR as stored under DWM
    if (RegReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", packed))
    {
        BYTE a = static_cast<BYTE>((packed >> 24) & 0xFF);
        BYTE b = static_cast<BYTE>((packed >> 16) & 0xFF);
        BYTE g = static_cast<BYTE>((packed >> 8) & 0xFF);
        BYTE r = static_cast<BYTE>(packed & 0xFF);
        return (static_cast<DWORD>(a) << 24) | (static_cast<DWORD>(r) << 16) | (static_cast<DWORD>(g) << 8) | b;
    }
    return 0xFFB85F00;  // 0x005FB8 as 0xAARRGGBB
}

bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, size, &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

void LogLine(const std::wstring& text)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);

    static bool prepared = false;
    static std::wstring logPath;
    if (!prepared)
    {
        std::wstring dir = GetDataDir();
        EnsureDirectory(dir);
        logPath = JoinPath(dir, L"log.txt");
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &info) &&
            info.nFileSizeHigh == 0 && info.nFileSizeLow > 1024 * 1024)
        {
            DeleteFileW(logPath.c_str());
        }
        prepared = true;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wstring line = Fmt(L"%04u-%02u-%02u %02u:%02u:%02u  %s\r\n", now.wYear, now.wMonth, now.wDay,
                            now.wHour, now.wMinute, now.wSecond, text.c_str());
    std::string utf8 = Narrow(line);
    FILE* file = nullptr;
    if (_wfopen_s(&file, logPath.c_str(), L"ab") == 0 && file)
    {
        fwrite(utf8.data(), 1, utf8.size(), file);
        fclose(file);
    }
}

void LogLineF(const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    wchar_t buffer[2048];
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    LogLine(buffer);
}

std::wstring ToLower(std::wstring text)
{
    if (!text.empty()) CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && iswspace(text[begin])) ++begin;
    while (end > begin && iswspace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

bool StartsWithI(std::wstring_view text, std::wstring_view prefix)
{
    if (text.size() < prefix.size()) return false;
    return CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

bool EndsWithI(std::wstring_view text, std::wstring_view suffix)
{
    if (text.size() < suffix.size()) return false;
    return CompareStringOrdinal(text.data() + (text.size() - suffix.size()), static_cast<int>(suffix.size()),
                                suffix.data(), static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

bool EqualsI(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring NormalizePathKey(const std::wstring& path)
{
    if (path.empty()) return {};
    std::wstring full(MAX_PATH * 2, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (written == 0)
    {
        full = path;
    }
    else if (written >= full.size())
    {
        full.resize(written + 1);
        GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    }
    full.resize(wcslen(full.c_str()));
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    return ToLower(full);
}
