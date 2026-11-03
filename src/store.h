// Persistent state: which shortcuts this tool manages, plus settings.
#pragma once

#include "common.h"
#include "model.h"

struct StoredRecord
{
    std::wstring key;   // NormalizePathKey(path)
    std::wstring path;  // original absolute path
    DWORD originalAttributes = 0;
    bool desiredHidden = false;
    std::wstring displayName;
    std::wstring target;
    long long firstSeen = 0;
    long long lastApplied = 0;
};

struct AppSettings
{
    std::wstring language;  // empty means "follow the system UI language"
    AutostartMode autostart = AutostartMode::Off;
};

// Held across a load-modify-save transaction so two processes cannot interleave:
// the window, the watcher and the command-line modes all take it, because a
// whole-file save from one of them would otherwise overwrite the others.
// The named mutex alone is per-session while state.dat is per-user, so a second
// session (or a portable copy on a share) is excluded by a file lock instead.
class StateLock
{
public:
    StateLock();
    ~StateLock();
    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;

    bool held() const { return held_; }

private:
    HANDLE mutex_ = nullptr;
    HANDLE lockFile_ = nullptr;
    bool fileLocked_ = false;
    bool held_ = false;
};

class Store
{
public:
    bool Load();
    bool Save();

    StoredRecord* Find(const std::wstring& key);
    const StoredRecord* Find(const std::wstring& key) const;
    StoredRecord& Ensure(const std::wstring& key);
    void Erase(const std::wstring& key);

    std::vector<StoredRecord>& Records() { return records_; }
    const std::vector<StoredRecord>& Records() const { return records_; }
    AppSettings& Settings() { return settings_; }
    const AppSettings& Settings() const { return settings_; }

private:
    // Set when the file on disk could not be read at all: saving then would
    // overwrite the only copy of what this tool hid, so the file is left alone.
    bool readOnly_ = false;
    bool loadedFromBackup_ = false;

    std::vector<StoredRecord> records_;
    AppSettings settings_;
};

std::wstring StateFilePath();
