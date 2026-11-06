// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/island/island_draw.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include "host/island/island_glyphs.h"

namespace island {

namespace {

using widgets::Rgb;

const float kCompact = 36;     // the pill's height
const float kRadius = 38;      // a card's corner
const float kPad = 20;         // a card's margin

const DWRITE_FONT_WEIGHT kRegular = DWRITE_FONT_WEIGHT_NORMAL;
const DWRITE_FONT_WEIGHT kSemibold = DWRITE_FONT_WEIGHT_SEMI_BOLD;

struct Palette {
    D2D1_COLOR_F text = Rgb(0xFFFFFF), secondary = Rgb(0x8E8E93), green = Rgb(0x30D158), red = Rgb(0xFF453A),
                 orange = Rgb(0xFF9F0A), blue = Rgb(0x0A84FF), link = Rgb(0x409CFF), button = Rgb(0x2C2C2E),
                 track = Rgb(0xFFFFFF, 0.22f), dim = Rgb(0x3A3A3C), black = Rgb(0x000000);
};
const Palette P;

// One island being drawn: the target, its fonts and text helpers. Coordinates are DIPs from
// the island's top left corner.
class Ctx {
public:
    Ctx(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s) : g(g), rt(rt), s(s) {}
    widgets::Graphics& g;
    ID2D1RenderTarget* rt;
    const Scene& s;

    const std::wstring& T(const wchar_t* key) const {
        static const std::wstring empty;
        if (!s.texts) return empty;
        auto it = s.texts->find(key);
        return it != s.texts->end() ? it->second : empty;
    }
    IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight) { return g.Font(size, weight, false); }
    float Width(const std::wstring& t, IDWriteTextFormat* f) { return widgets::TextWidth(g, t, f); }
    // One line whose baseline is at `baseline` (the widgets' rule).
    void TextAt(const std::wstring& t, IDWriteTextFormat* f, float left, float right, float baseline,
                const D2D1_COLOR_F& c, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        float b, h;
        LineOf(f, &b, &h);
        widgets::TextOptions o;
        o.align = align;
        o.rtl = s.rtl;
        widgets::DrawText(g, rt, t, f, {left, baseline - b, right, baseline - b + h + 1}, c, o);
    }
    // Text in a box, wrapped (two lines at most for the box's height).
    void Wrapped(const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F box, const D2D1_COLOR_F& c) {
        widgets::TextOptions o;
        o.wrap = true;
        o.rtl = s.rtl;
        widgets::DrawText(g, rt, t, f, box, c, o);
    }
    void Centered(const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F box, const D2D1_COLOR_F& c) {
        widgets::TextOptions o;
        o.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        o.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        o.rtl = s.rtl;
        widgets::DrawText(g, rt, t, f, box, c, o);
    }
    ComPtr<ID2D1SolidColorBrush> Brush(const D2D1_COLOR_F& c) {
        ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(c, b.put());
        return b;
    }
    void Fill(D2D1_RECT_F r, float radius, const D2D1_COLOR_F& c) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(c).get());
    }
    void Dot(float x, float y, float r, const D2D1_COLOR_F& c) {
        rt->FillEllipse(D2D1::Ellipse({x, y}, r, r), Brush(c).get());
    }
    void Line(float x0, float y0, float x1, float y1, float w, const D2D1_COLOR_F& c) {
        if (!round_) {
            ComPtr<ID2D1Factory> f;
            rt->GetFactory(f.put());
            f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                             D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                 nullptr, 0, round_.put());
        }
        rt->DrawLine({x0, y0}, {x1, y1}, Brush(c).get(), w, round_.get());
    }
    void Glyph(island::Glyph glyph, float x, float y, float size, const D2D1_COLOR_F& c) {
        DrawGlyph(rt, glyph, x, y, size, c);
    }
    // A picture (cover, app icon) in a rounded square.
    void Picture(IWICBitmapSource* source, D2D1_RECT_F r, float radius) {
        ComPtr<ID2D1Bitmap> bitmap;
        ComPtr<ID2D1Factory> f;
        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        ComPtr<ID2D1Layer> layer;
        rt->GetFactory(f.put());
        if (!source || FAILED(rt->CreateBitmapFromWicBitmap(source, nullptr, bitmap.put())) ||
            FAILED(f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), mask.put())) ||
            FAILED(rt->CreateLayer(nullptr, layer.put()))) {
            return;
        }
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.get()), layer.get());
        rt->DrawBitmap(bitmap.get(), r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        rt->PopLayer();
    }

