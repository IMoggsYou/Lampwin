// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#include "host/widgets/canvas.h"
#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace widgets {

namespace {

const float kPi = 3.14159265f;

bool HasFamily(IDWriteFactory* f, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(f->GetSystemFontCollection(fonts.put(), FALSE)) &&
           SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

// One pass of a box blur over `count` values `step` apart, radius r (running sum).
void BoxPass(const float* src, float* dst, int count, int step, int r) {
    float sum = 0;
    const float inv = 1.0f / (2 * r + 1);
    for (int i = -r; i <= r; ++i) sum += src[std::clamp(i, 0, count - 1) * step];
    for (int i = 0; i < count; ++i) {
        dst[i * step] = sum * inv;
        sum += src[std::min(i + r + 1, count - 1) * step] - src[std::max(i - r, 0) * step];
    }
}

// Three box blurs in each direction come within a few percent of a Gaussian of that sigma.
void Blur(std::vector<float>& a, int w, int h, float sigma) {
    int r = std::max(1, (int)lroundf(sigma));
    std::vector<float> tmp(a.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) BoxPass(&a[(size_t)y * w], &tmp[(size_t)y * w], w, 1, r);
        for (int x = 0; x < w; ++x) BoxPass(&tmp[x], &a[x], h, w, r);
    }
}

WICBitmapTransformOptions ExifTransform(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> meta;
    if (FAILED(frame->GetMetadataQueryReader(meta.put()))) return WICBitmapTransformRotate0;
    PROPVARIANT v;
    PropVariantInit(&v);
    USHORT orientation = 1;
    if (SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2) orientation = v.uiVal;
    PropVariantClear(&v);
    switch (orientation) {
        case 2: return WICBitmapTransformFlipHorizontal;
        case 3: return WICBitmapTransformRotate180;
        case 4: return WICBitmapTransformFlipVertical;
        case 5: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return WICBitmapTransformRotate90;
        case 7: return (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8: return WICBitmapTransformRotate270;
    }
    return WICBitmapTransformRotate0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Graphics
// ---------------------------------------------------------------------------

bool Graphics::Init(const std::wstring& locale) {
    locale_ = locale;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.put())))) {
        return false;
    }
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      __uuidof(IWICImagingFactory), wic_.put_void()));
}

void Graphics::SetLocale(const std::wstring& locale) {
    if (locale == locale_) return;
    locale_ = locale;
    fonts_.clear();   // the locale picks the right CJK glyphs
}

IDWriteTextFormat* Graphics::Font(float size, DWRITE_FONT_WEIGHT weight, bool display) {
    auto key = std::make_tuple(size, (int)weight, display);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second.get();
    // Windows 10 has no Segoe UI Variable.
    static const bool variable = HasFamily(dwrite_.get(), L"Segoe UI Variable Text");
    const wchar_t* family = !variable ? L"Segoe UI" : display ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text";
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, locale_.c_str(), format.put()))) {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextFormat* raw = format.get();
    fonts_[key] = format;
    return raw;
}

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------

Surface::~Surface() {
    Free();
}

void Surface::Free() {
    if (dc_) {
        if (old_) SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    old_ = nullptr;
    bits_ = nullptr;
    width_ = height_ = 0;
}

bool Surface::Resize(Graphics& g, int width, int height) {
    if (width == width_ && height == height_ && dc_) return true;
    Free();
    if (width <= 0 || height <= 0) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    dc_ = CreateCompatibleDC(nullptr);
    if (!bitmap_ || !dc_) {
        Free();
        return false;
    }
    old_ = SelectObject(dc_, bitmap_);
    bits_ = (BYTE*)bits;
    width_ = width;
    height_ = height;
    if (!target_) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        if (FAILED(g.D2D()->CreateDCRenderTarget(&props, target_.put()))) return false;
    }
    return true;
}

ID2D1RenderTarget* Surface::Begin(bool clear) {
    if (!target_ || !dc_) return nullptr;
    RECT r = {0, 0, width_, height_};
    if (FAILED(target_->BindDC(dc_, &r))) return nullptr;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (clear) target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    // ClearType needs an opaque background; on a transparent one it leaves colored fringes.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    return target_.get();
}

