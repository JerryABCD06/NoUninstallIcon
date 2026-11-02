#include "ui.h"

#include "i18n.h"

#include <uiautomation.h>
#include "util.h"

#include <imm.h>
#include <shellscalingapi.h>

namespace {

constexpr float kPad = 24.0f;
constexpr float kToolbarTop = 80.0f;
constexpr float kToolbarHeight = 36.0f;
constexpr float kRowHeight = 48.0f;
constexpr float kHeaderHeight = 34.0f;
constexpr float kFooterHeight = 46.0f;
constexpr float kRadius = 6.0f;
constexpr float kCheckLeft = 28.0f;
constexpr float kCheckSize = 20.0f;
constexpr float kIconLeft = 64.0f;
constexpr float kTextLeft = 96.0f;

constexpr wchar_t kIconSearch[] = L"\uE721";
constexpr wchar_t kIconClear[] = L"\uE711";
constexpr wchar_t kIconRefresh[] = L"\uE72C";
constexpr wchar_t kIconCheck[] = L"\uE73E";
constexpr wchar_t kIconGlobe[] = L"\uE774";
constexpr wchar_t kIconInfo[] = L"\uE946";

constexpr size_t kMaxQueryLength = 200;
constexpr size_t kMaxClipboardLength = 4096;

constexpr wchar_t kAuthorUrl[] = L"https://github.com/JerryABCD06/";
constexpr wchar_t kAuthorDisplay[] = L"github.com/JerryABCD06";

D2D1_RECT_F Rect(float left, float top, float right, float bottom)
{
    return D2D1_RECT_F{left, top, right, bottom};
}

D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float dx, float dy)
{
    return Rect(rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy);
}

bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y)
{
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

float ClampF(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

std::wstring ModeLabel(AutostartMode mode)
{
    switch (mode)
    {
    case AutostartMode::Once: return i18n::T(L"autostart.once");
    case AutostartMode::Watch: return i18n::T(L"autostart.watch");
    default: return i18n::T(L"autostart.off");
    }
}

}  // namespace

bool MainWindow::Create(HINSTANCE instance, int showCommand)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NoUninstallIcon.MainWindow";
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return false;

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP;
    RECT bounds{0, 0, 1040, 760};
    AdjustWindowRectExForDpi(&bounds, style, FALSE, exStyle, 96);

    hwnd_ = CreateWindowExW(exStyle, wc.lpszClassName, i18n::T(L"app.title"), style, CW_USEDEFAULT,
                            CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
                            nullptr, instance, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
    return self->HandleMessage(message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        OnSize(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_DPICHANGED:
        OnDpiChanged(wparam, lparam);
        return 0;
    case WM_SETTINGCHANGE:
        ApplyTheme();
        Invalidate();
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_MOUSELEAVE:
        hotRow_ = -1;
        Invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        OnLeftDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        OnLeftUp(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_RBUTTONUP:
        OnRightClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONDBLCLK:
    {
        float y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_;
        int row = RowAtPoint(y, nullptr);
        if (row >= 0 && callbacks.openLocation) callbacks.openLocation(model_->rows[row]);
        return 0;
    }
    case WM_CAPTURECHANGED:
        // Losing capture (alt-tab, a menu) must not leave a drag half-armed.
        selecting_ = false;
        draggingThumb_ = false;
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wparam);
        return 0;
    case WM_SYSKEYDOWN:
        // F10 is a system key, so Shift+F10 arrives here and not as WM_KEYDOWN.
        if (wparam == VK_F10 || wparam == VK_APPS)
        {
            OnKeyDown(wparam);
            return 0;
        }
        break;
    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wparam));
        return 0;
    case WM_TIMER:
        OnTimer(wparam);
        return 0;
    case WM_APP_SCAN_DONE:
        if (callbacks.scanDone) callbacks.scanDone();
        return 0;
    case WM_APP_ICON_READY:
        DrainReadyIcons();
        return 0;
    case WM_APP_AUTOSTART_DONE:
        if (callbacks.autostartDone)
            callbacks.autostartDone(wparam != 0, static_cast<AutostartMode>(static_cast<int>(lparam)));
        return 0;
    case WM_IME_STARTCOMPOSITION:
        OnImeStartComposition();
        return 0;
    case WM_IME_COMPOSITION:
        OnImeComposition(lparam);
        return 0;
    case WM_IME_ENDCOMPOSITION:
        OnImeEndComposition();
        return 0;
    case WM_IME_CHAR:
        // Already delivered through WM_IME_COMPOSITION; letting DefWindowProc
        // translate it would insert every character twice.
        return 0;
    case WM_GETOBJECT:
        return a11y::HandleGetObject(hwnd_, wparam, lparam);
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = static_cast<LONG>(820 * scale_);
        info->ptMinTrackSize.y = static_cast<LONG>(560 * scale_);
        return 0;
    }
    case WM_DESTROY:
        a11y::Uninstall();
        StopIconWorker();
        GfxDestroy(gfx_);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void MainWindow::OnCreate()
{
    painter_.Init();
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic_.GetAddressOf()));
    scale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;

    RECT client{};
    GetClientRect(hwnd_, &client);
    clientWidth_ = static_cast<int>(client.right - client.left);
    clientHeight_ = static_cast<int>(client.bottom - client.top);

    gfx_.dpi = scale_ * 96.0f;
    GfxCreate(gfx_, hwnd_, static_cast<UINT>(clientWidth_), static_cast<UINT>(clientHeight_), gfx_.dpi);

    a11y::Install(hwnd_);
    StartIconWorker();
    ApplyTheme();
    RefreshText();
    SetFocusTarget(FocusTarget::Search);
    focusRingVisible_ = false;
}

void MainWindow::ApplyTheme()
{
    theme_ = MakeTheme(IsSystemDarkMode());
    BOOL dark = theme_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    Invalidate();
}

void MainWindow::OnSize(UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    clientWidth_ = static_cast<int>(width);
    clientHeight_ = static_cast<int>(height);
    if (gfx_.swapChain) GfxResize(gfx_, width, height, scale_ * 96.0f);
    ClampScroll();
    Invalidate();
}

void MainWindow::OnDpiChanged(WPARAM wparam, LPARAM lparam)
{
    scale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
    const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    RefreshText();
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    if (gfx_.dc)
    {
        GfxBegin(gfx_);
        painter_.SetTarget(gfx_.dc.Get());
        painter_.SetScale(scale_);
        Draw();
        GfxEnd(gfx_);
    }
    EndPaint(hwnd_, &ps);
    (void)dc;
}