private:
    void LineOf(IDWriteTextFormat* f, float* baseline, float* height) {
        // By size and weight: the island's fonts are one family.
        static std::map<std::pair<float, int>, std::pair<float, float>> lines;
        const std::pair<float, int> key = {f->GetFontSize(), (int)f->GetFontWeight()};
        auto it = lines.find(key);
        if (it == lines.end()) {
            float b = f->GetFontSize(), h = b * 1.33f;
            ComPtr<IDWriteTextLayout> layout;
            DWRITE_LINE_METRICS m = {};
            UINT32 count = 0;
            if (SUCCEEDED(g.DWrite()->CreateTextLayout(L"Ag", 2, f, 1000, 1000, layout.put())) &&
                SUCCEEDED(layout->GetLineMetrics(&m, 1, &count)) && count == 1) {
                b = m.baseline;
                h = m.height;
            }
            it = lines.emplace(key, std::make_pair(b, h)).first;
        }
        *baseline = it->second.first;
        *height = it->second.second;
    }

    ComPtr<ID2D1StrokeStyle> round_;
};

std::wstring Fill(std::wstring pattern, const std::wstring& a0) {
    const size_t at = pattern.find(L"{0}");
    if (at != std::wstring::npos) pattern.replace(at, 3, a0);
    return pattern;
}

std::wstring Percent(int v) {
    return std::to_wstring(v) + L"%";
}

// Five level bars `h` high at most, the first at x, around cy. Still (low) when `live` is off.
void Bars(Ctx& c, float x, float cy, float h, const D2D1_COLOR_F& color, bool live, float phase) {
    for (int i = 0; i < 5; ++i) {
        const float k = live ? 0.35f + 0.65f * fabsf(sinf(c.s.time * 3.4f + phase + i * 1.3f)) : 0;
        const float bh = std::max(3.0f, h * k);
        c.Line(x + i * 4.2f, cy - bh / 2, x + i * 4.2f, cy + bh / 2, 2.6f, color);
    }
}

// A battery `level` full, 25 x 12 DIPs at x around cy.
void Battery(Ctx& c, float x, float cy, int level, const D2D1_COLOR_F& color, bool charging) {
    const float w = 25, h = 12, y = cy - h / 2;
    c.rt->DrawRoundedRectangle(D2D1::RoundedRect({x + 0.5f, y + 0.5f, x + w - 0.5f, y + h - 0.5f}, 3.5f, 3.5f),
                               c.Brush(Rgb(0xFFFFFF, 0.35f)).get(), 1);
    c.Fill({x + w + 1, cy - 2.5f, x + w + 2.6f, cy + 2.5f}, 1, Rgb(0xFFFFFF, 0.35f));
    const float fill = (w - 4) * std::clamp(level, 0, 100) / 100.0f;
    if (fill > 0) c.Fill({x + 2, y + 2, x + 2 + std::max(fill, 2.0f), y + h - 2}, 2, color);
    if (charging) c.Glyph(Glyph::Bolt, x + w / 2 - 6, cy - 6, 12, P.text);
}

// A wide rounded button with a word on it.
void Button(Ctx& c, D2D1_RECT_F r, const D2D1_COLOR_F& fill, const std::wstring& text, const D2D1_COLOR_F& color,
            Action action, std::vector<Hit>* hits) {
    c.Fill(r, (r.bottom - r.top) / 2, fill);
    c.Centered(text, c.Font(15, kSemibold), r, color);
    if (hits) hits->push_back({r, action});
}

// A round button with a glyph, `d` across, centred on (cx, cy).
void Round(Ctx& c, float cx, float cy, float d, const D2D1_COLOR_F& fill, Glyph glyph, const D2D1_COLOR_F& color,
           bool slashed, Action action, std::vector<Hit>* hits) {
    c.Dot(cx, cy, d / 2, fill);
    const float g = d * 0.46f;
    if (slashed) DrawSlashed(c.rt, glyph, cx - g / 2, cy - g / 2, g, color, fill);
    else c.Glyph(glyph, cx - g / 2, cy - g / 2, g, color);
    if (hits) hits->push_back({{cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2}, action});
}

void Cover(Ctx& c, float x, float y, float size, float radius) {
    if (c.s.art) c.Picture(c.s.art, {x, y, x + size, y + size}, radius);
    else widgets::DrawMusicTile(c.rt, {x, y, x + size, y + size}, radius);
}

