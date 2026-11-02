#include "paint.h"

#include "i18n.h"
#include "util.h"

namespace {

constexpr wchar_t kTextFamily[] = L"Segoe UI Variable Text";
constexpr wchar_t kDisplayFamily[] = L"Segoe UI Variable Display";
constexpr wchar_t kIconFamily[] = L"Segoe Fluent Icons";

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.0f)
{
    D2D1_COLOR_F color;
    color.r = ((rgb >> 16) & 0xFF) / 255.0f;
    color.g = ((rgb >> 8) & 0xFF) / 255.0f;
    color.b = (rgb & 0xFF) / 255.0f;
    color.a = alpha;
    return color;
}

uint32_t PackColor(D2D1_COLOR_F color)
{
    auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (channel(color.a) << 24) | (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
}

}  // namespace

Theme MakeTheme(bool dark)
{
    Theme theme;
    theme.dark = dark;
    if (!dark)
    {
        theme.windowBg = Rgb(0xF3F3F3);
        theme.text = Rgb(0x1A1A1A);
        theme.textDim = Rgb(0x5D5D5D);
        theme.textFaint = Rgb(0x8A8A8A);
        theme.stroke = Rgb(0x000000, 0.08f);
        theme.strokeStrong = Rgb(0x000000, 0.16f);
        theme.card = Rgb(0xFFFFFF, 0.55f);
        theme.accent = Rgb(0x005FB8);
        theme.onAccent = Rgb(0xFFFFFF);
        theme.hover = Rgb(0x000000, 0.037f);
        theme.pressed = Rgb(0x000000, 0.024f);
        theme.checkBorder = Rgb(0x000000, 0.45f);
        theme.controlFill = Rgb(0xFFFFFF, 0.70f);
        theme.controlBorder = Rgb(0x000000, 0.06f);
        theme.toggleOff = Rgb(0x000000, 0.0f);
        theme.toggleOffBorder = Rgb(0x000000, 0.45f);
        theme.toggleOn = Rgb(0x005FB8);
        theme.toggleKnob = Rgb(0x000000, 0.45f);
        theme.badge = Rgb(0x000000, 0.06f);
        theme.toastBg = Rgb(0x1F1F1F, 0.96f);
        theme.toastText = Rgb(0xFFFFFF);
        theme.scrim = Rgb(0x000000, 0.30f);
        theme.dialogBg = Rgb(0xFFFFFF);
        theme.dialogStroke = Rgb(0x000000, 0.10f);
        theme.link = Rgb(0x005FB8);
    }
    else
    {
        theme.windowBg = Rgb(0x202020);
        theme.text = Rgb(0xFFFFFF);
        theme.textDim = Rgb(0xC9C9C9);
        theme.textFaint = Rgb(0x9A9A9A);
        theme.stroke = Rgb(0xFFFFFF, 0.09f);
        theme.strokeStrong = Rgb(0xFFFFFF, 0.16f);
        theme.card = Rgb(0xFFFFFF, 0.05f);
        theme.accent = Rgb(0x60CDFF);
        theme.onAccent = Rgb(0x000000);
        theme.hover = Rgb(0xFFFFFF, 0.06f);
        theme.pressed = Rgb(0xFFFFFF, 0.03f);
        theme.checkBorder = Rgb(0xFFFFFF, 0.55f);
        theme.controlFill = Rgb(0xFFFFFF, 0.06f);
        theme.controlBorder = Rgb(0xFFFFFF, 0.09f);
        theme.toggleOff = Rgb(0xFFFFFF, 0.0f);
        theme.toggleOffBorder = Rgb(0xFFFFFF, 0.55f);
        theme.toggleOn = Rgb(0x60CDFF);
        theme.toggleKnob = Rgb(0xFFFFFF, 0.75f);
        theme.badge = Rgb(0xFFFFFF, 0.08f);
        theme.toastBg = Rgb(0x2C2C2C, 0.97f);
        theme.toastText = Rgb(0xFFFFFF);
        theme.scrim = Rgb(0x000000, 0.50f);
        theme.dialogBg = Rgb(0x2B2B2B);
        theme.dialogStroke = Rgb(0xFFFFFF, 0.10f);
        theme.link = Rgb(0x60CDFF);
    }
    return theme;
}