void MainWindow::Invalidate() const
{
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SetModel(Model* model)
{
    model_ = model;
    RebuildDisplay();
    Invalidate();
}

void MainWindow::ModelChanged()
{
    RebuildDisplay();
    Invalidate();
}

void MainWindow::SetLoading(bool loading)
{
    if (model_) model_->loading = loading;
    Invalidate();
}

void MainWindow::RebuildDisplay()
{
    entries_.clear();
    if (!model_) return;
    model_->Recompute();

    int lastGroup = -1;
    float y = 0.0f;
    for (int row = 0; row < model_->RowCount(); ++row)
    {
        int group = model_->IsUninstallRow(row) ? 0 : 1;
        if (group != lastGroup)
        {
            if (!entries_.empty()) y += 10.0f;
            Entry header;
            header.header = true;
            header.value = group;
            header.top = y;
            header.height = kHeaderHeight;
            entries_.push_back(header);
            y += kHeaderHeight;
            lastGroup = group;
        }
        Entry entry;
        entry.header = false;
        entry.value = row;
        entry.top = y;
        entry.height = kRowHeight;
        entries_.push_back(entry);
        y += kRowHeight;
    }
    contentHeightCache_ = y;
    ClampScroll();
    if (focusRow_ >= model_->RowCount()) focusRow_ = model_->RowCount() - 1;
    // Rows appear after the scan; give the focused list something to point at.
    if (focus_ == FocusTarget::List && focusRow_ < 0 && model_->RowCount() > 0) focusRow_ = 0;
}

void MainWindow::ClampScroll()
{
    float height = static_cast<float>(clientHeight_) / scale_;
    float view = height - (kFooterHeight + 136.0f);
    if (view < 0.0f) view = 0.0f;
    float maxScroll = std::max(0.0f, contentHeightCache_ - view);
    scroll_ = ClampF(scroll_, 0.0f, maxScroll);
}

void MainWindow::UpdateQuery()
{

    // Announce the field once the typing pauses; every edit restarts the wait.
    if (hwnd_ && searchFocused_) SetTimer(hwnd_, 3, 1000, nullptr);
    ClampCaret();
    if (!model_) return;
    model_->query = query_;
    RebuildDisplay();
    scroll_ = 0.0f;
    Invalidate();
}

void MainWindow::FocusSearch()
{
    SetSearchFocus(true);
}

void MainWindow::SetSearchFocus(bool focused)
{
    SetFocusTarget(focused ? FocusTarget::Search : FocusTarget::List);
}

void MainWindow::OnTimer(UINT_PTR id)
{
    if (id == 1)
    {
        caretVisible_ = !caretVisible_;
        Invalidate();
    }
    else if (id == 2)
    {
        KillTimer(hwnd_, 2);
        toastVisible_ = false;
        Invalidate();
    }
    else if (id == 3)
    {
        // A newer notification replaces whatever is still being spoken.
        KillTimer(hwnd_, 3);
        if (searchFocused_ && !query_.empty()) a11y::Notify(query_);
    }
}

void MainWindow::RefreshText()
{
    if (!painter_.DWrite()) return;  // called before the painter is ready
    if (hwnd_) SetWindowTextW(hwnd_, i18n::T(L"app.title"));

    std::wstring langName;
    for (const auto& lang : i18n::Available())
    {
        if (EqualsI(lang.code, i18n::CurrentCode())) langName = lang.nativeName;
    }
    if (langName.empty()) langName = i18n::CurrentCode();

    IDWriteTextFormat* smallFormat = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    langButtonWidth_ = 12.0f + 16.0f + 8.0f + painter_.TextWidth(langName, smallFormat, 200.0f) + 8.0f + 10.0f + 12.0f;

    IDWriteTextFormat* labelFormat = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    autostartLabelWidth_ = std::min(320.0f, painter_.TextWidth(i18n::T(L"toggle.autostart"), labelFormat, 320.0f));

    // Size the combo to the widest of the three modes so switching does not
    // make the footer jump around.
    float modeWidth = 0.0f;
    for (const wchar_t* text : {i18n::T(L"autostart.off"), i18n::T(L"autostart.once"), i18n::T(L"autostart.watch")})
        modeWidth = std::max(modeWidth, painter_.TextWidth(text, labelFormat, 320.0f));
    autostartComboWidth_ = modeWidth + 34.0f;

    IDWriteTextFormat* aboutFormat = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    aboutButtonWidth_ = painter_.TextWidth(i18n::T(L"about.button"), aboutFormat, 260.0f) + 44.0f;

    // Toolbar buttons size themselves to their label so a longer translation
    // does not leave one button visibly wider than the other.
    rescanWidth_ = std::max(72.0f, painter_.TextWidth(i18n::T(L"action.rescan"), aboutFormat, 320.0f) + 24.0f);
    restoreAllWidth_ = std::max(72.0f, painter_.TextWidth(i18n::T(L"action.restore_all"), aboutFormat, 320.0f) + 24.0f);

    // The About card grows with its longest line instead of clipping it.
    {
        IDWriteTextFormat* titleFormat = painter_.Format(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                         L"Segoe UI Variable Display");
        IDWriteTextFormat* bodyFormat = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
        float titleWidth = painter_.TextWidth(i18n::T(L"about.title"), titleFormat, 640.0f);
        float freeWidth = painter_.TextWidth(i18n::T(L"about.free"), bodyFormat, 640.0f);
        float licenseWidth = painter_.TextWidth(i18n::T(L"about.license"), bodyFormat, 640.0f);
        float authorWidth = painter_.TextWidth(i18n::T(L"about.author"), bodyFormat, 320.0f) +
                            painter_.TextWidth(i18n::T(L"about.author_name"), bodyFormat, 320.0f) + 6.0f;
        float widest = std::max(std::max(titleWidth, freeWidth), std::max(licenseWidth, authorWidth));
        dialogWidth_ = std::min(640.0f, std::max(440.0f, widest + 2.0f * kPad));
        dialogOkWidth_ = std::max(88.0f, painter_.TextWidth(i18n::T(L"about.ok"), bodyFormat, 240.0f) + 32.0f);
    }

    Invalidate();
}

void MainWindow::SetAutostartMode(AutostartMode mode)
{
    autostart_ = mode;
    RefreshText();
}

MainWindow::Layout MainWindow::ComputeLayout() const
{
    Layout layout;
    float width = static_cast<float>(clientWidth_) / scale_;
    float height = static_cast<float>(clientHeight_) / scale_;

    layout.title = Rect(kPad, 12.0f, width - kPad, 46.0f);
    layout.subtitle = Rect(kPad, 46.0f, width - kPad, 64.0f);
    layout.search = Rect(kPad, kToolbarTop, kPad + 380.0f, kToolbarTop + kToolbarHeight);
    layout.searchText = Rect(layout.search.left + 34.0f, layout.search.top, layout.search.right - 30.0f,
                             layout.search.bottom);

    float rightEdge = width - kPad;
    layout.rescan = Rect(rightEdge - rescanWidth_, kToolbarTop, rightEdge, kToolbarTop + kToolbarHeight);
    layout.restoreAll = Rect(layout.rescan.left - 8.0f - restoreAllWidth_, kToolbarTop, layout.rescan.left - 8.0f,
                             kToolbarTop + kToolbarHeight);

    layout.list = Rect(0.0f, 136.0f, width, height - kFooterHeight);
    layout.footer = Rect(0.0f, height - kFooterHeight, width, height);
    layout.scrollbar = Rect(width - 12.0f, layout.list.top + 4.0f, width - 4.0f, layout.list.bottom - 4.0f);

    float midY = height - kFooterHeight * 0.5f;
    layout.language = Rect(rightEdge - langButtonWidth_, midY - 15.0f, rightEdge, midY + 15.0f);

    float comboRight = layout.language.left - 16.0f;
    layout.autostartCombo = Rect(comboRight - autostartComboWidth_, midY - 15.0f, comboRight, midY + 15.0f);
    float labelRight = layout.autostartCombo.left - 8.0f;
    layout.autostartLabel = Rect(labelRight - autostartLabelWidth_, midY - 12.0f, labelRight, midY + 12.0f);

    // Wider than before: the toast now carries a shortcut name.
    float toastWidth = std::min(680.0f, width - 64.0f);
    layout.toast = Rect((width - toastWidth) * 0.5f, layout.footer.top - 60.0f,
                        (width + toastWidth) * 0.5f, layout.footer.top - 16.0f);
    layout.toastUndo = Rect(layout.toast.right - 96.0f, layout.toast.top + 8.0f, layout.toast.right - 16.0f,
                            layout.toast.bottom - 8.0f);

    layout.aboutButton = Rect(width - kPad - aboutButtonWidth_, 12.0f, width - kPad, 48.0f);

    const float dialogWidth = dialogWidth_;
    const float dialogHeight = 232.0f;
    layout.dialogCard = Rect((width - dialogWidth) * 0.5f, (height - dialogHeight) * 0.5f,
                             (width + dialogWidth) * 0.5f, (height + dialogHeight) * 0.5f);
    layout.dialogOk = Rect(layout.dialogCard.right - kPad - dialogOkWidth_, layout.dialogCard.bottom - kPad - 32.0f,
                           layout.dialogCard.right - kPad, layout.dialogCard.bottom - kPad);
    return layout;
}

void MainWindow::Draw()
{
    Layout layout = ComputeLayout();
    DrawHeader(layout);
    DrawToolbar(layout);
    DrawList(layout);
    DrawFooter(layout);
    DrawFocusRing();
    PublishAccessibility(layout);
    if (toastVisible_) DrawToast(layout);
    if (aboutOpen_) DrawAbout(layout);
}

void MainWindow::DrawHeader(const Layout& layout)
{
    painter_.Text(i18n::T(L"app.title"), painter_.Format(24.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                                        L"Segoe UI Variable Display"),
                  layout.title, theme_.text);
    painter_.Text(i18n::T(L"app.subtitle"), painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL), layout.subtitle,
                  theme_.textDim);

    // Accent-coloured header command with the Fluent "Info" glyph.
    if (hotControl_ == 6) painter_.FillRound(layout.aboutButton, 4.0f, theme_.hover);
    const D2D1_RECT_F& about = layout.aboutButton;
    painter_.Text(kIconInfo, painter_.IconFormat(15.0f),
                  Rect(about.left + 10.0f, about.top, about.left + 26.0f, about.bottom), theme_.accent,
                  DWRITE_TEXT_ALIGNMENT_CENTER, false);
    painter_.Text(i18n::T(L"about.button"), painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(about.left + 32.0f, about.top, about.right - 10.0f, about.bottom), theme_.accent,
                  DWRITE_TEXT_ALIGNMENT_LEADING);
}

void MainWindow::DrawToolbar(const Layout& layout)
{
    // Search field.
    painter_.FillRound(layout.search, kRadius, theme_.controlFill);
    painter_.StrokeRound(layout.search, kRadius, searchFocused_ ? theme_.accent : theme_.controlBorder,
                         searchFocused_ ? 1.4f : 0.0f);

    D2D1_RECT_F iconRect = Rect(layout.search.left + 10.0f, layout.search.top, layout.search.left + 30.0f,
                                layout.search.bottom);
    painter_.Text(kIconSearch, painter_.IconFormat(14.0f), iconRect, theme_.textDim,
                  DWRITE_TEXT_ALIGNMENT_CENTER, false);

    IDWriteTextFormat* textFormat = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
    const D2D1_RECT_F textRect = layout.searchText;

    float caretX = textRect.left + painter_.TextWidth(query_.substr(0, caret_), textFormat);

    if (query_.empty() && composition_.empty())
    {
        painter_.Text(i18n::T(L"search.placeholder"), textFormat, textRect, theme_.textFaint);
    }
    else
    {
        painter_.PushClip(textRect);

        if (HasSelection())
        {
            size_t from = std::min(caret_, anchor_);
            size_t to = std::max(caret_, anchor_);
            float x1 = textRect.left + painter_.TextWidth(query_.substr(0, from), textFormat);
            float x2 = textRect.left + painter_.TextWidth(query_.substr(0, to), textFormat);
            painter_.FillRect(Rect(x1, textRect.top + 7.0f, x2, textRect.bottom - 7.0f),
                              WithAlpha(theme_.accent, 0.30f));
        }

        if (composition_.empty())
        {
            painter_.Text(query_, textFormat, textRect, theme_.text);
        }
        else
        {
            painter_.Text(query_.substr(0, caret_), textFormat, textRect, theme_.text);
            float compositionWidth = painter_.TextWidth(composition_, textFormat);
            painter_.Text(composition_, textFormat,
                          Rect(caretX, textRect.top, caretX + compositionWidth + 4.0f, textRect.bottom),
                          theme_.text, DWRITE_TEXT_ALIGNMENT_LEADING, false);
            painter_.Line(D2D1::Point2F(caretX, textRect.bottom - 8.0f),
                          D2D1::Point2F(caretX + compositionWidth, textRect.bottom - 8.0f), theme_.accent, 1.0f);
            if (caret_ < query_.size())
                painter_.Text(query_.substr(caret_), textFormat,
                              Rect(caretX + compositionWidth, textRect.top, textRect.right, textRect.bottom),
                              theme_.text, DWRITE_TEXT_ALIGNMENT_LEADING, false);
            caretX += compositionWidth;
        }

        painter_.PopClip();
    }

    if (searchFocused_ && caretVisible_ && composition_.empty())
    {
        float clamped = ClampF(caretX + 0.5f, textRect.left, layout.search.right - 8.0f);
        painter_.Line(D2D1::Point2F(clamped, layout.search.top + 9.0f),
                      D2D1::Point2F(clamped, layout.search.bottom - 9.0f), theme_.accent, 1.2f);
    }

    if (!query_.empty() || !composition_.empty())
    {
        D2D1_RECT_F clearRect = Rect(layout.search.right - 26.0f, layout.search.top + 8.0f,
                                     layout.search.right - 10.0f, layout.search.bottom - 8.0f);
        painter_.Text(kIconClear, painter_.IconFormat(10.0f), clearRect, theme_.textDim,
                      DWRITE_TEXT_ALIGNMENT_CENTER, false);
    }

    auto drawButton = [&](const D2D1_RECT_F& rect, const wchar_t* label, bool hot) {
        if (hot) painter_.FillRound(rect, kRadius, theme_.hover);
        IDWriteTextFormat* format = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
        D2D1_RECT_F textRect = Rect(rect.left + 12.0f, rect.top, rect.right - 12.0f, rect.bottom);
        painter_.Text(label, format, textRect, hot ? theme_.text : theme_.textDim,
                      DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    drawButton(layout.rescan, i18n::T(L"action.rescan"), hotControl_ == 1);
    drawButton(layout.restoreAll, i18n::T(L"action.restore_all"), hotControl_ == 2);

    painter_.Line(D2D1::Point2F(0.0f, 132.0f), D2D1::Point2F(static_cast<float>(clientWidth_) / scale_, 132.0f),
                  theme_.stroke);
}

void MainWindow::DrawList(const Layout& layout)
{
    painter_.PushClip(layout.list);

    if (!model_ || model_->loading)
    {
        painter_.TextCentered(i18n::T(L"status.loading"),
                              painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL),
                              layout.list, theme_.textDim, false);
    }
    else if (entries_.empty())
    {
        std::wstring message = query_.empty() ? i18n::T(L"status.empty")
                                              : i18n::Tf(L"empty.search", {query_});
        painter_.TextCentered(message, painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL), layout.list,
                              theme_.textDim, false);
    }
    else
    {
        for (const Entry& entry : entries_)
        {
            float top = layout.list.top + entry.top - scroll_;
            if (top + entry.height < layout.list.top) continue;
            if (top > layout.list.bottom) break;

            if (entry.header)
            {
                DrawGroupHeader(Rect(layout.list.left, top, layout.list.right, top + entry.height), entry.value,
                                false);
            }
            else
            {
                DrawRow(entry, Rect(layout.list.left, top, layout.list.right, top + entry.height), entry.value,
                        entry.value == focusRow_);
            }
        }

        // Sticky group header.
        int stickyIndex = -1;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        {
            if (!entries_[i].header) continue;
            if (entries_[i].top <= scroll_) stickyIndex = i;
            else break;
        }
        if (stickyIndex >= 0 && entries_[stickyIndex].top < scroll_ - 0.5f)
        {
            float pinnedTop = layout.list.top;
            for (int i = stickyIndex + 1; i < static_cast<int>(entries_.size()); ++i)
            {
                if (!entries_[i].header) continue;
                float nextTop = layout.list.top + entries_[i].top - scroll_;
                if (nextTop < pinnedTop + kHeaderHeight) pinnedTop = nextTop - kHeaderHeight;
                break;
            }
            DrawGroupHeader(Rect(layout.list.left, pinnedTop, layout.list.right, pinnedTop + kHeaderHeight),
                            entries_[stickyIndex].value, true);
        }
    }

    painter_.PopClip();
    DrawScrollbar(layout);
}

