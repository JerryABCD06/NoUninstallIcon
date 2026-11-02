// Data model shared by the scanner, the state store and the UI.
#pragma once

#include "common.h"

enum class ShortcutScope
{
    User,
    Machine,
};

enum class ShortcutKind
{
    Link,
    Url,
};

enum class AutostartMode
{
    Off,
    Once,
    Watch,
};

struct ShortcutItem
{
    std::wstring path;         // absolute path on disk
    std::wstring key;          // normalised identity used in the state file
    std::wstring displayName;  // file name without extension
    std::wstring sortName;     // display name with a leading uninstall word removed
    std::wstring target;       // resolved target path, or the URL
    std::wstring arguments;
    std::wstring description;
    ShortcutScope scope = ShortcutScope::User;
    ShortcutKind kind = ShortcutKind::Link;
    bool hiddenOnDisk = false;   // FILE_ATTRIBUTE_HIDDEN is set right now
    bool managed = false;        // recorded by this tool
    bool desiredHidden = false;  // the recorded intent
    bool isUninstall = false;
};

struct Model
{
    std::vector<ShortcutItem> items;
    std::vector<int> rows;  // filtered + grouped + sorted item indices
    std::wstring query;
    std::wstring statusText;
    bool loading = true;
    bool scanFailed = false;

    int managedCount = 0;
    int externalHiddenCount = 0;
    int uninstallTotal = 0;
    int otherTotal = 0;

    void Recompute();
    int RowCount() const { return static_cast<int>(rows.size()); }
    const ShortcutItem* At(int row) const;
    bool IsUninstallRow(int row) const;
    bool IsGroupStart(int row) const;
};
