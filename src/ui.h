// Main application window: custom Fluent-styled list built on Direct2D.
#pragma once

#include "common.h"
#include "model.h"
#include "paint.h"
#include "gfx.h"

struct UiCallbacks
{
    std::function<void(int itemIndex, bool hidden)> toggleItem;
    std::function<void()> rescan;
    std::function<void(AutostartMode)> autostartChanged;
    std::function<void(const std::wstring&)> languageChanged;
    std::function<void()> restoreAll;
    std::function<void(bool hide)> hideUninstallGroup;
    std::function<void()> undo;
    std::function<void(int itemIndex)> openLocation;
    std::function<void()> scanDone;
    std::function<void(bool ok, AutostartMode mode)> autostartDone;
};

enum class FocusTarget
{
    Search,
    List,
    Rescan,
    RestoreAll,
    HideUninstall,
    Autostart,
    Language,
    About,
};

class MainWindow
{
public:
    bool Create(HINSTANCE instance, int showCommand);
    HWND Handle() const { return hwnd_; }

    void SetModel(Model* model);
    void ModelChanged();
    void RebuildDisplay();
    void ApplyTheme();
    void SetAutostartMode(AutostartMode mode);
    AutostartMode GetAutostartMode() const { return autostart_; }
    void SetLoading(bool loading);
    void ShowToast(const std::wstring& text, bool undoable);
    void Invalidate() const;
    void FocusSearch();
    void RefreshText();
    void SetAboutOpen(bool open);
    void SetPreviewQuery(const std::wstring& text);
    void SetPreviewFocus(int index);
    void SetPreviewScroll(int pixels) { previewScroll_ = static_cast<float>(pixels); }
    bool IsAboutOpen() const { return aboutOpen_; }

    // Off-screen rendering (used by --screenshot).
    bool InitOffscreen();
    void RenderTo(ID2D1RenderTarget* target, float width, float height, bool dark);

    UiCallbacks callbacks;

private:
    struct Entry
    {
        bool header = false;
        int value = 0;  // group index for headers, row index for items
        float top = 0.0f;
        float height = 0.0f;
    };

    struct Layout
    {
        D2D1_RECT_F title{};
        D2D1_RECT_F subtitle{};
        D2D1_RECT_F search{};
        D2D1_RECT_F searchText{};
        D2D1_RECT_F rescan{};
        D2D1_RECT_F restoreAll{};
        D2D1_RECT_F list{};
        D2D1_RECT_F footer{};
        D2D1_RECT_F language{};
        D2D1_RECT_F autostartLabel{};
        D2D1_RECT_F autostartCombo{};
        D2D1_RECT_F scrollbar{};
        D2D1_RECT_F toast{};
        D2D1_RECT_F toastUndo{};
        D2D1_RECT_F aboutButton{};
        D2D1_RECT_F dialogCard{};
        D2D1_RECT_F dialogOk{};
        float contentHeight = 0.0f;
        float maxScroll = 0.0f;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void OnCreate();
    void OnSize(UINT width, UINT height);
    void OnPaint();
    void OnDpiChanged(WPARAM wparam, LPARAM lparam);
    void OnMouseMove(int x, int y);
    void OnLeftDown(int x, int y);
    void OnLeftUp(int x, int y);
    void OnRightClick(int x, int y);
    void OnMouseWheel(int delta);
    void OnKeyDown(WPARAM key);
    void OnChar(wchar_t ch);
    void OnTimer(UINT_PTR id);
    void OnImeStartComposition();
    void OnImeComposition(LPARAM flags);
    void OnImeEndComposition();
    void UpdateImePosition();