// The call's program: its own icon, else a green circle with a phone.
void CallIcon(Ctx& c, float x, float y, float size) {
    if (c.s.appIcon) {
        c.Picture(c.s.appIcon, {x, y, x + size, y + size}, size * 0.22f);
        return;
    }
    c.Dot(x + size / 2, y + size / 2, size / 2, P.green);
    c.Glyph(Glyph::Phone, x + size * 0.25f, y + size * 0.25f, size * 0.5f, P.text);
}

const float kLine = 22.5f;   // the baseline of a line of text in the pill

// ---------------------------------------------------------------------------
// The views. Each draws into a (w, h) island at the origin.
// ---------------------------------------------------------------------------

void MusicView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const widgets::MediaInfo& m = *c.s.media;
    const bool live = m.playing;
    if (!c.s.expanded) {
        Cover(c, 6, 6, 24, 6);
        c.TextAt(m.title, c.Font(12.5f, kSemibold), 38, w - 42, kLine, P.text);
        Bars(c, w - 30, 18, 14, c.s.accent, live, 0);
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    Cover(c, kPad, kPad, 56, 12);
    c.TextAt(m.title, c.Font(15, kSemibold), 88, w - 56, 42, P.text);
    const std::wstring by = m.artist.empty() ? m.app : m.artist;
    c.TextAt(by, c.Font(13, kRegular), 88, w - 56, 61, P.secondary);
    Bars(c, w - 41, 37, 16, c.s.accent, live, 0);
    float cy = 105;
    if (m.duration > 0) {
        const INT64 total = m.duration / 10000000, now = std::clamp<INT64>(c.s.position / 10000000, 0, total);
        const float y = 104;
        c.TextAt(Clock(now), c.Font(11, kSemibold), kPad, 52, y + 4, P.secondary, DWRITE_TEXT_ALIGNMENT_TRAILING);
        c.TextAt(L"-" + Clock(total - now), c.Font(11, kSemibold), w - 52, w - 18, y + 4, P.secondary);
        c.Line(62, y, w - 62, y, 5, P.track);
        const float done = total > 0 ? (float)now / (float)total : 0;
        c.Line(62, y, 62 + (w - 124) * done, y, 5, P.text);
        cy = 145;
    }
    c.Glyph(Glyph::Previous, w / 2 - 78 - 15, cy - 15, 30, P.text);
    c.Glyph(m.playing ? Glyph::Pause : Glyph::Play, w / 2 - 17, cy - 17, 34, P.text);
    c.Glyph(Glyph::Next, w / 2 + 78 - 15, cy - 15, 30, P.text);
    if (hits) {
        hits->push_back({{w / 2 - 78 - 26, cy - 24, w / 2 - 78 + 26, cy + 24}, Action::Previous});
        hits->push_back({{w / 2 - 26, cy - 24, w / 2 + 26, cy + 24}, Action::PlayPause});
        hits->push_back({{w / 2 + 78 - 26, cy - 24, w / 2 + 78 + 26, cy + 24}, Action::Next});
        hits->push_back({{0, 0, w, cy - 26}, Action::Toggle});
    }
}

void CallView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const CallInfo& call = *c.s.call;
    const std::wstring time = Clock(c.s.callSeconds);
    if (!c.s.expanded) {
        c.Glyph(Glyph::Phone, 12, 8, 20, P.green);
        c.TextAt(time, c.Font(13, kSemibold), 38, w - 50, kLine, P.green);
        Bars(c, w - 30, 18, 14, P.green, !c.s.micMuted, 0.8f);
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    CallIcon(c, kPad, kPad, 44);
    c.TextAt(call.app, c.Font(16, kSemibold), 76, w - 56, 40, P.text);
    const std::wstring line = Fill(c.T(c.s.micMuted ? L"host.island.callMuted" : L"host.island.call"), time);
    c.TextAt(line, c.Font(13, kRegular), 76, w - 56, 60, P.secondary);
    if (c.s.micMuted) DrawSlashed(c.rt, Glyph::Mic, w - 44, 28, 20, P.secondary, P.black);
    else c.Glyph(Glyph::Mic, w - 44, 28, 20, P.green);
    // The microphone (on: white, like iOS), the camera when it is on, the program's window.
    const float cy = 112;
    std::vector<int> buttons = {0};
    if (call.camera) buttons.push_back(1);
    if (!call.path.empty()) buttons.push_back(2);
    const float step = 62, x0 = w / 2 - step * (buttons.size() - 1) / 2.0f;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const float cx = x0 + step * i;
        if (buttons[i] == 0) {
            if (c.s.micMuted) Round(c, cx, cy, 48, P.button, Glyph::Mic, P.text, true, Action::MicMute, hits);
            else Round(c, cx, cy, 48, P.text, Glyph::Mic, P.black, false, Action::MicMute, hits);
        } else if (buttons[i] == 1) {
            Round(c, cx, cy, 48, P.button, Glyph::Camera, P.green, false, Action::None, nullptr);
        } else {
            Round(c, cx, cy, 48, P.button, Glyph::Open, P.text, false, Action::ShowApp, hits);
        }
    }
    if (hits) hits->push_back({{0, 0, w, cy - 26}, Action::Toggle});
}

