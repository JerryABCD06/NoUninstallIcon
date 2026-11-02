// String, path, registry and logging helpers.
#pragma once

#include "common.h"

std::wstring Widen(std::string_view text);
std::string Narrow(std::wstring_view text);

// printf-style formatting that always returns a std::wstring.
std::wstring Fmt(const wchar_t* format, ...);

// Returns a reference to a cached string; the buffer is valid for the lifetime
// of the process, so it is safe to hand to shell APIs without copying.
const std::wstring& GetExePath();
std::wstring GetExeDir();
std::wstring GetDataDir();
// True when a "portable" marker next to the executable selected the data folder.
bool IsPortableMode();
std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf);
std::wstring FileNameOf(const std::wstring& path);
std::wstring ExtensionOf(const std::wstring& path);

bool EnsureDirectory(const std::wstring& path);
bool ReadWholeFile(const std::wstring& path, std::string& out);
bool WriteWholeFileAtomic(const std::wstring& path, std::string_view data);

bool RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD& out);
bool RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, std::wstring& out);
bool RegWriteDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value);

bool IsSystemDarkMode();
DWORD GetAccentColorBgra();
bool IsElevated();

void LogLine(const std::wstring& text);
void LogLineF(const wchar_t* format, ...);

std::wstring ToLower(std::wstring text);
std::wstring Trim(const std::wstring& text);
bool StartsWithI(std::wstring_view text, std::wstring_view prefix);
bool EndsWithI(std::wstring_view text, std::wstring_view suffix);
bool EqualsI(std::wstring_view a, std::wstring_view b);

// Absolute path used as the identity of a shortcut in the state database.
std::wstring NormalizePathKey(const std::wstring& path);

std::string WideToUtf8(std::wstring_view text);
