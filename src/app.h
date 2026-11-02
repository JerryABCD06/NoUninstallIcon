// Application controller: wires the scanner, the store and the window.
#pragma once

#include "common.h"
#include "model.h"
#include "scanner.h"
#include "store.h"
#include "ui.h"

class App
{
public:
    bool Initialize(HINSTANCE instance);

    int RunGui(int showCommand);
    int RunSilentApply();
    int RunRestoreAll();
    int RunCleanup();
    int RunSelfTest();
    int RunWatch();
    void SetCapturePath(const std::wstring& path) { capturePath_ = path; }
    void SetAboutOpen(bool open) { window_.SetAboutOpen(open); }
    void ScanBlocking();
    bool RenderScreenshot(const std::wstring& path, bool dark);
    void WriteDump(const std::wstring& path);

private:
    void StartScan();
    void FinishScan();
    void MergeRecords();
    void HandleToggle(int itemIndex, bool hidden);
    void HandleRestoreAll();
    void HandleHideGroup(bool hide);
    void HandleAutostart(AutostartMode mode);
    void HandleOpenLocation(int itemIndex);
    void HandleUndo();
    void HandleRescan();
    int ApplyDesired(bool notify);
    void ShowToast(const std::wstring& text, bool undoable);

    std::wstring capturePath_;
    HINSTANCE instance_ = nullptr;
    Model model_;
    Store store_;
    MainWindow window_;

    std::mutex scanMutex_;
    std::unique_ptr<ScanOutcome> pendingScan_;
    bool scanning_ = false;

    struct LastAction
    {
        std::vector<std::wstring> keys;
        bool state = false;
        bool valid = false;
    } last_;
};