void MainWindow::DrawGroupHeader(const D2D1_RECT_F& rect, int group, bool sticky)
{
    if (sticky)
    {
        // Opaque: a translucent fill lets the row underneath show through.
        D2D1_COLOR_F background = theme_.windowBg;
        background.a = 1.0f;
        painter_.FillRect(rect, background);
        painter_.Line(D2D1::Point2F(rect.left, rect.bottom - 0.5f),
                      D2D1::Point2F(rect.right, rect.bottom - 0.5f), theme_.stroke);
    }

    const wchar_t* title = group == 0 ? i18n::T(L"group.uninstall") : i18n::T(L"group.other");
    int count = group == 0 ? (model_ ? model_->uninstallTotal : 0) : (model_ ? model_->otherTotal : 0);

    // Reserve exactly what the count and the action need, so a longer
    // translation cannot run into the group title.
    IDWriteTextFormat* countFormat = painter_.Format(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
    float countWidth = painter_.TextWidth(i18n::Tf(L"group.items", {std::to_wstring(count)}), countFormat, 260.0f);
    float actionWidth = 0.0f;
    if (group == 0)
    {
        const wchar_t* actionLabel = AllUninstallHidden() ? i18n::T(L"action.unhide_group")
                                                          : i18n::T(L"action.hide_group");
        actionWidth = painter_.TextWidth(actionLabel, painter_.Format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), 260.0f) +
                      24.0f;
    }
    float reserve = countWidth + actionWidth + 28.0f;

    D2D1_RECT_F textRect = Rect(rect.left + kCheckLeft, rect.top, rect.right - reserve, rect.bottom);
    painter_.Text(title, painter_.Format(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), textRect, theme_.textDim);

    D2D1_RECT_F countRect = Rect(rect.right - reserve, rect.top, rect.right - kPad, rect.bottom);
    painter_.Text(i18n::Tf(L"group.items", {std::to_wstring(count)}), countFormat, countRect, theme_.textFaint,
                  DWRITE_TEXT_ALIGNMENT_TRAILING);

    if (group == 0)
    {
        D2D1_RECT_F action = GroupActionRect(rect, group);
        const wchar_t* label = AllUninstallHidden() ? i18n::T(L"action.unhide_group")
                                                    : i18n::T(L"action.hide_group");
        painter_.Text(label, painter_.Format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), action, theme_.accent,
                      DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

bool MainWindow::AllUninstallHidden() const
{
    if (!model_) return false;
    bool any = false;
    for (int row : model_->rows)
    {
        const ShortcutItem* item = model_->At(row);
        if (!item || !item->isUninstall) continue;
        any = true;
        if (!(item->managed && item->desiredHidden)) return false;
    }
    return any;
}

D2D1_RECT_F MainWindow::GroupActionRect(const D2D1_RECT_F& header, int group)
{
    if (group != 0 || !model_) return Rect(0.0f, 0.0f, 0.0f, 0.0f);
    int count = model_->uninstallTotal;
    float countWidth = painter_.TextWidth(i18n::Tf(L"group.items", {std::to_wstring(count)}),
                                          painter_.Format(11.5f, DWRITE_FONT_WEIGHT_NORMAL), 240.0f);
    const wchar_t* label = AllUninstallHidden() ? i18n::T(L"action.unhide_group")
                                                : i18n::T(L"action.hide_group");
    float labelWidth = painter_.TextWidth(label, painter_.Format(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD), 240.0f);
    float right = header.right - kPad - countWidth - 18.0f;
    return Rect(right - labelWidth - 24.0f, header.top + 4.0f, right, header.bottom - 4.0f);
}

int MainWindow::HeaderGroupAtPoint(float y, D2D1_RECT_F* headerRect) const
{
    if (!model_ || entries_.empty()) return -1;
    Layout layout = ComputeLayout();
    if (y < layout.list.top || y > layout.list.bottom) return -1;
    float contentY = y - layout.list.top + scroll_;
    for (const Entry& entry : entries_)
    {
        if (!entry.header) continue;
        if (contentY >= entry.top && contentY < entry.top + entry.height)
        {
            if (headerRect)
            {
                float top = layout.list.top + entry.top - scroll_;
                *headerRect = Rect(layout.list.left, top, layout.list.right, top + entry.height);
            }
            return entry.value;
        }
    }
    return -1;
}

void MainWindow::DrawRow(const Entry& entry, const D2D1_RECT_F& rect, int rowIndex, bool focused)
{
    if (!model_) return;
    (void)entry;
    const ShortcutItem* item = model_->At(rowIndex);
    if (!item) return;

    bool hot = rowIndex == hotRow_;
    if (hot)
    {
        painter_.FillRound(Inset(rect, 12.0f, 2.0f), 4.0f, theme_.hover);
    }
    else if (focused)
    {
        painter_.FillRound(Inset(rect, 12.0f, 2.0f), 4.0f, theme_.pressed);
    }

    bool checked = item->managed && item->desiredHidden;
    float centerY = rect.top + (rect.bottom - rect.top) * 0.5f;
    D2D1_RECT_F check = Rect(kCheckLeft, centerY - kCheckSize * 0.5f, kCheckLeft + kCheckSize,
                             centerY + kCheckSize * 0.5f);

    if (checked)
    {
        painter_.FillRound(check, 4.0f, theme_.accent);
        painter_.Text(kIconCheck, painter_.IconFormat(11.0f),
                      Rect(check.left, check.top, check.right, check.bottom), theme_.onAccent,
                      DWRITE_TEXT_ALIGNMENT_CENTER, false);
    }
    else
    {
        painter_.StrokeRound(check, 4.0f, hot ? theme_.strokeStrong : theme_.checkBorder, 1.0f);
    }

    D2D1_RECT_F iconRect = Rect(kIconLeft, centerY - 10.0f, kIconLeft + 20.0f, centerY + 10.0f);
    if (ID2D1Bitmap* icon = IconFor(item->path, 20))
    {
        if (painter_.Target()) painter_.Target()->DrawBitmap(icon, iconRect, 0.95f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else
    {
        painter_.FillRound(iconRect, 4.0f, theme_.badge);
    }

    float rightLimit = rect.right - kPad;
    if (!item->managed && item->hiddenOnDisk)
    {
        std::wstring label = i18n::T(L"badge.external");
        IDWriteTextFormat* badgeFormat = painter_.Format(10.0f, DWRITE_FONT_WEIGHT_NORMAL);
        float badgeWidth = painter_.TextWidth(label, badgeFormat, 240.0f) + 16.0f;
        D2D1_RECT_F badge = Rect(rightLimit - badgeWidth, centerY - 10.0f, rightLimit, centerY + 10.0f);
        painter_.FillRound(badge, 10.0f, theme_.badge);
        painter_.Text(label, badgeFormat, badge, theme_.textDim, DWRITE_TEXT_ALIGNMENT_CENTER);
        rightLimit = badge.left - 8.0f;
    }

    D2D1_RECT_F nameRect = Rect(kTextLeft, rect.top + 5.0f, rightLimit, rect.top + 27.0f);
    painter_.Text(item->displayName, painter_.Format(14.0f, DWRITE_FONT_WEIGHT_NORMAL), nameRect, theme_.text);

    D2D1_RECT_F subRect = Rect(kTextLeft, rect.top + 27.0f, rightLimit, rect.top + 45.0f);
    painter_.Text(item->target, painter_.Format(11.0f, DWRITE_FONT_WEIGHT_NORMAL), subRect, theme_.textFaint);
}

void MainWindow::DrawFooter(const Layout& layout)
{
    painter_.Line(D2D1::Point2F(0.0f, layout.footer.top),
                  D2D1::Point2F(static_cast<float>(clientWidth_) / scale_, layout.footer.top), theme_.stroke);

    std::wstring status;
    if (!model_ || model_->loading)
    {
        status = i18n::T(L"status.loading");
    }
    else if (focusRow_ >= 0 && model_->At(focusRow_))
    {
        status = model_->At(focusRow_)->path;
    }
    else
    {
        status = i18n::Tf(L"status.summary", {std::to_wstring(model_->items.size()),
                                             std::to_wstring(model_->managedCount),
                                             std::to_wstring(model_->externalHiddenCount)});
    }

    D2D1_RECT_F statusRect = Rect(kPad, layout.footer.top, layout.autostartLabel.left - 24.0f,
                                  layout.footer.bottom);
    painter_.Text(status, painter_.Format(11.5f, DWRITE_FONT_WEIGHT_NORMAL), statusRect, theme_.textDim);

    // Language button.
    painter_.FillRound(layout.language, kRadius, theme_.controlFill);
    painter_.StrokeRound(layout.language, kRadius, theme_.controlBorder, 0.0f);

    D2D1_RECT_F globeRect = Rect(layout.language.left + 10.0f, layout.language.top,
                                 layout.language.left + 26.0f, layout.language.bottom);
    painter_.Text(kIconGlobe, painter_.IconFormat(13.0f), globeRect, theme_.textDim,
                  DWRITE_TEXT_ALIGNMENT_CENTER, false);

    std::wstring langName;
    for (const auto& lang : i18n::Available())
    {
        if (EqualsI(lang.code, i18n::CurrentCode())) langName = lang.nativeName;
    }
    if (langName.empty()) langName = i18n::CurrentCode();

    D2D1_RECT_F langText = Rect(layout.language.left + 30.0f, layout.language.top, layout.language.right - 16.0f,
                                layout.language.bottom);
    painter_.Text(langName, painter_.Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL), langText, theme_.text);

    painter_.Text(L"\uE70D", painter_.IconFormat(8.0f),
                  Rect(layout.language.right - 18.0f, layout.language.top, layout.language.right - 8.0f,
                       layout.language.bottom),
                  theme_.textDim, DWRITE_TEXT_ALIGNMENT_CENTER, false);

    // Autostart combo, styled like the language picker.
    painter_.Text(i18n::T(L"toggle.autostart"), painter_.Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL),
                  layout.autostartLabel, theme_.textDim, DWRITE_TEXT_ALIGNMENT_TRAILING);

    const D2D1_RECT_F& combo = layout.autostartCombo;
    painter_.FillRound(combo, 4.0f, hotControl_ == 4 ? theme_.hover : theme_.controlFill);
    painter_.StrokeRound(combo, 4.0f, theme_.controlBorder, 0.0f);
    painter_.Text(ModeLabel(autostart_), painter_.Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(combo.left + 12.0f, combo.top, combo.right - 22.0f, combo.bottom), theme_.text);
    painter_.Text(L"\uE70D", painter_.IconFormat(8.0f),
                  Rect(combo.right - 20.0f, combo.top, combo.right - 8.0f, combo.bottom), theme_.textDim,
                  DWRITE_TEXT_ALIGNMENT_CENTER, false);
}

void MainWindow::DrawScrollbar(const Layout& layout)
{
    if (!model_ || model_->loading) return;
    float viewHeight = layout.list.bottom - layout.list.top;
    if (contentHeightCache_ <= viewHeight + 1.0f) return;

    float trackHeight = layout.scrollbar.bottom - layout.scrollbar.top;
    float thumbHeight = std::max(32.0f, trackHeight * (viewHeight / contentHeightCache_));
    float maxScroll = std::max(1.0f, contentHeightCache_ - viewHeight);
    float ratio = ClampF(scroll_ / maxScroll, 0.0f, 1.0f);
    float thumbTop = layout.scrollbar.top + ratio * (trackHeight - thumbHeight);
    float width = layout.scrollbar.right - layout.scrollbar.left;
    D2D1_RECT_F thumb = Rect(layout.scrollbar.left + 2.0f, thumbTop, layout.scrollbar.left + 2.0f + 6.0f,
                             thumbTop + thumbHeight);
    painter_.FillRound(thumb, 3.0f, WithAlpha(theme_.textDim, 0.45f));
    (void)width;
}

void MainWindow::DrawToast(const Layout& layout)
{
    painter_.FillRound(layout.toast, 8.0f, theme_.toastBg);
    D2D1_RECT_F textRect = Rect(layout.toast.left + 16.0f, layout.toast.top,
                                toastUndoable_ ? layout.toastUndo.left - 8.0f : layout.toast.right - 16.0f,
                                layout.toast.bottom);
    painter_.Text(toastText_, painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL), textRect, theme_.toastText);

    if (toastUndoable_)
    {
        painter_.Text(i18n::T(L"toast.undo"), painter_.Format(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD),
                      layout.toastUndo, theme_.accent, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

ID2D1Bitmap* MainWindow::IconFor(const std::wstring& path, int size)
{
    auto it = icons_.find(path);
    if (it != icons_.end()) return it->second.Get();

    // Off-screen rendering has no worker; extract inline so screenshots keep
    // their icons.
    if (!gfx_.dc && painter_.Target() && wic_)
    {
        ComPtr<ID2D1Bitmap> bitmap;
        SHFILEINFOW info{};
        if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) && info.hIcon)
        {
            ComPtr<IWICBitmap> source;
            if (SUCCEEDED(wic_->CreateBitmapFromHICON(info.hIcon, source.GetAddressOf())))
            {
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(wic_->CreateFormatConverter(converter.GetAddressOf())) &&
                    SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                                    WICBitmapPaletteTypeCustom)))
                {
                    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    painter_.Target()->CreateBitmapFromWicBitmap(converter.Get(), &props, bitmap.GetAddressOf());
                }
            }
            DestroyIcon(info.hIcon);
        }
        auto inserted = icons_.emplace(path, std::move(bitmap));
        (void)size;
        return inserted.first->second.Get();
    }

    {
        std::lock_guard<std::mutex> guard(iconMutex_);
        if (iconRequested_.insert(path).second) iconQueue_.push_back(path);
    }
    iconCv_.notify_one();
    return nullptr;
}