bool Painter::Init()
{
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    return true;
}

void Painter::ResetTargetResources()
{
    brushes_.clear();
}

void Painter::SetTarget(ID2D1RenderTarget* target)
{
    if (target_.Get() != target) ResetTargetResources();
    target_ = target;
}

IDWriteTextFormat* Painter::Format(float sizeInDip, DWRITE_FONT_WEIGHT weight, const wchar_t* family)
{
    std::wstring keyFamily = family ? family : kTextFamily;
    auto key = std::make_pair(keyFamily, static_cast<int>(std::lround(sizeInDip * 10.0f)) * 1000 + static_cast<int>(weight));
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();

    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(keyFamily.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, sizeInDip, i18n::LocaleName(),
                                         format.GetAddressOf())))
        return nullptr;

    ComPtr<IDWriteTextFormat1> extended;
    if (SUCCEEDED(format.As(&extended)))
    {
        ComPtr<IDWriteFontFallback> fallback;
        if (SUCCEEDED(dwrite_->GetSystemFontFallback(fallback.GetAddressOf())) && fallback)
            extended->SetFontFallback(fallback.Get());
    }

    auto stored = formats_.emplace(key, std::move(format));
    return stored.first->second.Get();
}

IDWriteTextFormat* Painter::IconFormat(float sizeInDip)
{
    return Format(sizeInDip, DWRITE_FONT_WEIGHT_NORMAL, kIconFamily);
}

ID2D1SolidColorBrush* Painter::Brush(D2D1_COLOR_F color)
{
    uint32_t packed = PackColor(color);
    auto it = brushes_.find(packed);
    if (it != brushes_.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target_->CreateSolidColorBrush(color, brush.GetAddressOf()))) return nullptr;
    auto stored = brushes_.emplace(packed, std::move(brush));
    return stored.first->second.Get();
}

void Painter::FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color)
{
    target_->FillRectangle(rect, Brush(color));
}

void Painter::FillRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color)
{
    D2D1_ROUNDED_RECT rounded{rect, radius, radius};
    target_->FillRoundedRectangle(rounded, Brush(color));
}

void Painter::StrokeRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width)
{
    D2D1_ROUNDED_RECT rounded{rect, radius, radius};
    // Inset by half the stroke width so the border stays inside the bounds.
    float stroke = width > 0.0f ? width : Hairline();
    D2D1_ROUNDED_RECT inset{rect, radius, radius};
    inset.rect.left += stroke * 0.5f;
    inset.rect.top += stroke * 0.5f;
    inset.rect.right -= stroke * 0.5f;
    inset.rect.bottom -= stroke * 0.5f;
    (void)rounded;
    target_->DrawRoundedRectangle(inset, Brush(color), stroke);
}

void Painter::Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color, float width)
{
    target_->DrawLine(from, to, Brush(color), width > 0.0f ? width : Hairline());
}

void Painter::Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color)
{
    D2D1_ELLIPSE ellipse{center, radius, radius};
    target_->FillEllipse(ellipse, Brush(color));
}

void Painter::Text(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                   D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align, bool trim)
{
    if (!format || text.empty()) return;
    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    if (width <= 0.0f || height <= 0.0f) return;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width,
                                         height, layout.GetAddressOf())))
        return;

    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(align);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (trim)
    {
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format, ellipsis.GetAddressOf())))
        {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            layout->SetTrimming(&trimming, ellipsis.Get());
        }
    }

    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), Brush(color),
                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Painter::TextCentered(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                           D2D1_COLOR_F color, bool trim)
{
    Text(text, format, rect, color, DWRITE_TEXT_ALIGNMENT_CENTER, trim);
}

float Painter::TextWidth(const std::wstring& text, IDWriteTextFormat* format, float maxWidth)
{
    if (!format || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, maxWidth,
                                         4096.0f, layout.GetAddressOf())))
        return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.width;
}

void Painter::PushClip(const D2D1_RECT_F& rect)
{
    target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clipDepth_;
}

void Painter::PopClip()
{
    if (clipDepth_ <= 0) return;
    target_->PopAxisAlignedClip();
    --clipDepth_;
}
