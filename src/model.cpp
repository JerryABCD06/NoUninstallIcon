#include "model.h"

#include "i18n.h"
#include "util.h"

namespace {

int CompareNatural(const std::wstring& a, const std::wstring& b)
{
    int result = CompareStringEx(i18n::LocaleName(), SORT_DIGITSASNUMBERS, a.c_str(), -1, b.c_str(), -1,
                                 nullptr, nullptr, 0);
    if (result == 0) return 0;
    return result - CSTR_EQUAL;
}

bool ContainsI(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty()) return true;
    return StrStrIW(haystack.c_str(), needle.c_str()) != nullptr;
}

void SortGroup(std::vector<int>& group, const std::vector<ShortcutItem>& items)
{
    std::stable_sort(group.begin(), group.end(), [&items](int left, int right) {
        const auto& a = items[left];
        const auto& b = items[right];
        int compared = CompareNatural(a.sortName, b.sortName);
        if (compared != 0) return compared < 0;
        compared = CompareNatural(a.displayName, b.displayName);
        if (compared != 0) return compared < 0;
        return CompareStringOrdinal(a.key.c_str(), -1, b.key.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
}

}  // namespace

void Model::Recompute()
{
    managedCount = 0;
    externalHiddenCount = 0;
    for (const auto& item : items)
    {
        if (item.managed)
            ++managedCount;
        else if (item.hiddenOnDisk)
            ++externalHiddenCount;
    }

    std::vector<int> uninstall;
    std::vector<int> other;
    uninstall.reserve(items.size());
    other.reserve(items.size());

    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const auto& item = items[i];
        if (!query.empty())
        {
            if (!ContainsI(item.displayName, query) && !ContainsI(item.target, query) &&
                !ContainsI(item.path, query))
                continue;
        }
        (item.isUninstall ? uninstall : other).push_back(i);
    }

    SortGroup(uninstall, items);
    SortGroup(other, items);

    uninstallTotal = static_cast<int>(uninstall.size());
    otherTotal = static_cast<int>(other.size());

    rows.assign(uninstall.begin(), uninstall.end());
    rows.insert(rows.end(), other.begin(), other.end());
}

const ShortcutItem* Model::At(int row) const
{
    if (row < 0 || row >= static_cast<int>(rows.size())) return nullptr;
    return &items[rows[row]];
}

bool Model::IsUninstallRow(int row) const
{
    const ShortcutItem* item = At(row);
    return item && item->isUninstall;
}

bool Model::IsGroupStart(int row) const
{
    if (row <= 0) return true;
    return IsUninstallRow(row) != IsUninstallRow(row - 1);
}