void MainWindow::StartIconWorker()
{
    if (iconThread_.joinable()) return;
    iconThread_ = std::thread([this]() { IconWorkerMain(); });
}

void MainWindow::StopIconWorker()
{
    {
        std::lock_guard<std::mutex> guard(iconMutex_);
        iconStop_ = true;
    }
    iconCv_.notify_all();
    if (iconThread_.joinable()) iconThread_.join();
}

void MainWindow::IconWorkerMain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    for (;;)
    {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(iconMutex_);
            iconCv_.wait(lock, [this]() { return iconStop_ || !iconQueue_.empty(); });
            if (iconStop_) break;
            path = std::move(iconQueue_.front());
            iconQueue_.pop_front();
        }

        SHFILEINFOW info{};
        if (wic && SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) &&
            info.hIcon)
        {
            ComPtr<IWICBitmap> source;
            if (SUCCEEDED(wic->CreateBitmapFromHICON(info.hIcon, source.GetAddressOf())))
            {
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(wic->CreateFormatConverter(converter.GetAddressOf())) &&
                    SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                                    WICBitmapPaletteTypeCustom)))
                {
                    ComPtr<IWICBitmap> converted;
                    if (SUCCEEDED(wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad,
                                                              converted.GetAddressOf())))
                    {
                        std::lock_guard<std::mutex> guard(iconMutex_);
                        iconReady_.emplace_back(path, std::move(converted));
                    }
                }
            }
            DestroyIcon(info.hIcon);
        }

        if (hwnd_) PostMessageW(hwnd_, WM_APP_ICON_READY, 0, 0);
    }

    CoUninitialize();
}

void MainWindow::DrainReadyIcons()
{
    std::vector<std::pair<std::wstring, ComPtr<IWICBitmap>>> ready;
    {
        std::lock_guard<std::mutex> guard(iconMutex_);
        ready.swap(iconReady_);
    }
    if (!gfx_.dc || ready.empty()) return;

    for (auto& entry : ready)
    {
        if (icons_.count(entry.first)) continue;
        ComPtr<ID2D1Bitmap> bitmap;
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(gfx_.dc->CreateBitmapFromWicBitmap(entry.second.Get(), &props, bitmap.GetAddressOf())))
            icons_[entry.first] = std::move(bitmap);
    }
    Invalidate();
}
void MainWindow::OnMouseMove(int x, int y)
{
    if (!trackingMouse_)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        trackingMouse_ = true;
    }

    if (draggingThumb_)
    {
        Layout layout = ComputeLayout();
        float viewHeight = layout.list.bottom - layout.list.top;
        float trackHeight = layout.scrollbar.bottom - layout.scrollbar.top;
        float thumbHeight = std::max(32.0f, trackHeight * (viewHeight / std::max(1.0f, contentHeightCache_)));
        float travel = trackHeight - thumbHeight;
        if (travel > 0.0f)
        {
            float delta = (static_cast<float>(y) / scale_ - dragStartY_) / travel;
            float maxScroll = std::max(0.0f, contentHeightCache_ - viewHeight);
            ScrollTo(dragStartScroll_ + delta * maxScroll);
        }
        return;
    }

    float fx = static_cast<float>(x) / scale_;
    float fy = static_cast<float>(y) / scale_;
    Layout layout = ComputeLayout();
    if (aboutOpen_)
    {
        int control = 0;
        if (ContainsPoint(layout.dialogOk, fx, fy)) control = 7;
        else if (ContainsPoint(AboutLinkRect(layout), fx, fy)) control = 8;
        if (control != hotControl_ || hotRow_ != -1)
        {
            hotControl_ = control;
            hotRow_ = -1;
            Invalidate();
        }
        return;
    }

    if (selecting_)
    {
        caret_ = CaretFromX(fx, layout);
        Invalidate();
        return;
    }

    int row = RowAtPoint(fy, nullptr);
    int control = 0;
    if (row < 0)
    {
        if (ContainsPoint(layout.rescan, fx, fy)) control = 1;
        else if (ContainsPoint(layout.restoreAll, fx, fy)) control = 2;
        else if (ContainsPoint(layout.language, fx, fy)) control = 3;
        else if (ContainsPoint(layout.autostartCombo, fx, fy) || ContainsPoint(layout.autostartLabel, fx, fy)) control = 4;
        else if (ContainsPoint(layout.search, fx, fy)) control = 5;
        else if (ContainsPoint(layout.aboutButton, fx, fy)) control = 6;
    }
    if (row != hotRow_ || control != hotControl_)
    {
        hotRow_ = row;
        hotControl_ = control;
        Invalidate();
    }
}

int MainWindow::RowAtPoint(float y, float* rowTop) const
{
    if (!model_ || entries_.empty()) return -1;
    Layout layout = ComputeLayout();
    if (y < layout.list.top || y > layout.list.bottom) return -1;
    float contentY = y - layout.list.top + scroll_;
    for (const Entry& entry : entries_)
    {
        if (contentY >= entry.top && contentY < entry.top + entry.height)
        {
            if (rowTop) *rowTop = entry.top;
            return entry.header ? -1 : entry.value;
        }
    }
    return -1;
}