bool Surface::End() {
    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) target_.reset();
    GdiFlush();
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// Shapes and text
// ---------------------------------------------------------------------------

D2D1_COLOR_F Rgb(UINT32 rgb, float alpha) {
    return D2D1::ColorF(rgb, alpha);
}

D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// The figma-squircle construction: straight edge, cubic Bezier, circular arc, cubic Bezier.
ComPtr<ID2D1PathGeometry> Squircle(ID2D1Factory* f, const D2D1_RECT_F& rect, float radius, float smoothing) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(f->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) return nullptr;
    const float w = rect.right - rect.left, h = rect.bottom - rect.top;
    const float budget = std::min(w, h) / 2;
    const float r = std::clamp(radius, 0.0f, budget);
    if (r <= 0) {
        sink->BeginFigure({rect.left, rect.top}, D2D1_FIGURE_BEGIN_FILLED);
        D2D1_POINT_2F pts[] = {{rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
        sink->AddLines(pts, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        return path;
    }
    auto rad = [](float deg) { return deg * kPi / 180; };
    float s = std::clamp(smoothing, 0.0f, 1.0f);
    float p = (1 + s) * r;
    s = std::min(s, budget / r - 1);
    p = std::min(p, budget);
    const float arcMeasure = 90 * (1 - s);
    const float arc = sinf(rad(arcMeasure / 2)) * r * sqrtf(2);
    const float alpha = (90 - arcMeasure) / 2;
    const float p3ToP4 = r * tanf(rad(alpha / 2));
    const float beta = 45 * s;
    const float c = p3ToP4 * cosf(rad(beta));
    const float d = c * tanf(rad(beta));
    const float b = (p - arc - c - d) / 3;
    const float a = 2 * b;

    D2D1_POINT_2F at = {rect.left + w - p, rect.top};
    sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED);
    // Each corner in its own frame: u along the edge it leaves, v into the next one. Every
    // step is relative to where the previous one ended (`at`).
    auto corner = [&](float ux, float uy, float vx, float vy) {
        auto P = [&](float du, float dv) { return D2D1::Point2F(at.x + ux * du + vx * dv, at.y + uy * du + vy * dv); };
        sink->AddBezier({P(a, 0), P(a + b, 0), P(a + b + c, d)});
        at = P(a + b + c, d);
        if (arc > 1e-4f) {
            D2D1_POINT_2F end = P(arc, arc);
            sink->AddArc({end, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL});
            at = end;
        }
        sink->AddBezier({P(d, c), P(d, b + c), P(d, a + b + c)});
        at = P(d, a + b + c);
    };
    corner(1, 0, 0, 1);   // top right
    at = {rect.right, rect.bottom - p};
    sink->AddLine(at);
    corner(0, 1, -1, 0);   // bottom right
    at = {rect.left + p, rect.bottom};
    sink->AddLine(at);
    corner(-1, 0, 0, -1);   // bottom left
    at = {rect.left, rect.top + p};
    sink->AddLine(at);
    corner(0, -1, 1, 0);   // top left
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

void DrawShadow(Graphics& g, Surface& s, const D2D1_RECT_F& card, float radius, float smoothing, float unit) {
    const int w = s.Width(), h = s.Height();
    ID2D1RenderTarget* rt = s.Begin();
    if (!rt) return;
    ComPtr<ID2D1PathGeometry> shape = Squircle(g.D2D(), card, radius, smoothing);
    ComPtr<ID2D1SolidColorBrush> black;
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), black.put());
    if (shape && black) rt->FillGeometry(shape.get(), black.get());
    s.End();

    BYTE* px = s.Bits();
    std::vector<float> mask((size_t)w * h);
    for (size_t i = 0; i < mask.size(); ++i) mask[i] = px[i * 4 + 3] / 255.0f;
    struct Layer {
        float sigma, offsetY, opacity;
    };
    const Layer layers[] = {{10.0f, 4.0f, 0.24f}, {1.5f, 1.0f, 0.14f}};
    std::vector<float> sum(mask.size(), 0.0f), blurred;
    for (const Layer& l : layers) {
        blurred = mask;
        Blur(blurred, w, h, l.sigma * unit);
        int dy = (int)lroundf(l.offsetY * unit);
        for (int y = 0; y < h; ++y) {
            int from = y - dy;
            if (from < 0 || from >= h) continue;
            for (int x = 0; x < w; ++x) sum[(size_t)y * w + x] += blurred[(size_t)from * w + x] * l.opacity;
        }
    }
    for (size_t i = 0; i < sum.size(); ++i) {
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 0;   // premultiplied black
        px[i * 4 + 3] = (BYTE)lroundf(std::min(sum[i], 1.0f) * 255);
    }
}

