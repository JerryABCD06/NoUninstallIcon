// Start menu enumeration and uninstall-entry classification.
#pragma once

#include "common.h"
#include "model.h"

struct ScanOutcome
{
    std::vector<ShortcutItem> items;
    int skipped = 0;
};

// Walks both Start menu trees (per-user and all-users) and resolves every
// .lnk and .url into a ShortcutItem.
void ScanStartMenu(ScanOutcome& outcome);

// True when the display name or target looks like an uninstall entry.
bool LooksLikeUninstall(const std::wstring& displayName, const std::wstring& target,
                        const std::wstring& arguments);

// Removes a leading or trailing uninstall word so sorting uses the app name.
std::wstring MakeSortName(const std::wstring& displayName);