void MainWindow::OnLeftDown(int x, int y)
{
    float fx = static_cast<float>(x) / scale_;
    float fy = static_cast<float>(y) / scale_;
    Layout layout = ComputeLayout();
    focusRingVisible_ = false;

    if (aboutOpen_)
    {
        if (ContainsPoint(layout.dialogOk, fx, fy))
        {
            aboutOpen_ = false;
            Invalidate();
            return;
        }
        if (ContainsPoint(AboutLinkRect(layout), fx, fy))
        {
            ShellExecuteW(nullptr, L"open", kAuthorUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (!ContainsPoint(layout.dialogCard, fx, fy))
        {
            aboutOpen_ = false;
            Invalidate();
        }
        return;
    }

    if (toastVisible_ && toastUndoable_ && ContainsPoint(layout.toastUndo, fx, fy))
    {
        toastVisible_ = false;
        if (callbacks.undo) callbacks.undo();
        Invalidate();
        return;
    }

    if (ContainsPoint(layout.search, fx, fy))
    {
        SetFocusTarget(FocusTarget::Search);
        if (!query_.empty() && fx > layout.search.right - 30.0f)
        {
            query_.clear();
            caret_ = anchor_ = 0;
            UpdateQuery();
            return;
        }
        caret_ = CaretFromX(fx, layout);
        anchor_ = caret_;
        selecting_ = true;
        SetCapture(hwnd_);
        Invalidate();
        return;
    }
    SetFocusTarget(FocusTarget::List);

    if (ContainsPoint(layout.rescan, fx, fy))
    {
        SetFocusTarget(FocusTarget::Rescan);
        if (callbacks.rescan) callbacks.rescan();
        return;
    }
    if (ContainsPoint(layout.restoreAll, fx, fy))
    {
        SetFocusTarget(FocusTarget::RestoreAll);
        if (callbacks.restoreAll) callbacks.restoreAll();
        return;
    }
    if (ContainsPoint(layout.aboutButton, fx, fy))
    {
        SetFocusTarget(FocusTarget::About);
        aboutOpen_ = true;
        aboutFocusLink_ = false;
        aboutTabbed_ = false;
        Invalidate();
        return;
    }
    if (ContainsPoint(layout.language, fx, fy))
    {
        SetFocusTarget(FocusTarget::Language);
        ShowLanguageMenu();
        return;
    }
    if (ContainsPoint(layout.autostartCombo, fx, fy) || ContainsPoint(layout.autostartLabel, fx, fy))
    {
        SetFocusTarget(FocusTarget::Autostart);
        ShowAutostartMenu();
        return;
    }

    D2D1_RECT_F headerRect{};
    int headerGroup = HeaderGroupAtPoint(fy, &headerRect);
    if (headerGroup == 0)
    {
        D2D1_RECT_F action = GroupActionRect(headerRect, headerGroup);
        if (ContainsPoint(action, fx, fy))
        {
            SetFocusTarget(FocusTarget::HideUninstall);
            if (callbacks.hideUninstallGroup) callbacks.hideUninstallGroup(!AllUninstallHidden());
            return;
        }
    }

    int row = RowAtPoint(fy, nullptr);
    if (row < 0 && ContainsPoint(layout.scrollbar, fx, fy))
    {
        draggingThumb_ = true;
        dragStartY_ = fy;
        dragStartScroll_ = scroll_;
        SetCapture(hwnd_);
        return;
    }
    if (row >= 0)
    {
        SetFocusTarget(FocusTarget::List);
        focusRow_ = row;
        // The whole row toggles; the checkbox is just its indicator.
        if (model_)
        {
            const ShortcutItem* item = model_->At(row);
            bool hidden = !(item->managed && item->desiredHidden);
            if (callbacks.toggleItem) callbacks.toggleItem(model_->rows[row], hidden);
        }
        Invalidate();
        return;
    }

    focusRow_ = -1;
    Invalidate();
}

void MainWindow::OnLeftUp(int x, int y)
{
    if (draggingThumb_) ReleaseCapture();
    draggingThumb_ = false;
    if (selecting_)
    {
        selecting_ = false;
        ReleaseCapture();
    }
    (void)x;
    (void)y;
}

void MainWindow::OnMouseWheel(int delta)
{
    if (aboutOpen_) return;
    ScrollTo(scroll_ - delta / 120.0f * kRowHeight * 2.4f);
}

void MainWindow::ScrollTo(float offset)
{
    Layout layout = ComputeLayout();
    float viewHeight = layout.list.bottom - layout.list.top;
    float maxScroll = std::max(0.0f, contentHeightCache_ - viewHeight);
    scroll_ = ClampF(offset, 0.0f, maxScroll);
    DemoteHiddenFocus();
    Invalidate();
}

void MainWindow::EnsureVisible(int rowIndex)
{
    Layout layout = ComputeLayout();
    float viewHeight = layout.list.bottom - layout.list.top;
    for (const Entry& entry : entries_)
    {
        if (entry.header || entry.value != rowIndex) continue;
        if (entry.top < scroll_) scroll_ = entry.top;
        else if (entry.top + entry.height > scroll_ + viewHeight) scroll_ = entry.top + entry.height - viewHeight;
        break;
    }
    ClampScroll();
    DemoteHiddenFocus();
}

void MainWindow::OnKeyDown(WPARAM key)
{
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    focusRingVisible_ = true;

    if (aboutOpen_)
    {
        if (key == VK_ESCAPE)
        {
            aboutOpen_ = false;
            Invalidate();
        }
        else if (key == VK_TAB)
        {
            aboutFocusLink_ = !aboutFocusLink_;
            aboutTabbed_ = true;
            Invalidate();
            PublishAccessibility(ComputeLayout());
            a11y::NotifyFocus(aboutFocusLink_ ? a11y::Element::DialogLink : a11y::Element::DialogOk);
            return;
        }
        else if (key == VK_RETURN || key == VK_SPACE)
        {
            if (aboutFocusLink_)
            {
                ShellExecuteW(nullptr, L"open", kAuthorUrl, nullptr, nullptr, SW_SHOWNORMAL);
            }
            else
            {
                aboutOpen_ = false;
                Invalidate();
            }
        }
        return;
    }

    if (ctrl && key == 'F')
    {
        SetFocusTarget(FocusTarget::Search);
        return;
    }

    if (key == VK_TAB)
    {
        CycleFocus(shift);
        return;
    }

    // The menu key and Shift+F10 open the context menu for whatever has focus.
    if (key == VK_APPS || (key == VK_F10 && shift))
    {
        if (focus_ == FocusTarget::List && model_ && model_->RowCount() > 0)
            ShowContextMenu(focusRow_ >= 0 ? focusRow_ : 0, true);
        else if (focus_ == FocusTarget::Search)
            ShowSearchMenu();
        return;
    }

    switch (focus_)
    {
    case FocusTarget::Search:
        HandleSearchKey(key, ctrl, shift);
        return;
    case FocusTarget::List:
        HandleListKey(key);
        return;
    case FocusTarget::Rescan:
        if ((key == VK_RETURN || key == VK_SPACE) && callbacks.rescan) callbacks.rescan();
        return;
    case FocusTarget::RestoreAll:
        if ((key == VK_RETURN || key == VK_SPACE) && callbacks.restoreAll) callbacks.restoreAll();
        return;
    case FocusTarget::HideUninstall:
        if ((key == VK_RETURN || key == VK_SPACE) && callbacks.hideUninstallGroup)
            callbacks.hideUninstallGroup(!AllUninstallHidden());
        return;
    case FocusTarget::Autostart:
        if (key == VK_RETURN || key == VK_SPACE)
            ShowAutostartMenu();
        else if (key == VK_UP || key == VK_LEFT)
            CycleAutostart(-1);
        else if (key == VK_DOWN || key == VK_RIGHT)
            CycleAutostart(1);
        return;
    case FocusTarget::Language:
        if (key == VK_RETURN || key == VK_SPACE)
            ShowLanguageMenu();
        else if (key == VK_UP || key == VK_LEFT)
            CycleLanguage(-1);
        else if (key == VK_DOWN || key == VK_RIGHT)
            CycleLanguage(1);
        return;
    case FocusTarget::About:
        if (key == VK_RETURN || key == VK_SPACE)
        {
            aboutOpen_ = true;
            aboutFocusLink_ = false;
            aboutTabbed_ = false;
            Invalidate();
        }
        return;
    }
}

void MainWindow::HandleSearchKey(WPARAM key, bool ctrl, bool shift)
{
    auto moveCaret = [this, shift](long delta) {
        long next = static_cast<long>(caret_) + delta;
        long limit = static_cast<long>(query_.size());
        next = next < 0 ? 0 : (next > limit ? limit : next);
        caret_ = static_cast<size_t>(next);
        if (!shift) anchor_ = caret_;
        caretVisible_ = true;
        Invalidate();
    };

    switch (key)
    {
    case VK_ESCAPE:
        if (HasSelection())
        {
            anchor_ = caret_;
            Invalidate();
        }
        else if (!query_.empty())
        {
            query_.clear();
            caret_ = anchor_ = 0;
            UpdateQuery();
        }
        else
        {
            SetFocusTarget(FocusTarget::List);
        }
        return;
    case VK_LEFT:
        moveCaret(-1);
        return;
    case VK_RIGHT:
        moveCaret(1);
        return;
    case VK_HOME:
        caret_ = 0;
        if (!shift) anchor_ = caret_;
        caretVisible_ = true;
        Invalidate();
        return;
    case VK_END:
        caret_ = query_.size();
        if (!shift) anchor_ = caret_;
        caretVisible_ = true;
        Invalidate();
        return;
    case VK_DOWN:
        // Down from the field moves into the result list, like a combo box.
        SetFocusTarget(FocusTarget::List);
        return;
    case VK_BACK:
        if (HasSelection())
            DeleteSelection();
        else if (caret_ > 0)
        {
            query_.erase(caret_ - 1, 1);
            --caret_;
            anchor_ = caret_;
            UpdateQuery();
        }
        return;
    case VK_DELETE:
        if (HasSelection())
            DeleteSelection();
        else if (caret_ < query_.size())
        {
            query_.erase(caret_, 1);
            anchor_ = caret_;
            UpdateQuery();
        }
        return;
    case 'A':
        if (ctrl)
        {
            anchor_ = 0;
            caret_ = query_.size();
            Invalidate();
        }
        return;
    case 'C':
        if (ctrl) CopySelection(false);
        return;
    case 'X':
        if (ctrl) CopySelection(true);
        return;
    case 'V':
        if (ctrl) PasteFromClipboard();
        return;
    default:
        return;
    }
}

void MainWindow::HandleListKey(WPARAM key)
{
    const int rows = model_ ? model_->RowCount() : 0;
    if (rows <= 0) return;

    switch (key)
    {
    case VK_DOWN:
        focusRow_ = std::min(focusRow_ + 1, rows - 1);
        if (focusRow_ < 0) focusRow_ = 0;
        EnsureVisible(focusRow_);
        Invalidate();
        AnnounceFocus();
        return;
    case VK_UP:
        if (focusRow_ <= 0)
            SetFocusTarget(FocusTarget::Search);
        else
        {
            focusRow_ = focusRow_ - 1;
            EnsureVisible(focusRow_);
            Invalidate();
            AnnounceFocus();
        }
        return;
    case VK_HOME:
        focusRow_ = 0;
        EnsureVisible(focusRow_);
        Invalidate();
        AnnounceFocus();
        return;
    case VK_END:
        focusRow_ = rows - 1;
        EnsureVisible(focusRow_);
        Invalidate();
        AnnounceFocus();
        return;
    case VK_NEXT:
        ScrollTo(scroll_ + (ComputeLayout().list.bottom - ComputeLayout().list.top) * 0.9f);
        return;
    case VK_PRIOR:
        ScrollTo(scroll_ - (ComputeLayout().list.bottom - ComputeLayout().list.top) * 0.9f);
        return;
    case VK_SPACE:
        if (focusRow_ >= 0 && focusRow_ < rows)
        {
            const ShortcutItem* item = model_->At(focusRow_);
            if (item)
            {
                bool hidden = !(item->managed && item->desiredHidden);
                if (callbacks.toggleItem) callbacks.toggleItem(model_->rows[focusRow_], hidden);
            }
        }
        return;
    case VK_RETURN:
        if (focusRow_ >= 0 && focusRow_ < rows && callbacks.openLocation)
            callbacks.openLocation(model_->rows[focusRow_]);
        return;
    default:
        return;
    }
}

void MainWindow::SetFocusTarget(FocusTarget target)
{
    focus_ = target;
    searchFocused_ = (target == FocusTarget::Search);

    if (searchFocused_)
    {
        SetTimer(hwnd_, 1, 530, nullptr);
        ImmAssociateContextEx(hwnd_, nullptr, IACE_DEFAULT);
        UpdateImePosition();
    }
    else
    {
        KillTimer(hwnd_, 1);
        composition_.clear();
    }

    if (target != FocusTarget::Search) KillTimer(hwnd_, 3);
    if (target != FocusTarget::List) lastAnnouncedGroup_ = -1;
    if (target == FocusTarget::List && model_ && model_->RowCount() > 0 && focusRow_ < 0)
    {
        focusRow_ = 0;
        EnsureVisible(focusRow_);
    }
    Invalidate();
    AnnounceFocus();
}

void MainWindow::CycleFocus(bool backwards)
{
    static const FocusTarget order[] = {FocusTarget::Search,  FocusTarget::List,     FocusTarget::Rescan,
                                        FocusTarget::RestoreAll, FocusTarget::HideUninstall,
                                        FocusTarget::Autostart, FocusTarget::Language, FocusTarget::About};
    const int count = static_cast<int>(sizeof(order) / sizeof(order[0]));
    int index = 0;
    for (int i = 0; i < count; ++i)
        if (order[i] == focus_) index = i;

    // Step over controls that are currently off-screen.
    for (int step = 0; step < count; ++step)
    {
        index = (index + (backwards ? count - 1 : 1)) % count;
        if (IsFocusTargetAvailable(order[index]))
        {
            SetFocusTarget(order[index]);
            return;
        }
    }
}

void MainWindow::CycleAutostart(int delta)
{
    const AutostartMode modes[] = {AutostartMode::Off, AutostartMode::Once, AutostartMode::Watch};
    int index = 0;
    for (int i = 0; i < 3; ++i)
        if (modes[i] == autostart_) index = i;
    index = (index + delta + 3) % 3;
    SetAutostartMode(modes[index]);
    if (callbacks.autostartChanged) callbacks.autostartChanged(modes[index]);
}

void MainWindow::CycleLanguage(int delta)
{
    const auto& languages = i18n::Available();
    if (languages.empty()) return;

    int index = 0;
    for (int i = 0; i < static_cast<int>(languages.size()); ++i)
        if (EqualsI(languages[i].code, i18n::CurrentCode())) index = i;
    index = (index + delta + static_cast<int>(languages.size())) % static_cast<int>(languages.size());
    ApplyLanguage(languages[index].code);
}

void MainWindow::ApplyLanguage(const std::wstring& code)
{
    if (!i18n::SetLanguage(code)) return;
    RefreshText();
    if (model_)
    {
        model_->Recompute();
        RebuildDisplay();
    }
    Invalidate();
    if (callbacks.languageChanged) callbacks.languageChanged(code);
}

bool MainWindow::HideActionRect(D2D1_RECT_F& rect)
{
    if (!model_ || entries_.empty()) return false;
    if (!entries_[0].header || entries_[0].value != 0) return false;

    Layout layout = ComputeLayout();

    // Mirror DrawList: a header scrolled past the top is pinned, and the pinned
    // one is what the user actually sees (and clicks).
    float top = layout.list.top + entries_[0].top - scroll_;
    if (entries_[0].top < scroll_)
    {
        float pinnedTop = layout.list.top;
        for (int i = 1; i < static_cast<int>(entries_.size()); ++i)
        {
            if (!entries_[i].header) continue;
            float nextTop = layout.list.top + entries_[i].top - scroll_;
            if (nextTop < pinnedTop + kHeaderHeight) pinnedTop = nextTop - kHeaderHeight;
            break;
        }
        top = pinnedTop;
    }

    D2D1_RECT_F header = Rect(layout.list.left, top, layout.list.right, top + kHeaderHeight);
    D2D1_RECT_F action = GroupActionRect(header, 0);

    // Only treat it as focusable when the whole button sits inside the viewport.
    if (action.top < layout.list.top || action.bottom > layout.list.bottom) return false;

    rect = action;
    return true;
}

bool MainWindow::IsFocusTargetAvailable(FocusTarget target)
{
    if (target != FocusTarget::HideUninstall) return true;
    D2D1_RECT_F rect{};
    return HideActionRect(rect);
}

void MainWindow::DemoteHiddenFocus()
{
    if (focus_ == FocusTarget::HideUninstall && !IsFocusTargetAvailable(focus_))
        SetFocusTarget(FocusTarget::List);
}

void MainWindow::DrawFocusRing()
{
    if (aboutOpen_ || !focusRingVisible_) return;

    Layout layout = ComputeLayout();
    D2D1_RECT_F rect{0.0f, 0.0f, 0.0f, 0.0f};
    float radius = 4.0f;

    switch (focus_)
    {
    case FocusTarget::Search:
        rect = layout.search;
        radius = kRadius;
        break;
    case FocusTarget::Rescan:
        rect = layout.rescan;
        break;
    case FocusTarget::RestoreAll:
        rect = layout.restoreAll;
        break;
    case FocusTarget::Autostart:
        rect = layout.autostartCombo;
        break;
    case FocusTarget::Language:
        rect = layout.language;
        radius = kRadius;
        break;
    case FocusTarget::About:
        rect = layout.aboutButton;
        break;
    case FocusTarget::HideUninstall:
        if (!HideActionRect(rect)) return;
        break;
    case FocusTarget::List:
        if (entries_.empty())
        {
            // Nothing to point at, so the list area itself shows the focus.
            rect = Rect(layout.list.left + 8.0f, layout.list.top + 6.0f, layout.list.right - 8.0f,
                        layout.list.bottom - 8.0f);
            break;
        }
        for (const Entry& entry : entries_)
        {
            if (entry.header || entry.value != focusRow_) continue;
            float top = layout.list.top + entry.top - scroll_;
            if (top + entry.height < layout.list.top || top > layout.list.bottom) break;
            rect = Rect(layout.list.left + 8.0f, top + 2.0f, layout.list.right - 8.0f,
                        top + entry.height - 2.0f);
            break;
        }
        break;
    }

    if (rect.right > rect.left) painter_.StrokeRound(rect, radius, theme_.accent, 2.0f);
}
void MainWindow::OnChar(wchar_t ch)
{
    if (!searchFocused_) return;
    if (ch < 32) return;
    InsertText(std::wstring(1, ch));
}

void MainWindow::OnImeStartComposition()
{
    UpdateImePosition();
}

void MainWindow::OnImeComposition(LPARAM flags)
{
    if (!searchFocused_) return;
    HIMC context = ImmGetContext(hwnd_);
    if (!context) return;

    if (flags & GCS_RESULTSTR)
    {
        LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0)
        {
            std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(context, GCS_RESULTSTR, result.data(), bytes);
            composition_.clear();
            InsertText(result);
        }
        else
        {
            composition_.clear();
            UpdateQuery();
        }
    }
    if (flags & GCS_COMPSTR)
    {
        LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
        if (bytes > 0)
        {
            composition_.assign(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(context, GCS_COMPSTR, composition_.data(), bytes);
        }
        else
        {
            composition_.clear();
        }
        UpdateImePosition();
        Invalidate();
    }
    ImmReleaseContext(hwnd_, context);
}

void MainWindow::OnImeEndComposition()
{
    composition_.clear();
    Invalidate();
}

void MainWindow::UpdateImePosition()
{
    if (!hwnd_) return;
    HIMC context = ImmGetContext(hwnd_);
    if (!context) return;

    Layout layout = ComputeLayout();
    IDWriteTextFormat* format = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
    float offset = painter_.TextWidth(query_ + composition_, format);
    LONG x = static_cast<LONG>((layout.search.left + 36.0f + offset) * scale_);
    LONG y = static_cast<LONG>((layout.search.top + 18.0f) * scale_);

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos.x = x;
    composition.ptCurrentPos.y = y;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos.x = x;
    candidate.ptCurrentPos.y = y;
    ImmSetCandidateWindow(context, &candidate);

    LOGFONTW font{};
    font.lfHeight = static_cast<LONG>(-16 * scale_);
    wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    ImmSetCompositionFontW(context, &font);

    ImmReleaseContext(hwnd_, context);
}

void MainWindow::ShowLanguageMenu()
{
    Layout layout = ComputeLayout();
    POINT point{static_cast<LONG>(layout.language.left * scale_),
                static_cast<LONG>(layout.language.bottom * scale_)};
    ClientToScreen(hwnd_, &point);

    HMENU menu = CreatePopupMenu();
    const auto& languages = i18n::Available();
    // Follow the system locale, resolved from the mapping table on every start.
    AppendMenuW(menu, MF_STRING | (i18n::IsFollowingSystem() ? MF_CHECKED : 0), 1000, i18n::T(L"lang.system"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    for (int i = 0; i < static_cast<int>(languages.size()); ++i)
    {
        UINT flags = MF_STRING;
        if (!i18n::IsFollowingSystem() && EqualsI(languages[i].code, i18n::CurrentCode())) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, 1001 + i, languages[i].nativeName.c_str());
    }

    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (command == 1000)
    {
        ApplyLanguage(std::wstring());  // empty means "follow the system"
    }
    else if (command >= 1001 && command < 1001 + static_cast<int>(languages.size()))
    {
        ApplyLanguage(languages[command - 1001].code);
    }
    PostMessage(hwnd_, WM_NULL, 0, 0);
}

void MainWindow::ShowAutostartMenu()
{
    Layout layout = ComputeLayout();
    POINT point{static_cast<LONG>(layout.autostartCombo.left * scale_),
                static_cast<LONG>(layout.autostartCombo.bottom * scale_)};
    ClientToScreen(hwnd_, &point);

    HMENU menu = CreatePopupMenu();
    const AutostartMode modes[] = {AutostartMode::Off, AutostartMode::Once, AutostartMode::Watch};
    const wchar_t* labels[] = {i18n::T(L"autostart.off"), i18n::T(L"autostart.once"), i18n::T(L"autostart.watch")};
    for (int i = 0; i < 3; ++i)
    {
        UINT flags = MF_STRING;
        if (autostart_ == modes[i]) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, 1100 + i, labels[i]);
    }


    if (IsPortableMode())
    {
        // The task lives in Windows, not in the portable folder: say so.
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, i18n::T(L"autostart.portable_note"));
    }
    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (command >= 1100 && command < 1103)
    {
        autostart_ = modes[command - 1100];
        RefreshText();
        if (callbacks.autostartChanged) callbacks.autostartChanged(autostart_);
    }
    PostMessage(hwnd_, WM_NULL, 0, 0);
}

void MainWindow::ShowToast(const std::wstring& text, bool undoable)
{
    toastText_ = text;
    toastUndoable_ = undoable;
    toastVisible_ = true;
    SetTimer(hwnd_, 2, 6000, nullptr);
    Invalidate();
}

bool MainWindow::InitOffscreen()
{
    if (!painter_.Init()) return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic_.GetAddressOf()));
    return true;
}

