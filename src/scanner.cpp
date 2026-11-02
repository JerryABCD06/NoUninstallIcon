#include "scanner.h"

#include "i18n.h"
#include "util.h"

#include <set>

namespace {

const wchar_t* kUninstallWords[] = {
    L"卸载", L"卸載", L"移除", L"删除", L"刪除", L"解除安装", L"解除安裝",
    L"アンインストール", L"削除", L"제거", L"삭제",
    L"uninstall", L"uninstaller", L"uninst", L"unwise", L"deinstall", L"deinstallieren",
    L"entfernen", L"desinstaller", L"désinstaller", L"supprimer", L"desinstalar", L"eliminar",
    L"disinstallare", L"rimuovi", L"удалить", L"удаление", L"verwijderen", L"avinstallera",
    L"afinstalleren", L"poista", L"odinstalovat", L"odinstaluj", L"deinstalacja", L"eltavolitas",
    L"eltávolítás", L"remove", L"deinstaller",
};

bool IsCjk(wchar_t ch)
{
    return (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xAC00 && ch <= 0xD7AF) ||
           (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0x3040 && ch <= 0x30FF) ||
           (ch >= 0x0400 && ch <= 0x04FF);
}

bool ContainsWord(const std::wstring& haystackLower, const std::wstring& wordLower)
{
    if (wordLower.empty()) return false;
    if (IsCjk(wordLower[0]))
        return haystackLower.find(wordLower) != std::wstring::npos;

    size_t pos = 0;
    while ((pos = haystackLower.find(wordLower, pos)) != std::wstring::npos)
    {
        bool leftOk = pos == 0 || !iswalnum(haystackLower[pos - 1]);
        size_t end = pos + wordLower.size();
        bool rightOk = end >= haystackLower.size() || !iswalnum(haystackLower[end]);
        if (leftOk && rightOk) return true;
        pos += 1;
    }
    return false;
}

bool EndsWithWord(const std::wstring& textLower, const std::wstring& wordLower)
{
    if (textLower.size() < wordLower.size()) return false;
    size_t offset = textLower.size() - wordLower.size();
    if (textLower.compare(offset, wordLower.size(), wordLower) != 0) return false;
    if (offset == 0) return true;
    if (!wordLower.empty() && IsCjk(wordLower[0])) return true;
    return !iswalnum(textLower[offset - 1]);
}

// Leading decorative punctuation (for example CJK quotation marks) should not
// decide where an entry lands in the list.
std::wstring StripDecorations(const std::wstring& text)
{
    static const std::wstring decorations = L"\u300A\u300B\u300C\u300D\u300E\u300F\u201C\u201D\u2018\u2019\"'";
    size_t start = 0;
    while (start < text.size() &&
           (text[start] == L' ' || text[start] == L'\t' || decorations.find(text[start]) != std::wstring::npos))
        ++start;
    if (start == 0) return text;
    if (start >= text.size()) return text;
    return text.substr(start);
}

void TrimSeparators(std::wstring& text)
{
    size_t begin = 0, end = text.size();
    auto isSeparator = [](wchar_t ch) {
        return iswspace(ch) || ch == L'-' || ch == L':' || ch == L'(' || ch == L')' || ch == L'.';
    };
    while (begin < end && isSeparator(text[begin])) ++begin;
    while (end > begin && isSeparator(text[end - 1])) --end;
    text = text.substr(begin, end - begin);
}

bool IsLinkExtension(const std::wstring& path)
{
    return EndsWithI(path, L".lnk");
}

bool IsUrlExtension(const std::wstring& path)
{
    return EndsWithI(path, L".url");
}

std::wstring ResolveLinkTarget(IShellLinkW* link, const std::wstring& path)
{
    wchar_t buffer[2048]{};
    WIN32_FIND_DATAW data{};
    if (SUCCEEDED(link->GetPath(buffer, ARRAYSIZE(buffer), &data, SLGP_RAWPATH)) && buffer[0] != L'\0')
        return buffer;

    // Advertised (MSI) shortcuts do not expose a normal target path; use the
    // parsing path stored in the property store instead.
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_READWRITE, IID_PPV_ARGS(store.GetAddressOf()))))
    {
        PROPVARIANT value{};
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Link_TargetParsingPath, &value)) && value.vt == VT_LPWSTR)
        {
            std::wstring result = value.pwszVal;
            PropVariantClear(&value);
            return result;
        }
        PropVariantClear(&value);
    }
    return {};
}

std::wstring ResolveUrlTarget(const std::wstring& path)
{
    wchar_t buffer[2048]{};
    GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", buffer, ARRAYSIZE(buffer), path.c_str());
    return buffer;
}

std::wstring ReadLinkString(IShellLinkW* link, bool arguments)
{
    wchar_t buffer[2048]{};
    HRESULT hr = arguments ? link->GetArguments(buffer, ARRAYSIZE(buffer))
                           : link->GetDescription(buffer, ARRAYSIZE(buffer));
    return SUCCEEDED(hr) ? std::wstring(buffer) : std::wstring();
}

