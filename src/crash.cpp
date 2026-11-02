#include "crash.h"

#include "common.h"
#include "util.h"

#include <dbghelp.h>

namespace {

// Resolved with plain environment lookups: the crash handler must not touch
// initialisation that may itself be in progress.
std::wstring DumpDirectory()
{
    wchar_t buffer[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"NOUI_DATA_DIR", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return buffer;

    length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::wstring(buffer) + L"\\NoUninstallIcon";
    return L".";
}

void WriteMarker(const wchar_t* name, const std::wstring& text)
{
    std::wstring directory = DumpDirectory();
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring path = directory + L"\\" + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::string utf8 = Narrow(text);
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
{
    wchar_t path[MAX_PATH * 2]{};
    SYSTEMTIME now{};
    GetLocalTime(&now);

    std::wstring directory = DumpDirectory();
    CreateDirectoryW(directory.c_str(), nullptr);
    swprintf_s(path, L"%s\\crash-%04u%02u%02u-%02u%02u%02u.dmp", directory.c_str(), now.wYear, now.wMonth,
               now.wDay, now.wHour, now.wMinute, now.wSecond);

    const unsigned code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION details{};
        details.ThreadId = GetCurrentThreadId();
        details.ExceptionPointers = info;
        details.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &details, nullptr,
                          nullptr);
        CloseHandle(file);
    }

    WriteMarker(L"crash.txt", Fmt(L"Unhandled exception 0x%08X at %p\r\nDump: %s\r\n", code, address, path));
    return EXCEPTION_EXECUTE_HANDLER;
}

void OnTerminate()
{
    // An uncaught C++ exception (std::out_of_range, std::bad_alloc, ...).
    WriteMarker(L"crash.txt", L"std::terminate was called (uncaught C++ exception)\r\n");
    std::abort();
}

}  // namespace

void crash::Install()
{
    SetUnhandledExceptionFilter(OnUnhandledException);
    std::set_terminate(OnTerminate);
}