void MainWindow::RenderTo(ID2D1RenderTarget* target, float width, float height, bool dark)
{
    theme_ = MakeTheme(dark);
    clientWidth_ = static_cast<int>(width);
    clientHeight_ = static_cast<int>(height);
    scale_ = 1.0f;
    painter_.SetTarget(target);
    painter_.SetScale(1.0f);
    if (model_) model_->Recompute();
    RebuildDisplay();
    if (previewScroll_ >= 0.0f)
    {
        scroll_ = previewScroll_;
        ClampScroll();
    }
    RefreshText();
    Draw();
}

void MainWindow::SetAboutOpen(bool open)
{
    aboutOpen_ = open;
    Invalidate();
}

D2D1_RECT_F MainWindow::AboutLinkRect(const Layout& layout)
{
    const auto& card = layout.dialogCard;
    const float left = card.left + kPad;
    IDWriteTextFormat* format = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
    float labelWidth = painter_.TextWidth(i18n::T(L"about.author"), format, 200.0f);
    float nameWidth = painter_.TextWidth(i18n::T(L"about.author_name"), format, 300.0f);
    // DWrite drops trailing whitespace from text metrics, so the gap has to be
    // explicit rather than relying on the trailing space in the label.
    float gap = 5.0f;
    return Rect(left + labelWidth + gap, card.top + 118.0f, left + labelWidth + gap + nameWidth + 2.0f,
                card.top + 140.0f);
}

