// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <map>
#include <string>
#include <tuple>
#include "host/com.h"

// Drawing for the widgets: Direct2D on the CPU into a premultiplied bitmap that a layered
// window shows (UpdateLayeredWindow). A widget redraws at most once a second, so the
// software rasterizer is plenty, and no GPU memory is held for it.
namespace widgets {

// The factories of one thread (Direct2D's is single-threaded) and its cached fonts.
class Graphics {
public:
    bool Init(const std::wstring& locale);
    ID2D1Factory* D2D() const { return d2d_.get(); }
    IDWriteFactory* DWrite() const { return dwrite_.get(); }
    IWICImagingFactory* Wic() const { return wic_.get(); }
    const std::wstring& Locale() const { return locale_; }
    void SetLocale(const std::wstring& locale);
    // Size in DIPs; display = Segoe UI Variable Display (large numbers), else Text.
    IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight, bool display);

private:
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    std::wstring locale_;
    std::map<std::tuple<float, int, bool>, ComPtr<IDWriteTextFormat>> fonts_;
};

// A 32-bit premultiplied DIB with a Direct2D target bound to it.
class Surface {
public:
    ~Surface();
    bool Resize(Graphics& g, int width, int height);
    // Ready to draw (cleared to transparent unless `clear` is false); nullptr if Direct2D failed.
    ID2D1RenderTarget* Begin(bool clear = true);
    bool End();
    HDC Dc() const { return dc_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    BYTE* Bits() const { return bits_; }

private:
    void Free();

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    BYTE* bits_ = nullptr;
    int width_ = 0, height_ = 0;
    ComPtr<ID2D1DCRenderTarget> target_;
};

D2D1_COLOR_F Rgb(UINT32 rgb, float alpha = 1.0f);
D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t);

// The rounded rectangle with Figma-style corner smoothing that MacWin draws everywhere
// (settings-app/Controls/Squircle.cs builds the same one).
ComPtr<ID2D1PathGeometry> Squircle(ID2D1Factory* f, const D2D1_RECT_F& r, float radius, float smoothing);

// How far a card's shadow reaches past its edges, in DIPs.
const float kShadowReach = 36;
// The soft shadow under a card (a wide ambient one and a tight contact one), in px, into
// a surface kShadowReach DIPs larger than the card on every side.
void DrawShadow(Graphics& g, Surface& s, const D2D1_RECT_F& card, float radius, float smoothing, float unit);

// Text in a box. Right-to-left languages read from the right (the box is not mirrored).
struct TextOptions {
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    bool wrap = false;
    bool rtl = false;
    float lineSpacing = 0;   // 0: the font's own
};
void DrawText(Graphics& g, ID2D1RenderTarget* rt, const std::wstring& text, IDWriteTextFormat* font,
              const D2D1_RECT_F& box, const D2D1_COLOR_F& color, const TextOptions& o = {});
float TextWidth(Graphics& g, const std::wstring& text, IDWriteTextFormat* font);

// A picture file decoded, turned upright (EXIF orientation) and scaled to cover w x h.
ComPtr<IWICBitmapSource> LoadPicture(Graphics& g, const std::wstring& path, UINT w, UINT h);

}  // namespace widgets
