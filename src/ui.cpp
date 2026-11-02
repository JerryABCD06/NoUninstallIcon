#include "ui.h"

#include "i18n.h"
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
    case WM_LBUTTONDBLCLK:
    {
        float y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_;
        int row = RowAtPoint(y, nullptr);
        if (row >= 0 && callbacks.openLocation) callbacks.openLocation(model_->rows[row]);
        return 0;
    }
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wparam);
        return 0;
    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wparam));
        return 0;
    case WM_TIMER:
        OnTimer(wparam);
        return 0;
    case WM_APP_SCAN_DONE:
        if (callbacks.scanDone) callbacks.scanDone();
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
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = static_cast<LONG>(820 * scale_);
        info->ptMinTrackSize.y = static_cast<LONG>(560 * scale_);
        return 0;
    }
    case WM_DESTROY:
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

    ApplyTheme();
    RefreshText();
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
    searchFocused_ = focused;
    caretVisible_ = true;
    if (focused)
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
    Invalidate();
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

    std::wstring label = i18n::T(L"toggle.autostart");
    if (autostart_ != AutostartMode::Off) label += L"  \u00B7  " + ModeLabel(autostart_);
    IDWriteTextFormat* labelFormat = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    autostartLabelWidth_ = std::min(320.0f, painter_.TextWidth(label, labelFormat, 320.0f));

    IDWriteTextFormat* aboutFormat = painter_.Format(12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    aboutButtonWidth_ = painter_.TextWidth(i18n::T(L"about.button"), aboutFormat, 260.0f) + 44.0f;

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

    float rightEdge = width - kPad;
    layout.rescan = Rect(rightEdge - 108.0f, kToolbarTop, rightEdge, kToolbarTop + kToolbarHeight);
    layout.restoreAll = Rect(layout.rescan.left - 8.0f - 176.0f, kToolbarTop, layout.rescan.left - 8.0f,
                             kToolbarTop + kToolbarHeight);

    layout.list = Rect(0.0f, 136.0f, width, height - kFooterHeight);
    layout.footer = Rect(0.0f, height - kFooterHeight, width, height);
    layout.scrollbar = Rect(width - 12.0f, layout.list.top + 4.0f, width - 4.0f, layout.list.bottom - 4.0f);

    float midY = height - kFooterHeight * 0.5f;
    layout.language = Rect(rightEdge - langButtonWidth_, midY - 15.0f, rightEdge, midY + 15.0f);

    float trackRight = layout.language.left - 20.0f;
    layout.autostartTrack = Rect(trackRight - 40.0f, midY - 10.0f, trackRight, midY + 10.0f);
    float labelRight = layout.autostartTrack.left - 10.0f;
    layout.autostartLabel = Rect(labelRight - autostartLabelWidth_, midY - 12.0f, labelRight, midY + 12.0f);

    float toastWidth = std::min(560.0f, width - 64.0f);
    layout.toast = Rect((width - toastWidth) * 0.5f, layout.footer.top - 60.0f,
                        (width + toastWidth) * 0.5f, layout.footer.top - 16.0f);
    layout.toastUndo = Rect(layout.toast.right - 96.0f, layout.toast.top + 8.0f, layout.toast.right - 16.0f,
                            layout.toast.bottom - 8.0f);

    layout.aboutButton = Rect(width - kPad - aboutButtonWidth_, 12.0f, width - kPad, 48.0f);

    const float dialogWidth = 440.0f;
    const float dialogHeight = 232.0f;
    layout.dialogCard = Rect((width - dialogWidth) * 0.5f, (height - dialogHeight) * 0.5f,
                             (width + dialogWidth) * 0.5f, (height + dialogHeight) * 0.5f);
    layout.dialogOk = Rect(layout.dialogCard.right - kPad - 88.0f, layout.dialogCard.bottom - kPad - 32.0f,
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

    std::wstring shown = query_;
    bool empty = shown.empty() && composition_.empty();
    IDWriteTextFormat* textFormat = painter_.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL);
    D2D1_RECT_F textRect = Rect(layout.search.left + 34.0f, layout.search.top, layout.search.right - 30.0f,
                                layout.search.bottom);

    if (empty)
    {
        painter_.Text(i18n::T(L"search.placeholder"), textFormat, textRect, theme_.textFaint);
    }
    else
    {
        painter_.Text(shown, textFormat, textRect, theme_.text);
        if (!composition_.empty())
        {
            float offset = painter_.TextWidth(shown, textFormat);
            D2D1_RECT_F compRect = Rect(textRect.left + offset, textRect.top, textRect.right, textRect.bottom);
            painter_.Text(composition_, textFormat, compRect, theme_.text);
        }
    }

    if (searchFocused_ && caretVisible_)
    {
        std::wstring measure = shown + composition_;
        float offset = painter_.TextWidth(measure, textFormat);
        float caretX = ClampF(textRect.left + offset + 1.0f, textRect.left, layout.search.right - 8.0f);
        float caretTop = layout.search.top + 9.0f;
        painter_.Line(D2D1::Point2F(caretX, caretTop), D2D1::Point2F(caretX, layout.search.bottom - 9.0f),
                      theme_.accent, 1.2f);
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
        D2D1_COLOR_F background = theme_.windowBg;
        background.a = theme_.dark ? 0.92f : 0.92f;
        painter_.FillRect(rect, background);
    }

    const wchar_t* title = group == 0 ? i18n::T(L"group.uninstall") : i18n::T(L"group.other");
    int count = group == 0 ? (model_ ? model_->uninstallTotal : 0) : (model_ ? model_->otherTotal : 0);

    D2D1_RECT_F textRect = Rect(rect.left + kCheckLeft, rect.top, rect.right - 160.0f, rect.bottom);
    painter_.Text(title, painter_.Format(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), textRect, theme_.textDim);

    D2D1_RECT_F countRect = Rect(rect.right - 160.0f, rect.top, rect.right - kPad, rect.bottom);
    painter_.Text(i18n::Tf(L"group.items", {std::to_wstring(count)}),
                  painter_.Format(11.5f, DWRITE_FONT_WEIGHT_NORMAL), countRect, theme_.textFaint,
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
    for (const auto& item : model_->items)
    {
        if (!item.isUninstall) continue;
        any = true;
        if (!(item.managed && item.desiredHidden)) return false;
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
        painter_.Target()->DrawBitmap(icon, iconRect, 0.95f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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

    // Autostart switch.
    bool on = autostart_ != AutostartMode::Off;
    painter_.FillRound(layout.autostartTrack, 10.0f, on ? theme_.toggleOn : theme_.toggleOff);
    if (!on) painter_.StrokeRound(layout.autostartTrack, 10.0f, theme_.toggleOffBorder, 1.0f);
    float knobRadius = 6.0f;
    float knobX = on ? layout.autostartTrack.right - 10.0f : layout.autostartTrack.left + 10.0f;
    painter_.Circle(D2D1::Point2F(knobX, layout.autostartTrack.top + 10.0f), knobRadius,
                    on ? theme_.onAccent : theme_.toggleKnob);

    std::wstring label = i18n::T(L"toggle.autostart");
    if (autostart_ != AutostartMode::Off) label += L"  \u00B7  " + ModeLabel(autostart_);
    painter_.Text(label, painter_.Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL), layout.autostartLabel,
                  theme_.textDim, DWRITE_TEXT_ALIGNMENT_TRAILING);
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
    if (!wic_ || !painter_.Target())
    {
        return nullptr;
    }

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

    int row = RowAtPoint(fy, nullptr);
    int control = 0;
    if (row < 0)
    {
        if (ContainsPoint(layout.rescan, fx, fy)) control = 1;
        else if (ContainsPoint(layout.restoreAll, fx, fy)) control = 2;
        else if (ContainsPoint(layout.language, fx, fy)) control = 3;
        else if (ContainsPoint(layout.autostartTrack, fx, fy) || ContainsPoint(layout.autostartLabel, fx, fy)) control = 4;
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
        SetSearchFocus(true);
        if (!query_.empty() && fx > layout.search.right - 30.0f)
        {
            query_.clear();
            UpdateQuery();
        }
        return;
    }
    SetSearchFocus(false);

    if (ContainsPoint(layout.rescan, fx, fy))
    {
        if (callbacks.rescan) callbacks.rescan();
        return;
    }
    if (ContainsPoint(layout.restoreAll, fx, fy))
    {
        if (callbacks.restoreAll) callbacks.restoreAll();
        return;
    }
    if (ContainsPoint(layout.aboutButton, fx, fy))
    {
        aboutOpen_ = true;
        Invalidate();
        return;
    }
    if (ContainsPoint(layout.language, fx, fy))
    {
        ShowLanguageMenu();
        return;
    }
    if (ContainsPoint(layout.autostartLabel, fx, fy))
    {
        ShowAutostartMenu();
        return;
    }
    if (ContainsPoint(layout.autostartTrack, fx, fy))
    {
        AutostartMode next = autostart_ == AutostartMode::Off ? AutostartMode::Once : AutostartMode::Off;
        autostart_ = next;
        RefreshText();
        if (callbacks.autostartChanged) callbacks.autostartChanged(next);
        return;
    }

    D2D1_RECT_F headerRect{};
    int headerGroup = HeaderGroupAtPoint(fy, &headerRect);
    if (headerGroup == 0)
    {
        D2D1_RECT_F action = GroupActionRect(headerRect, headerGroup);
        if (ContainsPoint(action, fx, fy))
        {
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
        focusRow_ = row;
        bool onCheckbox = fx >= kCheckLeft - 8.0f && fx <= kCheckLeft + kCheckSize + 8.0f;
        if (onCheckbox && model_)
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
}

void MainWindow::OnKeyDown(WPARAM key)
{
    if (aboutOpen_)
    {
        if (key == VK_ESCAPE || key == VK_RETURN || key == VK_SPACE)
        {
            aboutOpen_ = false;
            Invalidate();
        }
        return;
    }
    if (searchFocused_)
    {
        if (key == VK_ESCAPE)
        {
            if (!query_.empty())
            {
                query_.clear();
                UpdateQuery();
            }
            else
            {
                SetSearchFocus(false);
            }
            return;
        }
        if (key == VK_BACK && composition_.empty() && !query_.empty())
        {
            query_.pop_back();
            UpdateQuery();
            return;
        }
    }

    switch (key)
    {
    case VK_DOWN:
        if (model_ && model_->RowCount() > 0)
        {
            focusRow_ = std::min(focusRow_ + 1, model_->RowCount() - 1);
            if (focusRow_ < 0) focusRow_ = 0;
            EnsureVisible(focusRow_);
            Invalidate();
        }
        return;
    case VK_UP:
        if (model_ && model_->RowCount() > 0)
        {
            focusRow_ = std::max(focusRow_ - 1, 0);
            EnsureVisible(focusRow_);
            Invalidate();
        }
        return;
    case VK_NEXT:
        ScrollTo(scroll_ + (ComputeLayout().list.bottom - ComputeLayout().list.top) * 0.9f);
        return;
    case VK_PRIOR:
        ScrollTo(scroll_ - (ComputeLayout().list.bottom - ComputeLayout().list.top) * 0.9f);
        return;
    case VK_HOME:
        ScrollTo(0.0f);
        return;
    case VK_END:
        ScrollTo(contentHeightCache_);
        return;
    case VK_SPACE:
        if (!searchFocused_ && model_ && focusRow_ >= 0 && focusRow_ < model_->RowCount())
        {
            const ShortcutItem* item = model_->At(focusRow_);
            bool hidden = !(item->managed && item->desiredHidden);
            if (callbacks.toggleItem) callbacks.toggleItem(model_->rows[focusRow_], hidden);
        }
        return;
    case VK_RETURN:
        if (model_ && focusRow_ >= 0 && focusRow_ < model_->RowCount() && callbacks.openLocation)
            callbacks.openLocation(model_->rows[focusRow_]);
        return;
    case 'F':
        if (GetKeyState(VK_CONTROL) & 0x8000) SetSearchFocus(true);
        return;
    default:
        break;
    }
}

void MainWindow::OnChar(wchar_t ch)
{
    if (!searchFocused_) return;
    if (ch < 32) return;
    query_.push_back(ch);
    UpdateQuery();
    UpdateImePosition();
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
            query_ += result;
        }
        composition_.clear();
        UpdateQuery();
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
    for (int i = 0; i < static_cast<int>(languages.size()); ++i)
    {
        UINT flags = MF_STRING;
        if (EqualsI(languages[i].code, i18n::CurrentCode())) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, 1000 + i, languages[i].nativeName.c_str());
    }

    SetForegroundWindow(hwnd_);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (command >= 1000 && command < 1000 + static_cast<int>(languages.size()))
    {
        const std::wstring code = languages[command - 1000].code;
        if (i18n::SetLanguage(code))
        {
            RefreshText();
            if (model_)
            {
                model_->Recompute();
                RebuildDisplay();
            }
            Invalidate();
            if (callbacks.languageChanged) callbacks.languageChanged(code);
        }
    }
    PostMessage(hwnd_, WM_NULL, 0, 0);
}

void MainWindow::ShowAutostartMenu()
{
    Layout layout = ComputeLayout();
    POINT point{static_cast<LONG>(layout.autostartLabel.left * scale_),
                static_cast<LONG>(layout.footer.top * scale_)};
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
    return Rect(left + labelWidth, card.top + 118.0f, left + labelWidth + nameWidth + 4.0f, card.top + 140.0f);
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
    if (hotControl_ == 8)
        painter_.Line(D2D1::Point2F(link.left, link.bottom - 2.0f),
                      D2D1::Point2F(link.right, link.bottom - 2.0f), theme_.link, 1.0f);

    painter_.FillRound(layout.dialogOk, 4.0f, theme_.accent);
    painter_.Text(i18n::T(L"about.ok"), painter_.Format(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), layout.dialogOk,
                  theme_.onAccent, DWRITE_TEXT_ALIGNMENT_CENTER);
}