void EnumerateDirectory(const std::wstring& directory, ShortcutScope scope, int depth, ScanOutcome& outcome)
{
    if (depth > 12) return;

    std::wstring pattern = JoinPath(directory, L"*");
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE) return;

    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring fullPath = JoinPath(directory, name);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (EqualsI(name, L"Startup")) continue;
            EnumerateDirectory(fullPath, scope, depth + 1, outcome);
            continue;
        }

        bool isLink = IsLinkExtension(name);
        bool isUrl = IsUrlExtension(name);
        if (!isLink && !isUrl) continue;

        ShortcutItem item;
        item.path = fullPath;
        item.key = NormalizePathKey(fullPath);
        item.scope = scope;
        item.kind = isLink ? ShortcutKind::Link : ShortcutKind::Url;
        item.displayName = name.substr(0, name.size() - 4);
        item.hiddenOnDisk = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        item.target = isLink ? std::wstring() : ResolveUrlTarget(fullPath);

        if (isLink)
        {
            ComPtr<IShellLinkW> link;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(link.GetAddressOf()))))
            {
                ComPtr<IPersistFile> file;
                if (SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(fullPath.c_str(), STGM_READ)))
                {
                    item.target = ResolveLinkTarget(link.Get(), fullPath);
                    item.arguments = ReadLinkString(link.Get(), true);
                    item.description = ReadLinkString(link.Get(), false);
                }
            }
        }

        if (item.target.empty()) ++outcome.skipped;

        item.isUninstall = LooksLikeUninstall(item.displayName, item.target, item.arguments);
        item.sortName = MakeSortName(item.displayName);

        outcome.items.push_back(std::move(item));
    } while (FindNextFileW(search, &data));

    FindClose(search);
}

}  // namespace

bool LooksLikeUninstall(const std::wstring& displayName, const std::wstring& target, const std::wstring& arguments)
{
    std::wstring nameLower = ToLower(displayName);
    for (const wchar_t* word : kUninstallWords)
    {
        std::wstring lowered = ToLower(word);
        if (ContainsWord(nameLower, lowered)) return true;
    }

    std::wstring fileName = ToLower(FileNameOf(target));
    if (fileName.rfind(L"unins", 0) == 0 || fileName.rfind(L"uninstall", 0) == 0 ||
        fileName.rfind(L"unwise", 0) == 0 || fileName.rfind(L"uninst", 0) == 0)
        return true;

    std::wstring argsLower = ToLower(arguments);
    if (argsLower.find(L"/uninstall") != std::wstring::npos || argsLower.find(L"/remove") != std::wstring::npos)
        return true;
    if (fileName.rfind(L"msiexec", 0) == 0 && (argsLower.find(L"/x") != std::wstring::npos ||
                                               argsLower.find(L"/uninstall") != std::wstring::npos))
        return true;

    return false;
}

std::wstring MakeSortName(const std::wstring& displayName)
{
    std::wstring lowered = ToLower(displayName);

    // A leading uninstall word wins, but only if something is left over.
    for (const wchar_t* word : kUninstallWords)
    {
        std::wstring loweredWord = ToLower(word);
        if (lowered.rfind(loweredWord, 0) != 0) continue;
        std::wstring remainder = displayName.substr(loweredWord.size());
        TrimSeparators(remainder);
        return remainder.empty() ? StripDecorations(displayName) : StripDecorations(remainder);
    }

    for (const wchar_t* word : kUninstallWords)
    {
        std::wstring loweredWord = ToLower(word);
        if (!EndsWithWord(lowered, loweredWord)) continue;
        std::wstring remainder = displayName.substr(0, displayName.size() - loweredWord.size());
        TrimSeparators(remainder);
        if (!remainder.empty()) return StripDecorations(remainder);
    }
    return StripDecorations(displayName);
}

void ScanStartMenu(ScanOutcome& outcome)
{
    outcome.items.clear();
    outcome.skipped = 0;

    struct Root
    {
        KNOWNFOLDERID id;
        ShortcutScope scope;
    };
    // NOUI_SCAN_ROOT points the scanner at a fixture folder for tests.
    wchar_t fixture[MAX_PATH]{};
    DWORD fixtureLength = GetEnvironmentVariableW(L"NOUI_SCAN_ROOT", fixture, MAX_PATH);
    if (fixtureLength > 0 && fixtureLength < MAX_PATH)
    {
        EnumerateDirectory(fixture, ShortcutScope::User, 0, outcome);
        return;
    }

    const Root roots[] = {{FOLDERID_Programs, ShortcutScope::User}, {FOLDERID_CommonPrograms, ShortcutScope::Machine}};

    std::wstring selfPath = NormalizePathKey(GetExePath());

    for (const Root& root : roots)
    {
        PWSTR folder = nullptr;
        if (FAILED(SHGetKnownFolderPath(root.id, KF_FLAG_DEFAULT, nullptr, &folder)) || !folder) continue;
        std::wstring path = folder;
        CoTaskMemFree(folder);
        EnumerateDirectory(path, root.scope, 0, outcome);
    }

    // Never let the tool hide its own shortcut.
    outcome.items.erase(std::remove_if(outcome.items.begin(), outcome.items.end(),
                                       [&](const ShortcutItem& item) {
                                           return NormalizePathKey(item.target) == selfPath;
                                       }),
                        outcome.items.end());
}
