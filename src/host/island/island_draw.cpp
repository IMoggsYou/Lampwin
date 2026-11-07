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
                const D2D1_COLOR_F& c, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool trim = true) {
        float b, h;
        LineOf(f, &b, &h);
        widgets::TextOptions o;
        o.align = align;
        o.rtl = s.rtl;
        o.trim = trim;
        widgets::DrawText(g, rt, t, f, {left, baseline - b, right, baseline - b + h + 1}, c, o);
    }
    // The box of a line of text whose baseline is at `baseline`.
    void LineBox(IDWriteTextFormat* f, float baseline, float* top, float* bottom) {
        float b, h;
        LineOf(f, &b, &h);
        *top = baseline - b;
        *bottom = baseline - b + h + 1;
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
    // A glyph scaled by `scale` about its middle and turned by `angle` degrees about a point
    // of its square (`px`, `py` from 0 to 1); with `back`, slashed.
    void Moving(island::Glyph glyph, float x, float y, float size, const D2D1_COLOR_F& c, float scale,
                float angle = 0, float px = 0.5f, float py = 0.5f, const D2D1_COLOR_F* back = nullptr) {
        D2D1::Matrix3x2F old;
        rt->GetTransform(&old);
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(angle, {x + size * px, y + size * py}) *
                         D2D1::Matrix3x2F::Scale(scale, scale, {x + size / 2, y + size / 2}) * old);
        if (back) DrawSlashed(rt, glyph, x, y, size, c, *back);
        else DrawGlyph(rt, glyph, x, y, size, c);
        rt->SetTransform(old);
    }
    // A picture (cover, app icon) in a rounded square.
    void Picture(IWICBitmapSource* source, D2D1_RECT_F r, float radius, float opacity = 1) {
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
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), opacity),
                      layer.get());
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

float Clamp(float v, float lo, float hi) {
    return std::min(std::max(v, lo), hi);
}

// ---------------------------------------------------------------------------
// Motion: every symbol comes in when its view does, buttons light up under the pointer and
// give a little when pressed. Times are seconds of Scene::since.
// ---------------------------------------------------------------------------

