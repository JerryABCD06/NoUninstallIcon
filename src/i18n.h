// Runtime localisation. Every visible string comes from res/lang/*.lang,
// which is embedded into the executable as RCDATA.
#pragma once

#include "common.h"

namespace i18n {

struct LanguageInfo
{
    std::wstring code;
    std::wstring nativeName;
};

// preferredCode may be empty, in which case the system UI language is used.
void Init(const std::wstring& preferredCode);
bool SetLanguage(const std::wstring& code);

const std::wstring& CurrentCode();
const std::vector<LanguageInfo>& Available();
const wchar_t* LocaleName();

// Returns the requested key, falling back to English and then to the key itself.
const wchar_t* T(const wchar_t* key);

std::wstring Tf(const wchar_t* key, std::initializer_list<std::wstring> args);
std::wstring Substitute(const std::wstring& pattern, std::initializer_list<std::wstring> args);

}  // namespace i18n