void MainWindow::DrawAbout(const Layout& layout)
{
    float width = static_cast<float>(clientWidth_) / scale_;
    float height = static_cast<float>(clientHeight_) / scale_;
    painter_.FillRect(Rect(0.0f, 0.0f, width, height), theme_.scrim);

    const D2D1_RECT_F card = layout.dialogCard;
    painter_.FillRound(card, 8.0f, theme_.dialogBg);
    painter_.StrokeRound(card, 8.0f, theme_.dialogStroke, 1.0f);

    const float left = card.left + kPad;
    const float right = card.right - kPad;

    painter_.Text(i18n::T(L"about.title"),
                  painter_.Format(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI Variable Display"),
                  Rect(left, card.top + 18.0f, right, card.top + 50.0f), theme_.text);

    painter_.Text(i18n::T(L"about.free"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(left, card.top + 60.0f, right, card.top + 82.0f), theme_.text);
    painter_.Text(i18n::T(L"about.license"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(left, card.top + 84.0f, right, card.top + 106.0f), theme_.textDim);

    D2D1_RECT_F link = AboutLinkRect(layout);
    painter_.Text(i18n::T(L"about.author"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(left, link.top, link.left, link.bottom), theme_.textDim);
    painter_.Text(i18n::T(L"about.author_name"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL), link,
                  theme_.link);
    painter_.Text(kAuthorDisplay, painter_.Format(11.5f, DWRITE_FONT_WEIGHT_NORMAL),
                  Rect(left, card.top + 142.0f, right, card.top + 162.0f), theme_.textFaint);
    if (hotControl_ == 8 || aboutFocusLink_)
        painter_.Line(D2D1::Point2F(link.left, link.bottom - 2.0f),
                      D2D1::Point2F(link.right, link.bottom - 2.0f), theme_.link, 1.0f);

    painter_.FillRound(layout.dialogOk, 4.0f, theme_.accent);
    painter_.Text(i18n::T(L"about.ok"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), layout.dialogOk,
                  theme_.onAccent, DWRITE_TEXT_ALIGNMENT_CENTER);

    if (focusRingVisible_)
    {
        if (aboutFocusLink_)
            painter_.StrokeRound(Rect(link.left - 5.0f, link.top + 1.0f, link.right + 5.0f, link.bottom - 1.0f),
                                 3.0f, theme_.accent, 2.0f);
        else
            painter_.StrokeRound(Rect(layout.dialogOk.left - 3.0f, layout.dialogOk.top - 3.0f,
                                      layout.dialogOk.right + 3.0f, layout.dialogOk.bottom + 3.0f),
                                 6.0f, theme_.text, 1.5f);
    }
}
namespace {

bool WriteClipboardText(HWND hwnd, const std::wstring& text)
{
    if (text.empty()) return false;
    if (!OpenClipboard(hwnd)) return false;

    bool ok = false;
    if (EmptyClipboard())
    {
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory)
        {
            void* target = GlobalLock(memory);
            if (target)
            {
                memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory))
                    ok = true;
                else
                    GlobalFree(memory);
            }
            else
            {
                GlobalFree(memory);
            }
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring ReadClipboardText(HWND hwnd)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd)) return {};

    std::wstring text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
    {
        if (const wchar_t* source = static_cast<const wchar_t*>(GlobalLock(data)))
        {
            text = source;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();

    if (text.size() > kMaxClipboardLength) text.resize(kMaxClipboardLength);

    // A search box is a single line: fold line breaks and tabs into spaces.
    std::wstring cleaned;
    cleaned.reserve(text.size());
    for (wchar_t ch : text)
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
        {
            if (!cleaned.empty() && cleaned.back() != L' ') cleaned.push_back(L' ');
        }
        else
        {
            cleaned.push_back(ch);
        }
    }
    while (!cleaned.empty() && cleaned.back() == L' ') cleaned.pop_back();
    size_t start = 0;
    while (start < cleaned.size() && cleaned[start] == L' ') ++start;
    return cleaned.substr(start);
}

}  // namespace

std::wstring MainWindow::SelectedText() const
{
    size_t from = std::min(caret_, anchor_);
    size_t to = std::max(caret_, anchor_);
    if (from >= query_.size()) return {};
    if (to > query_.size()) to = query_.size();
    return query_.substr(from, to - from);
}

void MainWindow::ClampCaret()
{
    if (caret_ > query_.size()) caret_ = query_.size();
    if (anchor_ > query_.size()) anchor_ = query_.size();
}

void MainWindow::DeleteSelection()
{
    if (!HasSelection()) return;
    size_t from = std::min(caret_, anchor_);
    size_t to = std::max(caret_, anchor_);
    if (from > query_.size()) from = query_.size();
    if (to > query_.size()) to = query_.size();
    if (to <= from)
    {
        caret_ = anchor_ = from;
        return;
    }
    query_.erase(from, to - from);
    caret_ = anchor_ = from;
    caretVisible_ = true;
    UpdateQuery();
}

void MainWindow::InsertText(const std::wstring& incoming)
{
    if (incoming.empty()) return;

    // Keep the field bounded: every keystroke measures prefixes of this string,
    // so an accidental paste of a whole document would stall the UI.
    size_t from = std::min(caret_, anchor_);
    size_t to = std::max(caret_, anchor_);
    if (from > query_.size()) from = query_.size();
    if (to > query_.size()) to = query_.size();
    if (to < from) to = from;

    size_t room = kMaxQueryLength > (query_.size() - (to - from)) ? kMaxQueryLength - (query_.size() - (to - from)) : 0;
    std::wstring text = incoming.size() > room ? incoming.substr(0, room) : incoming;
    if (text.empty() && to == from) return;

    query_.erase(from, to - from);
    query_.insert(from, text);
    caret_ = anchor_ = from + text.size();
    caretVisible_ = true;
    UpdateQuery();
    UpdateImePosition();
}

void MainWindow::CopySelection(bool cut)
{
    std::wstring text = HasSelection() ? SelectedText() : query_;
    if (text.empty()) return;
    if (!WriteClipboardText(hwnd_, text)) return;
    if (cut && HasSelection()) DeleteSelection();
}

void MainWindow::PasteFromClipboard()
{
    std::wstring text = ReadClipboardText(hwnd_);
    if (!text.empty()) InsertText(text);
}

size_t MainWindow::CaretFromX(float x, const Layout& layout)
{
    // Hit-testing one layout is exact (it accounts for kerning) and constant
    // time; measuring every prefix was O(n) layouts per mouse move.
    IDWriteTextFormat* format = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
    if (!format || !painter_.DWrite()) return query_.size();

    const float width = std::max(1.0f, layout.searchText.right - layout.searchText.left);
    const float height = std::max(1.0f, layout.searchText.bottom - layout.searchText.top);

    ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED(painter_.DWrite()->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()), format,
                                                   width, height, textLayout.GetAddressOf())))
        return query_.size();
    textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(textLayout->HitTestPoint(x - layout.searchText.left, height * 0.5f, &trailing, &inside, &metrics)))
        return query_.size();

    size_t index = metrics.textPosition + (trailing ? 1u : 0u);
    return std::min<size_t>(index, query_.size());
}

void MainWindow::OnRightClick(int x, int y)
{
    if (aboutOpen_) return;

    float fx = static_cast<float>(x) / scale_;
    float fy = static_cast<float>(y) / scale_;
    Layout layout = ComputeLayout();

    int row = RowAtPoint(fy, nullptr);
    if (row >= 0)
    {
        ShowContextMenu(row);
        return;
    }
    if (!ContainsPoint(layout.search, fx, fy)) return;

    if (!searchFocused_) SetSearchFocus(true);

    size_t position = CaretFromX(fx, layout);
    if (!HasSelection() || position < std::min(caret_, anchor_) || position > std::max(caret_, anchor_))
    {
        caret_ = position;
        anchor_ = position;
    }
    Invalidate();
    ShowSearchMenu();
}

void MainWindow::ShowContextMenu(int rowIndex, bool fromKeyboard)
{
    if (!model_) return;
    const ShortcutItem* item = model_->At(rowIndex);
    if (!item) return;

    focusRow_ = rowIndex;
    SetFocusTarget(FocusTarget::List);

    const bool willHide = !(item->managed && item->desiredHidden);
    const bool hasTarget = !item->target.empty();

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1301, willHide ? i18n::T(L"menu.hide") : i18n::T(L"menu.show"));
    // The primary command is the menu default, which renders bold.
    SetMenuDefaultItem(menu, 1301, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1302, i18n::T(L"menu.open_shortcut"));
    AppendMenuW(menu, MF_STRING | (hasTarget ? 0 : MF_GRAYED), 1303, i18n::T(L"menu.open_target"));

    POINT point{};
    if (fromKeyboard)
    {
        // Anchor to the focused row rather than wherever the pointer is.
        Layout layout = ComputeLayout();
        float top = layout.list.top + 8.0f;
        for (const Entry& entry : entries_)
        {
            if (entry.header || entry.value != focusRow_) continue;
            top = layout.list.top + entry.top - scroll_;
            break;
        }
        point.x = static_cast<LONG>(layout.list.left * scale_);
        point.y = static_cast<LONG>(top * scale_);
        ClientToScreen(hwnd_, &point);
    }
    else
    {
        GetCursorPos(&point);
    }
    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessage(hwnd_, WM_NULL, 0, 0);

    switch (command)
    {
    case 1301:
        if (callbacks.toggleItem) callbacks.toggleItem(model_->rows[rowIndex], willHide);
        break;
    case 1302:
        if (callbacks.openLocation) callbacks.openLocation(model_->rows[rowIndex]);
        break;
    case 1303:
    {
        std::wstring arguments = L"/select,\"" + item->target + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    default:
        break;
    }
}

