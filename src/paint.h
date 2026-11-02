// Theme colours and Direct2D drawing helpers.
#pragma once

#include "common.h"

struct Theme
{
    bool dark = false;
    D2D1_COLOR_F windowBg{};
    D2D1_COLOR_F text{};
    D2D1_COLOR_F textDim{};
    D2D1_COLOR_F textFaint{};
    D2D1_COLOR_F stroke{};
    D2D1_COLOR_F strokeStrong{};
    D2D1_COLOR_F card{};
    D2D1_COLOR_F accent{};
    D2D1_COLOR_F onAccent{};
    D2D1_COLOR_F hover{};
    D2D1_COLOR_F pressed{};
    D2D1_COLOR_F checkBorder{};
    D2D1_COLOR_F controlFill{};
    D2D1_COLOR_F controlBorder{};
    D2D1_COLOR_F toggleOff{};
    D2D1_COLOR_F toggleOffBorder{};
    D2D1_COLOR_F toggleOn{};
    D2D1_COLOR_F toggleKnob{};
    D2D1_COLOR_F badge{};
    D2D1_COLOR_F toastBg{};
    D2D1_COLOR_F toastText{};
    D2D1_COLOR_F scrim{};
    D2D1_COLOR_F dialogBg{};
    D2D1_COLOR_F dialogStroke{};
    D2D1_COLOR_F link{};
};

Theme MakeTheme(bool dark);

inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha)
{
    color.a = alpha;
    return color;
}

class Painter
{
public:
    bool Init();
    void SetTarget(ID2D1RenderTarget* target);
    ID2D1RenderTarget* Target() const { return target_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }

    void SetScale(float scale) { scale_ = scale; }
    float Scale() const { return scale_; }
    float Hairline() const { return 1.0f / (scale_ > 0.0f ? scale_ : 1.0f); }

    IDWriteTextFormat* Format(float sizeInDip, DWRITE_FONT_WEIGHT weight,
                              const wchar_t* family = L"Segoe UI Variable Text");
    IDWriteTextFormat* IconFormat(float sizeInDip);

    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F color);

    void FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color);
    void FillRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color);
    void StrokeRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width = 0.0f);
    void Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color, float width = 0.0f);
    void Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color);

    void Text(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
              D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              bool trim = true);
    void TextCentered(const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                      D2D1_COLOR_F color, bool trim = true);
    float TextWidth(const std::wstring& text, IDWriteTextFormat* format, float maxWidth = 8192.0f);

    void PushClip(const D2D1_RECT_F& rect);
    void PopClip();

private:
    void ResetTargetResources();

    ComPtr<IDWriteFactory5> dwrite_;
    ComPtr<ID2D1RenderTarget> target_;
    std::map<std::pair<std::wstring, int>, ComPtr<IDWriteTextFormat>> formats_;
    std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
    int clipDepth_ = 0;
    float scale_ = 1.0f;
};