    Layout ComputeLayout() const;
    void Draw();
    void DrawHeader(const Layout& layout);
    void DrawToolbar(const Layout& layout);
    void DrawList(const Layout& layout);
    void DrawGroupHeader(const D2D1_RECT_F& rect, int group, bool sticky);
    D2D1_RECT_F GroupActionRect(const D2D1_RECT_F& header, int group);
    int HeaderGroupAtPoint(float y, D2D1_RECT_F* headerRect) const;
    bool AllUninstallHidden() const;
    void DrawRow(const Entry& entry, const D2D1_RECT_F& rect, int rowIndex, bool focused);
    void DrawFooter(const Layout& layout);
    void DrawToast(const Layout& layout);
    void DrawScrollbar(const Layout& layout);
    void DrawAbout(const Layout& layout);
    D2D1_RECT_F AboutLinkRect(const Layout& layout);

    ID2D1Bitmap* IconFor(const std::wstring& path, int size);

    void SetSearchFocus(bool focused);
    void SetFocusTarget(FocusTarget target);
    void CycleFocus(bool backwards);
    void HandleSearchKey(WPARAM key, bool ctrl, bool shift);
    void HandleListKey(WPARAM key);
    void CycleAutostart(int delta);
    void CycleLanguage(int delta);
    void ApplyLanguage(const std::wstring& code);
    void DrawFocusRing();
    void StartIconWorker();
    void StopIconWorker();
    void IconWorkerMain();
    void DrainReadyIcons();
    bool HideActionRect(D2D1_RECT_F& rect);
    bool IsFocusTargetAvailable(FocusTarget target);
    void DemoteHiddenFocus();
    void ShowSearchMenu();
    size_t CaretFromX(float x, const Layout& layout);
    void InsertText(const std::wstring& text);
    void CopySelection(bool cut);
    void PasteFromClipboard();
    void DeleteSelection();
    void ClampCaret();
    std::wstring SelectedText() const;
    bool HasSelection() const { return caret_ != anchor_; }
    void UpdateQuery();
    void ScrollTo(float offset);
    void EnsureVisible(int rowIndex);
    int RowAtPoint(float y, float* rowTop) const;
    void ShowLanguageMenu();
    void ShowAutostartMenu();
    void ShowContextMenu(int rowIndex);
    void ClampScroll();
    void ReportError(const std::wstring& text);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Gfx gfx_;
    Painter painter_;
    Theme theme_;
    Model* model_ = nullptr;
    AutostartMode autostart_ = AutostartMode::Off;

    std::vector<Entry> entries_;
    float contentHeightCache_ = 0.0f;
    float scroll_ = 0.0f;
    int hotRow_ = -1;
    int hotControl_ = 0;
    int focusRow_ = -1;
    bool searchFocused_ = false;
    bool trackingMouse_ = false;
    bool draggingThumb_ = false;
    float dragStartY_ = 0.0f;
    float dragStartScroll_ = 0.0f;

    std::wstring query_;
    std::wstring composition_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    bool selecting_ = false;
    bool caretVisible_ = true;

    bool toastVisible_ = false;
    bool toastUndoable_ = false;
    std::wstring toastText_;
    bool aboutOpen_ = false;
    bool aboutFocusLink_ = false;
    FocusTarget focus_ = FocusTarget::Search;
    float previewScroll_ = -1.0f;
    bool focusRingVisible_ = false;

    ComPtr<IWICImagingFactory> wic_;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> icons_;

    // Icon extraction is slow enough (a .lnk has to be resolved) that doing it
    // during a paint stalls the window while scrolling.
    std::thread iconThread_;
    std::mutex iconMutex_;
    std::condition_variable iconCv_;
    std::deque<std::wstring> iconQueue_;
    std::set<std::wstring> iconRequested_;
    std::vector<std::pair<std::wstring, ComPtr<IWICBitmap>>> iconReady_;
    bool iconStop_ = false;

    float langButtonWidth_ = 120.0f;
    float autostartLabelWidth_ = 150.0f;
    float autostartComboWidth_ = 150.0f;
    float aboutButtonWidth_ = 72.0f;
    float rescanWidth_ = 96.0f;
    float restoreAllWidth_ = 140.0f;
    float dialogWidth_ = 440.0f;
    float dialogOkWidth_ = 88.0f;

    float scale_ = 1.0f;
    int clientWidth_ = 0;
    int clientHeight_ = 0;
};