float EaseOut(float t) {
    t = Clamp(t, 0, 1);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// A little past the end and back: things that pop in.
float EaseBack(float t) {
    t = Clamp(t, 0, 1) - 1;
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * t * t * t + c1 * t * t;
}

// From 0 to 1 over `length` seconds, `delay` seconds after the view came up.
float Phase(const Scene& s, float delay, float length) {
    return Clamp((s.since - delay) / length, 0, 1);
}

// How big a symbol that pops in is now (from 0.6 of its size).
float Pop(const Scene& s, float delay = 0) {
    return 0.6f + 0.4f * EaseBack(Phase(s, delay, 0.42f));
}

D2D1_COLOR_F Faded(D2D1_COLOR_F c, float alpha) {
    c.a *= Clamp(alpha, 0, 1);
    return c;
}

float Lit(const Scene& s, Action a) {
    return a != Action::None && s.hover == a ? Clamp(s.hoverAmount, 0, 1) : 0;
}

// A held button shrinks a little; let go, it springs back past its size for a moment.
float PressScale(const Scene& s, Action a) {
    return a != Action::None && s.press == a ? 1 - 0.12f * s.pressAmount : 1;
}

// A button's colour under the pointer: a dark one lightens, a light one dims.
D2D1_COLOR_F LitFill(const D2D1_COLOR_F& fill, float lit) {
    const float luma = 0.2126f * fill.r + 0.7152f * fill.g + 0.0722f * fill.b;
    return luma > 0.6f ? widgets::Mix(fill, Rgb(0xD1D1D6, fill.a), 0.6f * lit)
                       : widgets::Mix(fill, Rgb(0xFFFFFF, fill.a), 0.14f * lit);
}

// The level bars sit in a square slot as wide as the cover (kSlot), so the island is the same
// on both sides: the cover's margin on the left, the bars' on the right.
const float kSlot = 24, kBarStep = 4.2f, kBarWidth = 2.6f;

// Five level bars `h` high at most, centred in the slot that starts at `slot`, around cy.
// They show the sound as it plays (Scene::levels, bass to treble); without it, two waves of
// different speed make each bar its own. Scene::live eases them to rest (and back) instead of
// a jump, and they rise one after another when the view comes in.
void Bars(Ctx& c, float slot, float cy, float h, const D2D1_COLOR_F& color, float phase) {
    const float t = c.s.time, rest = 3;
    const float x = slot + (kSlot - (4 * kBarStep + kBarWidth)) / 2 + kBarWidth / 2;
    for (int i = 0; i < 5; ++i) {
        float wave;
        if (c.s.hasLevels) {
            wave = Clamp(c.s.levels[i], 0, 1);
        } else {
            const float a = 0.5f + 0.5f * sinf(t * 5.1f + phase + i * 1.7f);
            const float b = 0.6f + 0.4f * sinf(t * 2.3f + phase * 0.7f + i * 2.9f);
            wave = 0.25f + 0.75f * a * b;
        }
        const float k = wave * Clamp(c.s.live, 0, 1) * EaseOut(Phase(c.s, 0.1f + i * 0.04f, 0.3f));
        const float bh = rest + (h - rest) * k;
        c.Line(x + i * kBarStep, cy - bh / 2, x + i * kBarStep, cy + bh / 2, kBarWidth, color);
    }
}

// How a line too long for its place (the song's title) moves: it rests at its start, then
// glides to the left at an even pace until the copy that follows it (one gap behind) stands
// where it began, and again. Seconds and DIPs.
const float kGlideRest = 1.6f, kGlideSpeed = 30, kGlideGap = 36, kGlideFade = 12;

// One line from `left` to `right`; a longer one glides along in a loop, its ends fading
// softly where it goes under. Right-to-left text, read the other way, is cut instead.
void Glide(Ctx& c, const std::wstring& t, IDWriteTextFormat* f, float left, float right, float baseline,
           const D2D1_COLOR_F& color) {
    const float room = right - left, width = c.Width(t, f);
    if (width <= room + 0.5f || room <= 0 || c.s.rtl || !c.rt) {
        c.TextAt(t, f, left, right, baseline, color);
        return;
    }
    const float cycle = width + kGlideGap, period = kGlideRest + cycle / kGlideSpeed;
    const float at = fmodf(std::max(c.s.time, 0.0f), period);
    const float shift = at < kGlideRest ? 0 : (at - kGlideRest) * kGlideSpeed;
    float top, bottom;
    c.LineBox(f, baseline, &top, &bottom);
    // The left edge fades only while the line moves; at rest its first letter shows whole.
    const float leftFade = Clamp(shift / kGlideFade, 0, 1), edge = Clamp(kGlideFade / room, 0, 0.5f);
    const D2D1_GRADIENT_STOP stops[] = {{0, Rgb(0x000000, 1 - leftFade)},
                                        {edge, Rgb(0x000000, 1)},
                                        {1 - edge, Rgb(0x000000, 1)},
                                        {1, Rgb(0x000000, 0)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> mask;
    ComPtr<ID2D1Layer> layer;
    if (FAILED(c.rt->CreateGradientStopCollection(stops, 4, collection.put())) ||
        FAILED(c.rt->CreateLinearGradientBrush({{left, 0}, {right, 0}}, collection.get(), mask.put())) ||
        FAILED(c.rt->CreateLayer(nullptr, layer.put()))) {
        c.TextAt(t, f, left, right, baseline, color);
        return;
    }
    c.rt->PushLayer(D2D1::LayerParameters({left, top, right, bottom}, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), 1, mask.get()),
                    layer.get());
    for (float x = left - shift; x < right; x += cycle) {
        c.TextAt(t, f, x, x + width + 2, baseline, color, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    }
    c.rt->PopLayer();
}

// Whether `t` needs to glide in `room` DIPs.
bool Glides(Ctx& c, const std::wstring& t, IDWriteTextFormat* f, float room) {
    return !c.s.rtl && c.Width(t, f) > room + 0.5f;
}

// A battery `level` (0 to 100, may be partway) full, 25 x 12 DIPs at x around cy.
void Battery(Ctx& c, float x, float cy, float level, const D2D1_COLOR_F& color, bool charging) {
    const float w = 25, h = 12, y = cy - h / 2;
    c.rt->DrawRoundedRectangle(D2D1::RoundedRect({x + 0.5f, y + 0.5f, x + w - 0.5f, y + h - 0.5f}, 3.5f, 3.5f),
                               c.Brush(Rgb(0xFFFFFF, 0.35f)).get(), 1);
    c.Fill({x + w + 1, cy - 2.5f, x + w + 2.6f, cy + 2.5f}, 1, Rgb(0xFFFFFF, 0.35f));
    const float fill = (w - 4) * Clamp(level, 0, 100) / 100.0f;
    if (fill > 0.2f) c.Fill({x + 2, y + 2, x + 2 + std::max(fill, 2.0f), y + h - 2}, 2, color);
    // The bolt drops in once the fill is on its way.
    if (charging) {
        c.Moving(Glyph::Bolt, x + w / 2 - 6, cy - 6, 12, Faded(P.text, Phase(c.s, 0.2f, 0.15f)), Pop(c.s, 0.2f));
    }
}

// A wide rounded button with a word on it.
void Button(Ctx& c, D2D1_RECT_F r, const D2D1_COLOR_F& fill, const std::wstring& text, const D2D1_COLOR_F& color,
            Action action, std::vector<Hit>* hits) {
    if (hits) hits->push_back({r, action});
    const float k = PressScale(c.s, action);
    D2D1::Matrix3x2F old;
    c.rt->GetTransform(&old);
    c.rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k, {(r.left + r.right) / 2, (r.top + r.bottom) / 2}) * old);
    c.Fill(r, (r.bottom - r.top) / 2, LitFill(fill, Lit(c.s, action)));
    c.Centered(text, c.Font(15, kSemibold), r, color);
    c.rt->SetTransform(old);
}

// A round button with a glyph, `d` across, centred on (cx, cy); it pops in `delay` seconds
// after its view.
void Round(Ctx& c, float cx, float cy, float d, const D2D1_COLOR_F& fill, Glyph glyph, const D2D1_COLOR_F& color,
           bool slashed, Action action, std::vector<Hit>* hits, float delay = 0) {
    if (hits) hits->push_back({{cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2}, action});
    const float k = PressScale(c.s, action) * Pop(c.s, delay), in = Phase(c.s, delay, 0.16f);
    const D2D1_COLOR_F back = Faded(LitFill(fill, Lit(c.s, action)), in);
    c.Dot(cx, cy, d / 2 * k, back);
    const float g = d * 0.46f;
    c.Moving(glyph, cx - g / 2, cy - g / 2, g, Faded(color, in), k, 0, 0.5f, 0.5f, slashed ? &back : nullptr);
}

// A media control: a glyph with a soft disc behind it under the pointer.
void Control(Ctx& c, float cx, float cy, float size, Glyph glyph, Action action, std::vector<Hit>* hits,
             float delay) {
    if (hits) hits->push_back({{cx - 26, cy - 24, cx + 26, cy + 24}, action});
    const float lit = Lit(c.s, action), k = PressScale(c.s, action) * Pop(c.s, delay);
    if (lit > 0.01f) c.Dot(cx, cy, 23 * k, Rgb(0xFFFFFF, 0.12f * lit));
    c.Moving(glyph, cx - size / 2, cy - size / 2, size, Faded(P.text, Phase(c.s, delay, 0.16f)), k);
}

// The cover: it pops in with its view, and a new song's cover fades in over the last one.
void Cover(Ctx& c, float x, float y, float size, float radius) {
    const float swap = EaseOut(c.s.trackSwap);
    const bool fading = swap < 1 && c.s.prevArt != c.s.art;
    const float k = (0.75f + 0.25f * EaseBack(Phase(c.s, 0, 0.45f))) * (fading ? 0.92f + 0.08f * swap : 1);
    D2D1::Matrix3x2F old;
    c.rt->GetTransform(&old);
    c.rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k, {x + size / 2, y + size / 2}) * old);
    const D2D1_RECT_F r = {x, y, x + size, y + size};
    if (fading) {
        if (c.s.prevArt) c.Picture(c.s.prevArt, r, radius, 1 - swap);
        else widgets::DrawMusicTile(c.rt, r, radius);
    }
    if (c.s.art) {
        c.Picture(c.s.art, r, radius, fading ? swap : 1);
    } else if (!fading || !c.s.prevArt) {
        widgets::DrawMusicTile(c.rt, r, radius);
    }
    c.rt->SetTransform(old);
}

