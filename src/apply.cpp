#include "apply.h"

#include "util.h"

bool ReadAttributes(const std::wstring& path, DWORD& attributes)
{
    attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
}

ApplyStatus SetShortcutHidden(const std::wstring& path, bool hidden)
{
    DWORD attributes = 0;
    if (!ReadAttributes(path, attributes)) return ApplyStatus::Missing;

    DWORD desired = attributes;
    if (hidden)
        desired |= FILE_ATTRIBUTE_HIDDEN;
    else
        desired &= ~FILE_ATTRIBUTE_HIDDEN;

    if (desired != attributes)
    {
        // Clear the read-only flag if it is in the way, then put it back.
        bool wasReadOnly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (wasReadOnly && !SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY))
            return ApplyStatus::Failed;
        if (!SetFileAttributesW(path.c_str(), desired))
        {
            if (wasReadOnly) SetFileAttributesW(path.c_str(), attributes);
            LogLineF(L"SetFileAttributes failed for %s (error %lu)", path.c_str(), GetLastError());
            return ApplyStatus::Failed;
        }
        if (wasReadOnly && (desired & FILE_ATTRIBUTE_READONLY) == 0)
            SetFileAttributesW(path.c_str(), desired | FILE_ATTRIBUTE_READONLY);
    }

    DWORD check = 0;
    if (!ReadAttributes(path, check)) return ApplyStatus::Missing;
    return ((check & FILE_ATTRIBUTE_HIDDEN) != 0) == hidden ? ApplyStatus::Ok : ApplyStatus::Failed;
}

void NotifyShellItem(const std::wstring& path)
{
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, path.c_str(), nullptr);
}
