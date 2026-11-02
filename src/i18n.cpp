#include "i18n.h"

#include "util.h"

namespace i18n {
namespace {

struct LanguageDef
{
    const wchar_t* code;
    int resourceId;
};

// Resource ids are allocated per language file; the table is discovered at
// startup so adding a language is a new .lang plus one RCDATA line.
constexpr int kFirstLanguageId = 101;
constexpr int kLastLanguageId = 160;

std::vector<LanguageDef> gDefinitions;

using Table = std::unordered_map<std::wstring, std::wstring>;

Table gEnglish;
Table gCurrent;
std::wstring gCurrentCode = L"en";
std::vector<LanguageInfo> gAvailable;
std::vector<std::wstring> gDefinitionCodes;
std::wstring gLocaleName = L"en-US";
std::wstring gNativeName;

std::wstring Unescape(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t ch = text[i];
        if (ch != L'\\' || i + 1 >= text.size())
        {
            out.push_back(ch);
            continue;
        }
        wchar_t next = text[++i];
        switch (next)
        {
        case L'n': out.push_back(L'\n'); break;
        case L't': out.push_back(L'\t'); break;
        case L'r': out.push_back(L'\r'); break;
        case L'\\': out.push_back(L'\\'); break;
        case L'u':
        {
            if (i + 4 < text.size())
            {
                wchar_t hex[5] = {text[i + 1], text[i + 2], text[i + 3], text[i + 4], 0};
                out.push_back(static_cast<wchar_t>(wcstoul(hex, nullptr, 16)));
                i += 4;
            }
            else
            {
                out.push_back(L'\\');
                out.push_back(next);
            }
            break;
        }
        default:
            out.push_back(L'\\');
            out.push_back(next);
            break;
        }
    }
    return out;
}

bool LoadResource(int resourceId, Table& table)
{
    HRSRC handle = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!handle) return false;
    HGLOBAL loaded = LoadResource(nullptr, handle);
    if (!loaded) return false;
    const char* data = static_cast<const char*>(LockResource(loaded));
    DWORD size = SizeofResource(nullptr, handle);
    if (!data || size == 0) return false;

    std::string_view raw(data, size);
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF)
        raw.remove_prefix(3);

    std::wstring content = Widen(raw);
    size_t pos = 0;
    while (pos <= content.size())
    {
        size_t end = content.find(L'\n', pos);
        if (end == std::wstring::npos) end = content.size();
        std::wstring line = content.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#') continue;
        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(trimmed.substr(0, eq));

        // Only leading whitespace is stripped: a value may legitimately end
        // with a space ("Author: ") and that space has to survive.
        std::wstring rawValue = trimmed.substr(eq + 1);
        size_t firstNonSpace = rawValue.find_first_not_of(L" \t");
        rawValue = firstNonSpace == std::wstring::npos ? std::wstring() : rawValue.substr(firstNonSpace);
        std::wstring value = Unescape(rawValue);
        if (!key.empty()) table[key] = value;
    }
    return true;
}

std::wstring SystemLanguageCode()
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return L"en";
    std::wstring locale = name;
    std::wstring lower = ToLower(locale);
    if (StartsWithI(lower, L"zh"))
    {
        if (lower.find(L"-tw") != std::wstring::npos || lower.find(L"-hk") != std::wstring::npos ||
            lower.find(L"-mo") != std::wstring::npos)
            return L"zh-Hant";
        return L"zh-Hans";
    }
    size_t dash = lower.find(L'-');
    const std::wstring primary = dash == std::wstring::npos ? lower : lower.substr(0, dash);
    for (const auto& code : gDefinitionCodes)
        if (EqualsI(code, primary)) return code;
    if (StartsWithI(lower, L"ja")) return L"ja";
    return L"en";
}

bool ApplyLanguage(const std::wstring& code)
{
    Table loaded;
    int resourceId = 0;
    for (auto& def : gDefinitions)
    {
        if (EqualsI(def.code, code))
        {
            resourceId = def.resourceId;
            break;
        }
    }
    if (resourceId == 0) return false;
    if (!LoadResource(resourceId, loaded)) return false;

    gCurrent = std::move(loaded);
    gCurrentCode = code;

    auto native = gCurrent.find(L"lang.native_name");
    gNativeName = native != gCurrent.end() ? native->second : code;

    std::wstring lower = ToLower(code);
    if (lower == L"zh-hans") gLocaleName = L"zh-CN";
    else if (lower == L"zh-hant") gLocaleName = L"zh-TW";
    else if (lower == L"ja") gLocaleName = L"ja-JP";
    else gLocaleName = L"en-US";
    return true;
}

void BuildLanguageList()
{
    if (!gAvailable.empty()) return;
    for (auto& def : gDefinitions)
    {
        Table temp;
        if (!LoadResource(def.resourceId, temp)) continue;
        LanguageInfo info;
        info.code = def.code;
        auto native = temp.find(L"lang.native_name");
        info.nativeName = native != temp.end() ? native->second : def.code;
        gAvailable.push_back(std::move(info));
    }
}

}  // namespace

void Init(const std::wstring& preferredCode)
{
    LoadResource(kFirstLanguageId, gEnglish);

    // Discover every embedded language by probing its resource id.
    for (int id = kFirstLanguageId; id <= kLastLanguageId; ++id)
    {
        Table probe;
        if (!LoadResource(id, probe)) continue;
        auto code = probe.find(L"lang.code");
        if (code == probe.end() || code->second.empty()) continue;
        gDefinitions.push_back({L"", id});
        gDefinitionCodes.push_back(code->second);
    }

    BuildLanguageList();

    std::wstring wanted = preferredCode.empty() ? SystemLanguageCode() : preferredCode;
    if (!ApplyLanguage(wanted)) ApplyLanguage(L"en");
}

bool SetLanguage(const std::wstring& code)
{
    if (EqualsI(code, gCurrentCode)) return true;
    return ApplyLanguage(code);
}

const std::wstring& CurrentCode() { return gCurrentCode; }
const std::vector<LanguageInfo>& Available() { return gAvailable; }
const wchar_t* LocaleName() { return gLocaleName.c_str(); }

const wchar_t* T(const wchar_t* key)
{
    auto it = gCurrent.find(key);
    if (it != gCurrent.end()) return it->second.c_str();
    auto fallback = gEnglish.find(key);
    if (fallback != gEnglish.end()) return fallback->second.c_str();
    return key;
}

std::wstring Substitute(const std::wstring& pattern, std::initializer_list<std::wstring> args)
{
    std::wstring out = pattern;
    int index = 0;
    for (const auto& arg : args)
    {
        std::wstring token = L"{" + std::to_wstring(index++) + L"}";
        size_t pos = 0;
        while ((pos = out.find(token, pos)) != std::wstring::npos)
        {
            out.replace(pos, token.size(), arg);
            pos += arg.size();
        }
    }
    return out;
}

std::wstring Tf(const wchar_t* key, std::initializer_list<std::wstring> args)
{
    return Substitute(T(key), args);
}

}  // namespace i18n