void MirroringView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const MirrorInfo& m = *c.s.mirror;
    const std::wstring& title = c.T(L"host.island.mirroring");
    const std::wstring target = !m.target.empty() ? m.target : c.T(L"host.island.mirroring.duplicate");
    if (!c.s.expanded) {
        c.Glyph(Glyph::Mirroring, 11, 7, 22, P.blue);
        c.TextAt(title, c.Font(12.5f, kSemibold), 40, w - 40, kLine, P.text);
        c.Glyph(Glyph::Display, w - 31, 8, 20, P.secondary);
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    c.Glyph(Glyph::Mirroring, kPad, kPad, 32, P.blue);
    c.TextAt(title, c.Font(15, kSemibold), 64, w - 56, 36, P.text);
    c.TextAt(target, c.Font(13, kRegular), 64, w - 56, 55, P.secondary);
    c.Glyph(Glyph::Display, w - 44, 24, 24, P.secondary);
    Button(c, {14, h - 54, w - 14, h - 14}, Rgb(0x0A84FF, 0.22f), c.T(L"host.island.mirroring.stop"), P.link,
           Action::StopMirroring, hits);
    if (hits) hits->push_back({{0, 0, w, h - 58}, Action::Toggle});
}

void BatteryView(Ctx& c, float w, bool low) {
    const D2D1_COLOR_F color = !low ? P.green : c.s.battery <= 10 ? P.red : Rgb(0xFFD60A);
    const std::wstring& title = c.T(low ? L"host.island.lowBattery" : L"host.island.charging");
    c.TextAt(title, c.Font(13, kSemibold), 16, w / 2 + 30, kLine, P.text);
    c.TextAt(Percent(c.s.battery), c.Font(13, kSemibold), w - 120, w - 50, kLine, color,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    Battery(c, w - 44, 18, c.s.battery, color, !low);
}

void SoundView(Ctx& c, float w, bool silent, std::vector<Hit>* hits) {
    const D2D1_COLOR_F back = silent ? P.red : P.dim;
    c.Fill({6, 6, 48, 30}, 12, back);
    if (silent) DrawSlashed(c.rt, Glyph::Bell, 18, 9, 18, P.text, back);
    else c.Glyph(Glyph::Bell, 18, 9, 18, P.text);
    c.TextAt(c.T(silent ? L"host.island.silent" : L"host.island.sound"), c.Font(13, kSemibold), 60, w - 16, kLine,
             silent ? P.red : P.text, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (hits) hits->push_back({{0, 0, w, kCompact}, Action::ToggleMute});
}

void AirplaneOffView(Ctx& c, float w) {
    c.Glyph(Glyph::Airplane, 12, 7, 22, P.secondary);
    c.TextAt(c.T(L"host.island.airplane"), c.Font(13, kSemibold), 42, w - 60, kLine, P.text);
    c.TextAt(c.T(L"host.island.off"), c.Font(13, kSemibold), w - 80, w - 16, kLine, P.secondary,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// A card with a glyph, a title, a line or two, and buttons at the bottom.
void NoticeCard(Ctx& c, float w, float h, Glyph glyph, const D2D1_COLOR_F& color, const wchar_t* title,
                const wchar_t* text) {
    c.Glyph(glyph, 18, 18, 32, color);
    c.TextAt(c.T(title), c.Font(15, kSemibold), 62, w - kPad, 34, P.text);
    c.Wrapped(c.T(text), c.Font(13, kRegular), {62, 40, w - kPad, h - 58}, P.secondary);
}

void OutputView(Ctx& c, float w) {
    c.Glyph(c.s.headphones ? Glyph::Headphones : Glyph::Speaker, 18, 17, 30, P.text);
    c.TextAt(c.T(L"host.island.connected"), c.Font(12, kSemibold), 62, w - 24, 28, P.secondary);
    c.TextAt(c.s.output, c.Font(15, kSemibold), 62, w - 24, 47, P.text);
}

float Clamp(float v, float lo, float hi) {
    return std::min(std::max(v, lo), hi);
}

}  // namespace

std::wstring Clock(INT64 seconds) {
    if (seconds < 0) seconds = 0;
    wchar_t buf[32];
    if (seconds >= 3600) {
        swprintf(buf, ARRAYSIZE(buf), L"%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    } else {
        swprintf(buf, ARRAYSIZE(buf), L"%lld:%02lld", seconds / 60, seconds % 60);
    }
    return buf;
}

D2D1_SIZE_F Size(widgets::Graphics& g, const Scene& s) {
    Ctx c(g, nullptr, s);
    switch (s.view) {
        case View::None: return {120, 24};
        case View::Music:
            if (!s.expanded) return {248, kCompact};
            return {360, s.media && s.media->duration > 0 ? 176.0f : 136.0f};
        case View::Call: return s.expanded ? D2D1::SizeF(360, 150) : D2D1::SizeF(220, kCompact);
        case View::Mirroring: {
            if (s.expanded) return {360, 136};
            const float tw = c.Width(c.T(L"host.island.mirroring"), c.Font(12.5f, kSemibold));
            return {Clamp(80 + tw, 200, 320), kCompact};
        }
        case View::Charging:
        case View::LowBattery: return {280, kCompact};
        case View::Silent:
        case View::Sound: {
            const float tw = c.Width(c.T(s.view == View::Silent ? L"host.island.silent" : L"host.island.sound"),
                                     c.Font(13, kSemibold));
            return {Clamp(80 + tw, 200, 320), kCompact};
        }
        case View::AirplaneOff: {
            const float tw = c.Width(c.T(L"host.island.airplane"), c.Font(13, kSemibold)) +
                             c.Width(c.T(L"host.island.off"), c.Font(13, kSemibold));
            return {Clamp(84 + tw, 240, 340), kCompact};
        }
        case View::AirplaneOn:
        case View::MobileData: return {360, 146};
        case View::Output: {
            const float tw = std::max(c.Width(s.output, c.Font(15, kSemibold)),
                                      c.Width(c.T(L"host.island.connected"), c.Font(12, kSemibold)));
            return {Clamp(90 + tw, 240, 380), 64};
        }
    }
    return {120, 24};
}

void Draw(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float top, float w, float h,
          float content, std::vector<Hit>* hits) {
    const float radius = std::min(h / 2, kRadius);
    const float left = cx - w / 2;
    Ctx c(g, rt, s);
    const D2D1_ROUNDED_RECT shape = D2D1::RoundedRect({left, top, left + w, top + h}, radius, radius);
    rt->FillRoundedRectangle(shape, c.Brush(P.black).get());
    // A faint edge: the island stays defined over a black window or wallpaper.
    rt->DrawRoundedRectangle(D2D1::RoundedRect({left + 0.375f, top + 0.375f, left + w - 0.375f, top + h - 0.375f},
                                               radius - 0.375f, radius - 0.375f),
                             c.Brush(Rgb(0xFFFFFF, 0.10f)).get(), 0.75f);
    if (content <= 0.01f || s.view == View::None) return;

    // The content fades in as the shape settles, clipped to it.
    ComPtr<ID2D1Factory> f;
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    ComPtr<ID2D1Layer> layer;
    rt->GetFactory(f.put());
    if (FAILED(f->CreateRoundedRectangleGeometry(shape, mask.put())) || FAILED(rt->CreateLayer(nullptr, layer.put()))) {
        return;
    }
    D2D1::Matrix3x2F old;
    rt->GetTransform(&old);
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                        D2D1::IdentityMatrix(), Clamp(content, 0, 1)),
                  layer.get());
    rt->SetTransform(D2D1::Matrix3x2F::Translation(left, top) * old);
    std::vector<Hit> local;
    std::vector<Hit>* out = hits ? &local : nullptr;
    switch (s.view) {
        case View::Music:
            if (s.media) MusicView(c, w, h, out);
            break;
        case View::Call:
            if (s.call) CallView(c, w, h, out);
            break;
        case View::Mirroring:
            if (s.mirror) MirroringView(c, w, h, out);
            break;
        case View::Charging: BatteryView(c, w, false); break;
        case View::LowBattery: BatteryView(c, w, true); break;
        case View::Silent: SoundView(c, w, true, out); break;
        case View::Sound: SoundView(c, w, false, out); break;
        case View::AirplaneOff: AirplaneOffView(c, w); break;
        case View::AirplaneOn:
            NoticeCard(c, w, h, Glyph::Airplane, P.orange, L"host.island.airplaneOn.title",
                       L"host.island.airplaneOn.text");
            Button(c, {14, h - 54, w - 14, h - 14}, P.button, c.T(L"host.island.openSettings"), P.text,
                   Action::OpenAirplane, out);
            break;
        case View::MobileData: {
            NoticeCard(c, w, h, Glyph::Antenna, P.green, L"host.island.mobileData.title",
                       L"host.island.mobileData.text");
            const float mid = w / 2;
            Button(c, {14, h - 54, mid - 5, h - 14}, P.button, c.T(L"host.island.ok"), P.text, Action::Dismiss, out);
            Button(c, {mid + 5, h - 54, w - 14, h - 14}, Rgb(0x0A84FF, 0.22f), c.T(L"host.island.settings"), P.link,
                   Action::OpenCellular, out);
            break;
        }
        case View::Output: OutputView(c, w); break;
        case View::None: break;
    }
    rt->SetTransform(old);
    rt->PopLayer();
    if (hits) {
        for (Hit h : local) {
            h.rect = {h.rect.left + left, h.rect.top + top, h.rect.right + left, h.rect.bottom + top};
            hits->push_back(h);
        }
    }
}

void DrawBubble(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float cy, float size,
                float content) {
    Ctx c(g, rt, s);
    c.Dot(cx, cy, size / 2, P.black);
    rt->DrawEllipse(D2D1::Ellipse({cx, cy}, size / 2 - 0.375f, size / 2 - 0.375f), c.Brush(Rgb(0xFFFFFF, 0.10f)).get(),
                    0.75f);
    if (content <= 0.01f) return;
    const D2D1_COLOR_F white = Rgb(0xFFFFFF, content), green = Rgb(0x30D158, content), blue = Rgb(0x0A84FF, content);
    const float gsize = size * 0.56f, x = cx - gsize / 2, y = cy - gsize / 2;
    switch (s.bubble) {
        case View::Music:
            if (s.art) c.Picture(s.art, {cx - size * 0.33f, cy - size * 0.33f, cx + size * 0.33f, cy + size * 0.33f},
                                 size * 0.17f);
            else c.Glyph(Glyph::Play, x, y, gsize, white);
            break;
        case View::Call: c.Glyph(Glyph::Phone, x, y, gsize, green); break;
        case View::Mirroring: c.Glyph(Glyph::Mirroring, x, y, gsize, blue); break;
        default: break;
    }
}

D2D1_COLOR_F AccentOf(IWICBitmapSource* art) {
    const D2D1_COLOR_F fallback = Rgb(0xFA233B);
    UINT w = 0, h = 0;
    if (!art || FAILED(art->GetSize(&w, &h)) || w == 0 || h == 0 || w > 512 || h > 512) return fallback;
    std::vector<BYTE> px((size_t)w * h * 4);
    if (FAILED(art->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return fallback;
    // Colourful pixels count most: a cover that is mostly black or white still gives its colour.
    double r = 0, gr = 0, b = 0, weight = 0;
    for (size_t i = 0; i < px.size(); i += 4) {
        const double B = px[i] / 255.0, G = px[i + 1] / 255.0, R = px[i + 2] / 255.0, A = px[i + 3] / 255.0;
        if (A < 0.5) continue;
        const double hi = std::max({R, G, B}), lo = std::min({R, G, B});
        const double sat = hi > 0 ? (hi - lo) / hi : 0;
        const double wgt = sat * sat * hi + 0.02;
        r += R * wgt;
        gr += G * wgt;
        b += B * wgt;
        weight += wgt;
    }
    if (weight <= 0) return fallback;
    D2D1_COLOR_F c = {(float)(r / weight), (float)(gr / weight), (float)(b / weight), 1};
    // Light enough to read as a colour on black.
    const float hi = std::max({c.r, c.g, c.b});
    if (hi > 0.01f && hi < 0.85f) {
        const float k = 0.85f / hi;
        c = {c.r * k, c.g * k, c.b * k, 1};
    }
    const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    if (luma < 0.35f) c = widgets::Mix(c, Rgb(0xFFFFFF), 0.35f - luma);
    return c;
}

}  // namespace island