// A line of the song (title or artist): a new song's text slides up into place as the last
// one's leaves.
void SongLine(Ctx& c, const std::wstring& now, const std::wstring& before, IDWriteTextFormat* f, float left,
              float right, float baseline, const D2D1_COLOR_F& color) {
    const float swap = EaseOut(c.s.trackSwap);
    if (swap < 1 && before != now) {
        c.TextAt(before, f, left, right, baseline - 7 * swap, Faded(color, 1 - swap * 1.6f));
        c.TextAt(now, f, left, right, baseline + 7 * (1 - swap), Faded(color, swap));
        return;
    }
    Glide(c, now, f, left, right, baseline, color);
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

// The compact music pill: the cover, the title and the bars' slot, each 6 DIPs from the edge
// and 8 apart. Its width follows the title, between these two.
const float kPillEdge = 6, kPillGap = 8, kPillMin = 120, kPillMax = 300;
const float kPillText = kPillEdge + kSlot + kPillGap;   // where the title starts and, mirrored, ends
// Every compact view is as wide as what is on it, between kPillMin and this: no empty black.
const float kPillWide = 340;
// Words that start or end a pill without a symbol keep this far from its rounded end.
const float kPillWords = 16;
// The battery (25 DIPs and its tip) and the gap before it.
const float kBatteryWidth = 27.6f, kBatteryGap = 6;

float FitPill(float content, float most = kPillWide) {
    return Clamp(ceilf(content), kPillMin, most);
}

// The grown card: the cover's margin (kPad) on the left, the bars' on the right; the words
// between the cover and the bars' slot.
const float kCardText = 88, kCardTextEnd = kPad + kSlot + 12;

// ---------------------------------------------------------------------------
// The views. Each draws into a (w, h) island at the origin.
// ---------------------------------------------------------------------------

void MusicView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const widgets::MediaInfo& m = *c.s.media;
    const std::wstring by = m.artist.empty() ? m.app : m.artist;
    // The title follows the cover in from the side.
    const float slide = 10 * (1 - EaseOut(Phase(c.s, 0.05f, 0.4f)));
    if (!c.s.expanded) {
        Cover(c, kPillEdge, kPillEdge, kSlot, 6);
        SongLine(c, m.title, c.s.prevTitle, c.Font(12.5f, kSemibold), kPillText + slide, w - kPillText, kLine,
                 P.text);
        Bars(c, w - kPillEdge - kSlot, kCompact / 2, 14, c.s.accent, 0);
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    Cover(c, kPad, kPad, 56, 12);
    SongLine(c, m.title, c.s.prevTitle, c.Font(15, kSemibold), kCardText + slide, w - kCardTextEnd, 42, P.text);
    SongLine(c, by, c.s.prevBy, c.Font(13, kRegular), kCardText + slide, w - kCardTextEnd, 61, P.secondary);
    Bars(c, w - kPad - kSlot, 37, 16, c.s.accent, 0);
    float cy = 105;
    if (m.duration > 0) {
        const INT64 total = m.duration / 10000000, now = std::clamp<INT64>(c.s.position / 10000000, 0, total);
        const float y = 104;
        c.TextAt(Clock(now), c.Font(11, kSemibold), kPad, 52, y + 4, P.secondary, DWRITE_TEXT_ALIGNMENT_TRAILING);
        c.TextAt(L"-" + Clock(total - now), c.Font(11, kSemibold), w - 52, w - 18, y + 4, P.secondary);
        // The track draws itself across, then the part played follows (smoothly: the host
        // passes the position of this very frame).
        const float reach = EaseOut(Phase(c.s, 0.08f, 0.45f));
        const float length = (w - 124) * reach;
        c.Line(62, y, 62 + length, y, 5, P.track);
        const double exact = total > 0 ? (double)c.s.position / ((double)m.duration) : 0;
        c.Line(62, y, 62 + length * (float)std::min(exact, 1.0), y, 5, P.text);
        cy = 145;
    }
    Control(c, w / 2 - 78, cy, 30, Glyph::Previous, Action::Previous, hits, 0.1f);
    // Play and pause trade places: the old one shrinks away as the new one pops up.
    {
        const float swap = c.s.playSwap, k = PressScale(c.s, Action::PlayPause);
        const float cx = w / 2, lit = Lit(c.s, Action::PlayPause);
        if (hits) hits->push_back({{cx - 26, cy - 24, cx + 26, cy + 24}, Action::PlayPause});
        if (lit > 0.01f) c.Dot(cx, cy, 25 * k, Rgb(0xFFFFFF, 0.12f * lit));
        const Glyph now = m.playing ? Glyph::Pause : Glyph::Play, before = m.playing ? Glyph::Play : Glyph::Pause;
        const float in = Phase(c.s, 0.14f, 0.16f);
        if (swap < 1) {
            c.Moving(before, cx - 17, cy - 17, 34, Faded(P.text, (1 - EaseOut(swap * 2.2f)) * in),
                     k * (1 - 0.5f * EaseOut(swap * 1.5f)));
        }
        c.Moving(now, cx - 17, cy - 17, 34, Faded(P.text, EaseOut(swap * 1.8f) * in),
                 k * Pop(c.s, 0.14f) * (0.55f + 0.45f * EaseBack(swap)));
    }
    Control(c, w / 2 + 78, cy, 30, Glyph::Next, Action::Next, hits, 0.18f);
    if (hits) hits->push_back({{0, 0, w, cy - 26}, Action::Toggle});
}

void CallView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const CallInfo& call = *c.s.call;
    const std::wstring time = Clock(c.s.callSeconds);
    // The phone rings in: a few quick turns that die away.
    const float ring = c.s.since < 1.2f ? 16 * sinf(c.s.since * 30) * expf(-c.s.since * 4.5f) : 0;
    if (!c.s.expanded) {
        // The phone in the cover's slot, the bars in theirs: the same margins on both sides.
        c.Moving(Glyph::Phone, kPillEdge + 2, kPillEdge + 2, 20, P.green, Pop(c.s), ring);
        c.TextAt(time, c.Font(13, kSemibold), kPillText + 8 * (1 - EaseOut(Phase(c.s, 0.05f, 0.35f))),
                 w - kPillText, kLine, P.green);
        Bars(c, w - kPillEdge - kSlot, kCompact / 2, 14, P.green, 0.8f);
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    {
        D2D1::Matrix3x2F old;
        c.rt->GetTransform(&old);
        const float k = 0.75f + 0.25f * EaseBack(Phase(c.s, 0, 0.45f));
        c.rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k, {kPad + 22, kPad + 22}) * old);
        CallIcon(c, kPad, kPad, 44);
        c.rt->SetTransform(old);
    }
    c.TextAt(call.app, c.Font(16, kSemibold), 76, w - 56, 40, P.text);
    const std::wstring line = Fill(c.T(c.s.micMuted ? L"host.island.callMuted" : L"host.island.call"), time);
    c.TextAt(line, c.Font(13, kRegular), 76, w - 56, 60, P.secondary);
    const D2D1_COLOR_F black = P.black;
    if (c.s.micMuted) c.Moving(Glyph::Mic, w - 44, 28, 20, P.secondary, Pop(c.s, 0.06f), 0, 0.5f, 0.5f, &black);
    else c.Moving(Glyph::Mic, w - 44, 28, 20, P.green, Pop(c.s, 0.06f));
    // The microphone (on: white, like iOS), the camera when it is on, the program's window;
    // they pop in one after another.
    const float cy = 112;
    std::vector<int> buttons = {0};
    if (call.camera) buttons.push_back(1);
    if (!call.path.empty()) buttons.push_back(2);
    const float step = 62, x0 = w / 2 - step * (buttons.size() - 1) / 2.0f;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const float cx = x0 + step * i, delay = 0.1f + 0.05f * i;
        if (buttons[i] == 0) {
            if (c.s.micMuted) Round(c, cx, cy, 48, P.button, Glyph::Mic, P.text, true, Action::MicMute, hits, delay);
            else Round(c, cx, cy, 48, P.text, Glyph::Mic, P.black, false, Action::MicMute, hits, delay);
        } else if (buttons[i] == 1) {
            Round(c, cx, cy, 48, P.button, Glyph::Camera, P.green, false, Action::None, nullptr, delay);
        } else {
            Round(c, cx, cy, 48, P.button, Glyph::Open, P.text, false, Action::ShowApp, hits, delay);
        }
    }
    if (hits) hits->push_back({{0, 0, w, cy - 26}, Action::Toggle});
}

