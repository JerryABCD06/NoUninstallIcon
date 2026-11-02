// Applies the hidden attribute and tells the shell about the change.
#pragma once

#include "common.h"

enum class ApplyStatus
{
    Ok,
    Missing,
    Failed,
};

ApplyStatus SetShortcutHidden(const std::wstring& path, bool hidden);
void NotifyShellItem(const std::wstring& path);
bool ReadAttributes(const std::wstring& path, DWORD& attributes);