void MainWindow::ShowSearchMenu()
{
    const bool hasSelection = HasSelection();
    const bool hasText = !query_.empty();
    const bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), 1201, i18n::T(L"edit.cut"));
    AppendMenuW(menu, MF_STRING | ((hasSelection || hasText) ? 0 : MF_GRAYED), 1202, i18n::T(L"edit.copy"));
    AppendMenuW(menu, MF_STRING | (canPaste ? 0 : MF_GRAYED), 1203, i18n::T(L"edit.paste"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (hasText ? 0 : MF_GRAYED), 1204, i18n::T(L"edit.select_all"));
    AppendMenuW(menu, MF_STRING | (hasText ? 0 : MF_GRAYED), 1205, i18n::T(L"edit.clear"));

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessage(hwnd_, WM_NULL, 0, 0);

    switch (command)
    {
    case 1201:
        CopySelection(true);
        break;
    case 1202:
        CopySelection(false);
        break;
    case 1203:
        PasteFromClipboard();
        break;
    case 1204:
        anchor_ = 0;
        caret_ = query_.size();
        Invalidate();
        break;
    case 1205:
        query_.clear();
        caret_ = anchor_ = 0;
        UpdateQuery();
        break;
    default:
        break;
    }
}
void MainWindow::SetPreviewFocus(int index)
{
    if (index < 0) index = 0;
    if (index > static_cast<int>(FocusTarget::About)) index = static_cast<int>(FocusTarget::About);
    SetFocusTarget(static_cast<FocusTarget>(index));
    focusRingVisible_ = true;
    Invalidate();
}

void MainWindow::SetPreviewQuery(const std::wstring& text)
{
    query_ = text.size() > kMaxQueryLength ? text.substr(0, kMaxQueryLength) : text;
    anchor_ = 0;
    caret_ = text.size();
    searchFocused_ = true;
    caretVisible_ = true;
    UpdateQuery();
}
void MainWindow::PublishAccessibility(const Layout& layout)
{
    if (!hwnd_) return;

    POINT origin{0, 0};
    ClientToScreen(hwnd_, &origin);
    auto toScreen = [&](const D2D1_RECT_F& rect) {
        RECT out{};
        out.left = origin.x + static_cast<LONG>(rect.left * scale_);
        out.top = origin.y + static_cast<LONG>(rect.top * scale_);
        out.right = origin.x + static_cast<LONG>(rect.right * scale_);
        out.bottom = origin.y + static_cast<LONG>(rect.bottom * scale_);
        return out;
    };

    std::vector<a11y::SnapshotElement> elements;

    a11y::SnapshotElement root;
    root.id = a11y::Element::Root;
    root.name = i18n::T(L"a11y.app");
    root.bounds = toScreen(Rect(0.0f, 0.0f, static_cast<float>(clientWidth_) / scale_,
                                static_cast<float>(clientHeight_) / scale_));
    root.controlType = UIA_WindowControlTypeId;
    root.focusable = false;
    root.focused = false;
    elements.push_back(root);

    auto add = [&](a11y::Element id, const wchar_t* name, const D2D1_RECT_F& rect, int controlType,
                   bool focused) {
        a11y::SnapshotElement element;
        element.id = id;
        element.name = name;
        element.bounds = toScreen(rect);
        element.controlType = controlType;
        element.focused = focused;
        elements.push_back(std::move(element));
    };

    add(a11y::Element::Search, i18n::T(L"a11y.search"), layout.search, UIA_EditControlTypeId,
        focus_ == FocusTarget::Search);
    add(a11y::Element::Rescan, i18n::T(L"a11y.rescan"), layout.rescan, UIA_ButtonControlTypeId,
        focus_ == FocusTarget::Rescan);
    add(a11y::Element::RestoreAll, i18n::T(L"a11y.restoreAll"), layout.restoreAll, UIA_ButtonControlTypeId,
        focus_ == FocusTarget::RestoreAll);

    D2D1_RECT_F groupAction{0.0f, 0.0f, 0.0f, 0.0f};
    if (HideActionRect(groupAction))
    {
        add(a11y::Element::HideUninstall, i18n::T(L"a11y.hideGroup"), groupAction, UIA_ButtonControlTypeId,
            focus_ == FocusTarget::HideUninstall);
    }

    add(a11y::Element::Autostart, i18n::T(L"a11y.autostart"), layout.autostartCombo,
        UIA_ComboBoxControlTypeId, focus_ == FocusTarget::Autostart);
    add(a11y::Element::Language, i18n::T(L"a11y.language"), layout.language, UIA_ComboBoxControlTypeId,
        focus_ == FocusTarget::Language);
    add(a11y::Element::About, i18n::T(L"a11y.about"), layout.aboutButton, UIA_ButtonControlTypeId,
        focus_ == FocusTarget::About);

    if (aboutOpen_)
    {
        // Only the dialog is exposed while it is open; the window behind it is
        // not reachable.
        elements.clear();
        elements.push_back(root);

        const D2D1_RECT_F card = layout.dialogCard;

        a11y::SnapshotElement dialog;
        dialog.id = a11y::Element::Dialog;
        // One utterance on entry: the page name and its body. A focused element
        // whose HasKeyboardFocus is false is ignored by screen readers, so this
        // one has to own the focus.
        dialog.name = std::wstring(i18n::T(L"a11y.aboutDialog")) + L"\u3002" + i18n::T(L"a11y.aboutBody");
        dialog.bounds = toScreen(card);
        dialog.controlType = UIA_PaneControlTypeId;
        dialog.focused = !aboutTabbed_;
        elements.push_back(dialog);

        a11y::SnapshotElement body;
        body.id = a11y::Element::DialogText;
        body.name = i18n::T(L"a11y.aboutBody");
        body.bounds = toScreen(Rect(card.left + kPad, card.top + 60.0f, card.right - kPad, card.top + 140.0f));
        body.controlType = UIA_TextControlTypeId;
        body.focusable = true;
        body.focused = false;
        elements.push_back(body);

        a11y::SnapshotElement link;
        link.id = a11y::Element::DialogLink;
        link.name = i18n::T(L"a11y.aboutLink");
        link.bounds = toScreen(Rect(card.left + kPad, card.top + 118.0f, card.right - kPad, card.top + 162.0f));
        link.controlType = UIA_HyperlinkControlTypeId;
        link.focusable = true;
        link.focused = aboutTabbed_ && aboutFocusLink_;
        elements.push_back(link);

        a11y::SnapshotElement ok;
        ok.id = a11y::Element::DialogOk;
        ok.name = i18n::T(L"about.ok");
        ok.bounds = toScreen(layout.dialogOk);
        ok.controlType = UIA_ButtonControlTypeId;
        ok.focused = aboutTabbed_ && !aboutFocusLink_;
        elements.push_back(ok);

        a11y::Publish(elements);
        if (!publishedAbout_)
        {
            publishedAbout_ = true;
            a11y::NotifyFocus(a11y::Element::Dialog);
        }
        return;
    }

    if (publishedAbout_)
    {
        publishedAbout_ = false;
        a11y::NotifyFocus(a11y::Element::About);
    }

    const bool listEmpty = !model_ || model_->RowCount() == 0;
    std::wstring listName = i18n::T(L"a11y.list");
    if (listEmpty && focus_ == FocusTarget::List)
    {
        listName = query_.empty() ? i18n::T(L"status.empty") : i18n::Tf(L"empty.search", {query_});
    }
    // An empty list must not announce itself as a list: a screen reader enters
    // it looking for items, finds none and says nothing. With no items the
    // control is just the sentence it displays.
    add(a11y::Element::List, listName.c_str(), layout.list,
        listEmpty ? UIA_TextControlTypeId : UIA_ListControlTypeId, listEmpty && focus_ == FocusTarget::List);

    // The focused row is its own UIA element; materialising all 300+ rows would
    // make the tree far too heavy, so only the current one exists.
    const ShortcutItem* row = (model_ && focus_ == FocusTarget::List) ? model_->At(focusRow_) : nullptr;
    if (row)
    {
        const wchar_t* state = (row->managed && row->desiredHidden)
                                   ? i18n::T(L"a11y.state.hidden")
                                   : (row->hiddenOnDisk ? i18n::T(L"a11y.state.external")
                                                         : i18n::T(L"a11y.state.visible"));

        // Always name the group: the snapshot is rebuilt on every repaint, so a
        // one-shot group prefix would be gone by the time a screen reader asks.
        const wchar_t* group = row->isUninstall ? i18n::T(L"a11y.group.uninstall") : i18n::T(L"a11y.group.other");
        std::wstring rowName = i18n::Tf(L"a11y.rowGrouped", {group, row->displayName, state});

        D2D1_RECT_F rowRect = layout.list;
        for (const Entry& entry : entries_)
        {
            if (entry.header || entry.value != focusRow_) continue;
            float top = layout.list.top + entry.top - scroll_;
            rowRect = Rect(layout.list.left, top, layout.list.right, top + entry.height);
            break;
        }

        a11y::SnapshotElement rowElement;
        rowElement.id = a11y::Element::ListRow;
        rowElement.name = rowName;
        rowElement.help = row->path;
        rowElement.bounds = toScreen(rowRect);
        rowElement.controlType = UIA_ListItemControlTypeId;
        rowElement.focused = true;
        // Stable per shortcut, so the runtime id changes when the row changes.
        rowElement.identity = static_cast<long long>(std::hash<std::wstring>{}(row->path) & 0x7FFFFFFF);
        rowElement.positionInSet = focusRow_ + 1;
        rowElement.sizeOfSet = model_->RowCount();
        elements.push_back(std::move(rowElement));
    }

    a11y::Publish(elements);
}

void MainWindow::AnnounceFocus()
{
    if (!hwnd_) return;

    a11y::Element element = a11y::Element::Root;
    switch (focus_)
    {
    case FocusTarget::Search: element = a11y::Element::Search; break;
    case FocusTarget::List: element = a11y::Element::ListRow; break;
    case FocusTarget::Rescan: element = a11y::Element::Rescan; break;
    case FocusTarget::RestoreAll: element = a11y::Element::RestoreAll; break;
    case FocusTarget::HideUninstall: element = a11y::Element::HideUninstall; break;
    case FocusTarget::Autostart: element = a11y::Element::Autostart; break;
    case FocusTarget::Language: element = a11y::Element::Language; break;
    case FocusTarget::About: element = a11y::Element::About; break;
    }

    // Refresh the snapshot first so the provider reports the new focused flag
    // and, for the list, the new row text.
    PublishAccessibility(ComputeLayout());
    a11y::NotifyFocus(element);

    // An empty result list has no items for a screen reader to land on, so the
    // focus event alone produces silence. Announce the message directly.
    if (focus_ == FocusTarget::List && (!model_ || model_->RowCount() == 0))
    {
        // Spoken wording differs from the on-screen text on purpose.
        a11y::Notify(query_.empty() ? i18n::T(L"a11y.emptyList")
                                    : i18n::Tf(L"a11y.emptySearch", {query_}));
    }
}
void MainWindow::Announce(const std::wstring& text)
{
    a11y::Notify(text);
}