void MirroringView(Ctx& c, float w, float h, std::vector<Hit>* hits) {
    const MirrorInfo& m = *c.s.mirror;
    const std::wstring& title = c.T(L"host.island.mirroring");
    const std::wstring target = !m.target.empty() ? m.target : c.T(L"host.island.mirroring.duplicate");
    const float slide = 8 * (1 - EaseOut(Phase(c.s, 0.05f, 0.35f)));
    if (!c.s.expanded) {
        // Both symbols in the slots the cover and the bars have elsewhere.
        c.Moving(Glyph::Mirroring, kPillEdge + 1, 7, 22, P.blue, Pop(c.s));
        c.TextAt(title, c.Font(12.5f, kSemibold), kPillText + slide, w - kPillText, kLine, P.text);
        c.Moving(Glyph::Display, w - kPillEdge - kSlot + 2, 8, 20, Faded(P.secondary, Phase(c.s, 0.12f, 0.2f)),
                 Pop(c.s, 0.12f));
        if (hits) hits->push_back({{0, 0, w, h}, Action::Toggle});
        return;
    }
    c.Moving(Glyph::Mirroring, kPad, kPad, 32, P.blue, Pop(c.s));
    c.TextAt(title, c.Font(15, kSemibold), 64 + slide, w - 56, 36, P.text);
    c.TextAt(target, c.Font(13, kRegular), 64 + slide, w - 56, 55, P.secondary);
    c.Moving(Glyph::Display, w - 44, 24, 24, Faded(P.secondary, Phase(c.s, 0.12f, 0.2f)), Pop(c.s, 0.12f));
    Button(c, {14, h - 54, w - 14, h - 14}, Rgb(0x0A84FF, 0.22f), c.T(L"host.island.mirroring.stop"), P.link,
           Action::StopMirroring, hits);
    if (hits) hits->push_back({{0, 0, w, h - 58}, Action::Toggle});
}