void DrawText(Graphics& g, ID2D1RenderTarget* rt, const std::wstring& text, IDWriteTextFormat* font,
              const D2D1_RECT_F& box, const D2D1_COLOR_F& color, const TextOptions& o) {
    if (!font || text.empty()) return;
    const float w = std::max(0.0f, box.right - box.left), h = std::max(0.0f, box.bottom - box.top);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g.DWrite()->CreateTextLayout(text.c_str(), (UINT32)text.size(), font, w, h, layout.put()))) return;
    layout->SetTextAlignment(o.align);
    layout->SetParagraphAlignment(o.valign);
    layout->SetWordWrapping(o.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (o.rtl) layout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    if (o.lineSpacing > 0) layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, o.lineSpacing, o.lineSpacing * 0.8f);
    // Too long: cut with an ellipsis instead of spilling out of the card.
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(g.DWrite()->CreateEllipsisTrimmingSign(layout.get(), ellipsis.put()))) {
        DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trim, ellipsis.get());
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, brush.put()))) return;
    rt->DrawTextLayout({box.left, box.top}, layout.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float TextWidth(Graphics& g, const std::wstring& text, IDWriteTextFormat* font) {
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS m = {};
    if (!font || FAILED(g.DWrite()->CreateTextLayout(text.c_str(), (UINT32)text.size(), font, 10000, 1000,
                                                     layout.put())) ||
        FAILED(layout->GetMetrics(&m))) {
        return 0;
    }
    return m.widthIncludingTrailingWhitespace;
}

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

ComPtr<IWICBitmapSource> LoadPicture(Graphics& g, const std::wstring& path, UINT w, UINT h) {
    IWICImagingFactory* wic = g.Wic();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!wic || w == 0 || h == 0 ||
        FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              decoder.put())) ||
        FAILED(decoder->GetFrame(0, frame.put()))) {
        return nullptr;
    }
    ComPtr<IWICBitmapSource> source;
    frame.As(&source);
    WICBitmapTransformOptions turn = ExifTransform(frame.get());
    if (turn != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(wic->CreateBitmapFlipRotator(rotator.put())) &&
            SUCCEEDED(rotator->Initialize(source.get(), turn))) {
            rotator.As(&source);
        }
    }
    UINT sw = 0, sh = 0;
    if (FAILED(source->GetSize(&sw, &sh)) || sw == 0 || sh == 0) return nullptr;
    // Cover: scale so the picture fills the card, then cut the overhang off evenly.
    double scale = std::max((double)w / sw, (double)h / sh);
    UINT tw = std::max(w, (UINT)ceil(sw * scale)), th = std::max(h, (UINT)ceil(sh * scale));
    // WICBitmapInterpolationModeHighQualityCubic (Windows 10), missing from MinGW's headers.
    const auto kHighQualityCubic = (WICBitmapInterpolationMode)4;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(source.get(), tw, th, kHighQualityCubic))) {
        if (!scaler || FAILED(scaler->Initialize(source.get(), tw, th, WICBitmapInterpolationModeFant))) return nullptr;
    }
    ComPtr<IWICBitmapClipper> clipper;
    WICRect crop = {(INT)(tw - w) / 2, (INT)(th - h) / 2, (INT)w, (INT)h};
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateBitmapClipper(clipper.put())) || FAILED(clipper->Initialize(scaler.get(), &crop)) ||
        FAILED(wic->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(clipper.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                     0, WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }
    // Decoded once here; drawing it later costs nothing.
    ComPtr<IWICBitmap> cached;
    if (FAILED(wic->CreateBitmapFromSource(converter.get(), WICBitmapCacheOnLoad, cached.put()))) return nullptr;
    ComPtr<IWICBitmapSource> out;
    cached.As(&out);
    return out;
}

}  // namespace widgets
