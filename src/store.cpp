#include "store.h"

#include "util.h"

#include <ctime>

namespace {

constexpr wchar_t kHeader[] = L"NOUI1";
constexpr wchar_t kFile[] = L"state.dat";

std::wstring Escape(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text)
    {
        switch (ch)
        {
        case L'\\': out += L"\\\\"; break;
        case L'\t': out += L"\\t"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::wstring Unescape(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != L'\\' || i + 1 >= text.size())
        {
            out.push_back(text[i]);
            continue;
        }
        wchar_t next = text[++i];
        switch (next)
        {
        case L'\\': out.push_back(L'\\'); break;
        case L't': out.push_back(L'\t'); break;
        case L'n': out.push_back(L'\n'); break;
        case L'r': out.push_back(L'\r'); break;
        default:
            out.push_back(L'\\');
            out.push_back(next);
            break;
        }
    }
    return out;
}

std::vector<std::wstring> Split(const std::wstring& line, wchar_t separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;)
    {
        size_t pos = line.find(separator, start);
        if (pos == std::wstring::npos)
        {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

long long ToInt64(const std::wstring& text)
{
    return static_cast<long long>(_wcstoi64(text.c_str(), nullptr, 10));
}

DWORD ToHex(const std::wstring& text)
{
    return static_cast<DWORD>(wcstoul(text.c_str(), nullptr, 16));
}

// Parses one state file. Returns false when the file is missing or damaged so
// the caller can fall back to the backup copy.
bool ParseStateFile(const std::wstring& file, std::vector<StoredRecord>& records, AppSettings& settings)
{
    std::string raw;
    bool sawSetting = false;
    bool sawAutostart = false;
    if (!ReadWholeFile(file, raw)) return false;

    std::wstring content = Widen(raw);
    if (!content.empty() && content.front() == 0xFEFF) content.erase(content.begin());

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= content.size())
    {
        size_t end = content.find(L'\n', start);
        if (end == std::wstring::npos) end = content.size();
        std::wstring line = content.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }

    if (lines.empty() || lines[0] != kHeader) return false;

    std::vector<StoredRecord> parsed;
    AppSettings parsedSettings;

    for (size_t i = 1; i < lines.size(); ++i)
    {
        if (lines[i].empty()) continue;
        auto fields = Split(lines[i], L'\t');
        if (fields.empty()) continue;

        if (fields[0] == L"S" && fields.size() >= 3)
        {
            sawSetting = true;
            if (fields[1] == L"autostart") sawAutostart = true;
            if (fields[1] == L"language") parsedSettings.language = Unescape(fields[2]);
            else if (fields[1] == L"autostart")
            {
                if (fields[2] == L"once") parsedSettings.autostart = AutostartMode::Once;
                else if (fields[2] == L"watch") parsedSettings.autostart = AutostartMode::Watch;
    else if (fields[2] == L"silent") parsedSettings.autostart = AutostartMode::Silent;
                else parsedSettings.autostart = AutostartMode::Off;
            }
            continue;
        }

        if (fields[0] != L"R" || fields.size() < 9) continue;
        StoredRecord record;
        record.key = Unescape(fields[1]);
        record.path = Unescape(fields[2]);
        record.originalAttributes = ToHex(fields[3]);
        record.desiredHidden = fields[4] == L"1";
        record.displayName = Unescape(fields[5]);
        record.target = Unescape(fields[6]);
        record.firstSeen = ToInt64(fields[7]);
        record.lastApplied = ToInt64(fields[8]);
        if (!record.key.empty() && !record.path.empty()) parsed.push_back(std::move(record));
    }

    if (!sawAutostart) return false;  // a state without its autostart line was cut short
    records = std::move(parsed);
    settings = parsedSettings;
    return true;
}

}  // namespace

std::wstring StateFilePath()
{
    return JoinPath(GetDataDir(), kFile);
}

namespace { constexpr wchar_t kStateMutex[] = L"Local\\NoUninstallIcon.StateFile"; }

StateLock::StateLock()
{
    mutex_ = CreateMutexW(nullptr, FALSE, kStateMutex);
    if (!mutex_)
    {
        LogLineF(L"state lock: cannot create (err=%lu)", GetLastError());
        return;
    }
    const DWORD wait = WaitForSingleObject(mutex_, 5000);
    held_ = (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED);
    if (!held_) LogLineF(L"state lock: not acquired (%lu)", wait);
}

StateLock::~StateLock()
{
    if (held_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}

bool Store::Load()
{
    records_.clear();
    settings_ = AppSettings{};
    readOnly_ = false;
    loadedFromBackup_ = false;

    std::wstring path = StateFilePath();
    if (!PathFileExistsW(path.c_str()))
    {
        // No primary: a leftover backup is still the only memory of what we hid.
        if (ParseStateFile(path + L".bak", records_, settings_))
        {
            LogLineF(L"Recovered %d record(s) from the backup", static_cast<int>(records_.size()));
            loadedFromBackup_ = true;
            return true;
        }
        return true;  // first run
    }

    if (ParseStateFile(path, records_, settings_)) return true;

    // The records are the only memory of which shortcuts were hidden, so a
    // damaged file must not silently erase that knowledge.
    LogLineF(L"State file is unreadable, trying backup: %s", path.c_str());
    if (ParseStateFile(path + L".bak", records_, settings_))
    {
        LogLineF(L"Recovered %d record(s) from backup", static_cast<int>(records_.size()));
        loadedFromBackup_ = true;
        return true;
    }

    readOnly_ = true;
    LogLine(L"State file and backup are both unreadable; leaving the file alone");
    return false;
}

bool Store::Save()
{
    if (readOnly_)
    {
        LogLine(L"Refusing to save over an unreadable state file");
        LogLine(L"Save refused: the state file could not be read");
    }

    if (!EnsureDirectory(GetDataDir())) return false;

    std::wstring output;
    output += kHeader;
    output += L"\r\n";

    if (!settings_.language.empty()) output += L"S\tlanguage\t" + Escape(settings_.language) + L"\r\n";

    const wchar_t* mode = L"off";
    if (settings_.autostart == AutostartMode::Once) mode = L"once";
    else if (settings_.autostart == AutostartMode::Watch) mode = L"watch";
    else if (settings_.autostart == AutostartMode::Silent) mode = L"silent";
    output += std::wstring(L"S\tautostart\t") + mode + L"\r\n";

    for (const auto& record : records_)
    {
        wchar_t attributes[16]{};
        swprintf_s(attributes, L"%08X", record.originalAttributes);
        output += L"R\t" + Escape(record.key) + L"\t" + Escape(record.path) + L"\t" + attributes + L"\t" +
                  (record.desiredHidden ? L"1" : L"0") + L"\t" + Escape(record.displayName) + L"\t" +
                  Escape(record.target) + L"\t" + std::to_wstring(record.firstSeen) + L"\t" +
                  std::to_wstring(record.lastApplied) + L"\r\n";
    }

    std::wstring path = StateFilePath();

    // Write the new content to a private temp file and flush it before touching
    // the old copy: the backup must never be replaced by data we have not
    // managed to store yet, or a failed write leaves nothing behind.
    const std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    const std::string bytes = Narrow(output);
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        LogLineF(L"Save failed: cannot create the temp file (err=%lu)", GetLastError());
        return false;
    }
    DWORD written = 0;
    const BOOL wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const BOOL flushed = FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !flushed || written != bytes.size())
    {
        DeleteFileW(temp.c_str());
        return false;
    }

    // Only demote a primary we actually loaded: if the good data came from the
    // backup, copying the damaged primary over it would destroy the last copy.
    if (!loadedFromBackup_ && PathFileExistsW(path.c_str()))
    {
        if (!CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE))
            LogLineF(L"Backup not refreshed, keeping the old one (err=%lu)", GetLastError());
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
        return false;
    }
    loadedFromBackup_ = false;
    return true;
}

StoredRecord* Store::Find(const std::wstring& key)
{
    for (auto& record : records_)
        if (record.key == key) return &record;
    return nullptr;
}

const StoredRecord* Store::Find(const std::wstring& key) const
{
    for (const auto& record : records_)
        if (record.key == key) return &record;
    return nullptr;
}

StoredRecord& Store::Ensure(const std::wstring& key)
{
    if (StoredRecord* existing = Find(key)) return *existing;
    StoredRecord record;
    record.key = key;
    record.firstSeen = static_cast<long long>(time(nullptr));
    records_.push_back(std::move(record));
    return records_.back();
}

void Store::Erase(const std::wstring& key)
{
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [&](const StoredRecord& record) { return record.key == key; }),
                   records_.end());
}