// Charging: the battery fills up to its level and the number counts along. Low: the fill
// blinks a few times.
void BatteryView(Ctx& c, float w, bool low) {
    const D2D1_COLOR_F color = !low ? P.green : c.s.battery <= 10 ? P.red : Rgb(0xFFD60A);
    const std::wstring& title = c.T(low ? L"host.island.lowBattery" : L"host.island.charging");
    // The words from the left end, the battery at the right one, the number just before it.
    const float battery = w - kPillWords - kBatteryWidth;
    c.TextAt(title, c.Font(13, kSemibold), kPillWords + 8 * (1 - EaseOut(Phase(c.s, 0, 0.35f))), battery - 40, kLine,
             P.text);
    const float fill = low ? 1 : EaseOut(Phase(c.s, 0.05f, 0.85f));
    const float level = c.s.battery * fill;
    c.TextAt(Percent((int)lroundf(level)), c.Font(13, kSemibold), battery - 60, battery - kBatteryGap, kLine, color,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    const float blink = low && c.s.since < 1.6f ? 0.55f + 0.45f * cosf(c.s.since * 3.14159f * 2.5f) : 1;
    Battery(c, battery, kCompact / 2, level, Faded(color, blink), !low);
}

// Silent: the bell shakes like the one on the iPhone's switch, and its back turns red.
void SoundView(Ctx& c, float w, bool silent, std::vector<Hit>* hits) {
    const float turn = EaseOut(Phase(c.s, 0, 0.3f));
    const D2D1_COLOR_F back = silent ? widgets::Mix(P.dim, P.red, turn) : P.dim;
    c.Fill({6, 6, 48, 30}, 12, back);
    const float shake = c.s.since < 1.4f ? 24 * sinf(c.s.since * 26) * expf(-c.s.since * 4) : 0;
    if (silent) c.Moving(Glyph::Bell, 18, 9, 18, P.text, 1, shake, 0.5f, 0.1f, &back);
    else c.Moving(Glyph::Bell, 18, 9, 18, P.text, 1, shake, 0.5f, 0.1f);
    c.TextAt(c.T(silent ? L"host.island.silent" : L"host.island.sound"), c.Font(13, kSemibold), 60, w - kPillWords,
             kLine, silent ? P.red : P.text, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (hits) hits->push_back({{0, 0, w, kCompact}, Action::ToggleMute});
}

// The plane flies in from the left.
void AirplaneOffView(Ctx& c, float w) {
    const float fly = 1 - EaseOut(Phase(c.s, 0, 0.5f));
    c.Moving(Glyph::Airplane, kPillEdge + 1 - 14 * fly, 7 + 4 * fly, 22, Faded(P.secondary, 1 - fly), 1);
    IDWriteTextFormat* f = c.Font(13, kSemibold);
    const float off = c.Width(c.T(L"host.island.off"), f);
    c.TextAt(c.T(L"host.island.airplane"), f, kPillText, w - kPillWords - off - 10, kLine, P.text);
    c.TextAt(c.T(L"host.island.off"), f, w - kPillWords - off - 2, w - kPillWords, kLine, P.secondary,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// A card with a glyph, a title, a line or two, and buttons at the bottom.
void NoticeCard(Ctx& c, float w, float h, Glyph glyph, const D2D1_COLOR_F& color, const wchar_t* title,
                const wchar_t* text) {
    const float fly = glyph == Glyph::Airplane ? 1 - EaseOut(Phase(c.s, 0, 0.55f)) : 0;
    c.Moving(glyph, 18 - 16 * fly, 18 + 6 * fly, 32, Faded(color, 1 - fly), fly > 0 ? 1 : Pop(c.s));
    const float slide = 8 * (1 - EaseOut(Phase(c.s, 0.06f, 0.35f)));
    c.TextAt(c.T(title), c.Font(15, kSemibold), 62 + slide, w - kPad, 34, P.text);
    c.Wrapped(c.T(text), c.Font(13, kRegular), {62 + slide, 40, w - kPad + slide, h - 58}, P.secondary);
}

void OutputView(Ctx& c, float w) {
    // The symbol as far from the left end as the words are from the right one.
    c.Moving(c.s.headphones ? Glyph::Headphones : Glyph::Speaker, 17, 17, 30, P.text, Pop(c.s));
    const float slide = 8 * (1 - EaseOut(Phase(c.s, 0.06f, 0.35f)));
    c.TextAt(c.T(L"host.island.connected"), c.Font(12, kSemibold), 59 + slide, w - 17, 28, P.secondary);
    c.TextAt(c.s.output, c.Font(15, kSemibold), 59 + slide, w - 17, 47, P.text);
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

bool Scrolls(widgets::Graphics& g, const Scene& s) {
    if (s.view != View::Music || !s.media) return false;
    Ctx c(g, nullptr, s);
    if (!s.expanded) return Glides(c, s.media->title, c.Font(12.5f, kSemibold), Size(g, s).width - 2 * kPillText);
    const float room = 360 - kCardText - kCardTextEnd;
    const std::wstring& by = s.media->artist.empty() ? s.media->app : s.media->artist;
    return Glides(c, s.media->title, c.Font(15, kSemibold), room) || Glides(c, by, c.Font(13, kRegular), room);
}

D2D1_SIZE_F Size(widgets::Graphics& g, const Scene& s) {
    Ctx c(g, nullptr, s);
    switch (s.view) {
        case View::None: return {120, 24};
        case View::Music: {
            if (s.expanded) return {360, s.media && s.media->duration > 0 ? 176.0f : 136.0f};
            // As wide as its title needs, within limits; a longer title glides.
            const float tw = s.media ? c.Width(s.media->title, c.Font(12.5f, kSemibold)) : 0;
            return {FitPill(2 * kPillText + tw + 2, kPillMax), kCompact};
        }
        case View::Call: {
            if (s.expanded) return {360, 150};
            // The phone and the bars in their slots, the call's time between them.
            const float tw = c.Width(Clock(s.callSeconds), c.Font(13, kSemibold));
            return {FitPill(2 * kPillText + tw), kCompact};
        }
        case View::Mirroring: {
            if (s.expanded) return {360, 136};
            const float tw = c.Width(c.T(L"host.island.mirroring"), c.Font(12.5f, kSemibold));
            return {FitPill(2 * kPillText + tw + 2), kCompact};
        }
        case View::Charging:
        case View::LowBattery: {
            IDWriteTextFormat* f = c.Font(13, kSemibold);
            const wchar_t* title = s.view == View::LowBattery ? L"host.island.lowBattery" : L"host.island.charging";
            const float tw = c.Width(c.T(title), f);
            const float pw = c.Width(L"100%", f);
            return {FitPill(kPillWords + tw + 16 + pw + kBatteryGap + kBatteryWidth + kPillWords), kCompact};
        }
        case View::Silent:
        case View::Sound: {
            // The bell's capsule (6 to 48), a gap, the words, the end.
            const float tw = c.Width(c.T(s.view == View::Silent ? L"host.island.silent" : L"host.island.sound"),
                                     c.Font(13, kSemibold));
            return {FitPill(48 + 14 + tw + kPillWords + 2), kCompact};
        }
        case View::AirplaneOff: {
            const float tw = c.Width(c.T(L"host.island.airplane"), c.Font(13, kSemibold)) +
                             c.Width(c.T(L"host.island.off"), c.Font(13, kSemibold));
            return {FitPill(kPillText + tw + 12 + kPillWords + 2), kCompact};
        }
        case View::AirplaneOn:
        case View::MobileData: return {360, 146};
        case View::Output: {
            const float tw = std::max(c.Width(s.output, c.Font(15, kSemibold)),
                                      c.Width(c.T(L"host.island.connected"), c.Font(12, kSemibold)));
            return {FitPill(59 + tw + 17 + 2, 380), 64};
        }
    }
    return {120, 24};
}

void DrawShape(widgets::Graphics& g, ID2D1RenderTarget* rt, float cx, float top, float w, float h) {
    const float radius = std::min(h / 2, kRadius);
    const float left = cx - w / 2;
    Scene none;
    Ctx c(g, rt, none);
    rt->FillRoundedRectangle(D2D1::RoundedRect({left, top, left + w, top + h}, radius, radius), c.Brush(P.black).get());
    // A faint edge: the island stays defined over a black window or wallpaper.
    rt->DrawRoundedRectangle(D2D1::RoundedRect({left + 0.375f, top + 0.375f, left + w - 0.375f, top + h - 0.375f},
                                               radius - 0.375f, radius - 0.375f),
                             c.Brush(Rgb(0xFFFFFF, 0.10f)).get(), 0.75f);
}

void DrawContent(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float top, float w, float h,
                 float cw, float ch, float opacity, float scale, std::vector<Hit>* hits) {
    if (opacity <= 0.01f || s.view == View::None) return;
    const float radius = std::min(h / 2, kRadius);
    const float left = cx - w / 2, inner = cx - cw / 2;
    Ctx c(g, rt, s);
    // Clipped to the shape as it is now: content laid out for where the island is going is
    // uncovered as the island grows.
    ComPtr<ID2D1Factory> f;
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    ComPtr<ID2D1Layer> layer;
    rt->GetFactory(f.put());
    if (FAILED(f->CreateRoundedRectangleGeometry(D2D1::RoundedRect({left, top, left + w, top + h}, radius, radius),
                                                 mask.put())) ||
        FAILED(rt->CreateLayer(nullptr, layer.put()))) {
        return;
    }
    D2D1::Matrix3x2F old;
    rt->GetTransform(&old);
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                        D2D1::IdentityMatrix(), Clamp(opacity, 0, 1)),
                  layer.get());
    rt->SetTransform(D2D1::Matrix3x2F::Translation(inner, top) *
                     D2D1::Matrix3x2F::Scale(scale, scale, {cx, top + ch / 2}) * old);
    std::vector<Hit> local;
    std::vector<Hit>* out = hits ? &local : nullptr;
    switch (s.view) {
        case View::Music:
            if (s.media) MusicView(c, cw, ch, out);
            break;
        case View::Call:
            if (s.call) CallView(c, cw, ch, out);
            break;
        case View::Mirroring:
            if (s.mirror) MirroringView(c, cw, ch, out);
            break;
        case View::Charging: BatteryView(c, cw, false); break;
        case View::LowBattery: BatteryView(c, cw, true); break;
        case View::Silent: SoundView(c, cw, true, out); break;
        case View::Sound: SoundView(c, cw, false, out); break;
        case View::AirplaneOff: AirplaneOffView(c, cw); break;
        case View::AirplaneOn:
            NoticeCard(c, cw, ch, Glyph::Airplane, P.orange, L"host.island.airplaneOn.title",
                       L"host.island.airplaneOn.text");
            Button(c, {14, ch - 54, cw - 14, ch - 14}, P.button, c.T(L"host.island.openSettings"), P.text,
                   Action::OpenAirplane, out);
            break;
        case View::MobileData: {
            NoticeCard(c, cw, ch, Glyph::Antenna, P.green, L"host.island.mobileData.title",
                       L"host.island.mobileData.text");
            const float mid = cw / 2;
            Button(c, {14, ch - 54, mid - 5, ch - 14}, P.button, c.T(L"host.island.ok"), P.text, Action::Dismiss, out);
            Button(c, {mid + 5, ch - 54, cw - 14, ch - 14}, Rgb(0x0A84FF, 0.22f), c.T(L"host.island.settings"),
                   P.link, Action::OpenCellular, out);
            break;
        }
        case View::Output: OutputView(c, cw); break;
        case View::None: break;
    }
    rt->SetTransform(old);
    rt->PopLayer();
    if (hits) {
        for (Hit h : local) {
            h.rect = {h.rect.left + inner, h.rect.top + top, h.rect.right + inner, h.rect.bottom + top};
            hits->push_back(h);
        }
    }
}

void Draw(widgets::Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float cx, float top, float w, float h,
          float content, std::vector<Hit>* hits) {
    DrawShape(g, rt, cx, top, w, h);
    DrawContent(g, rt, s, cx, top, w, h, w, h, content, 1, hits);
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
