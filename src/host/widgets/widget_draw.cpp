// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_draw.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <vector>

namespace widgets {

namespace {

const float kPi = 3.14159265f;
const float kPad = 16;   // DIPs between the card's edge and what is on it

const DWRITE_FONT_WEIGHT kLight = DWRITE_FONT_WEIGHT_LIGHT;
const DWRITE_FONT_WEIGHT kRegular = DWRITE_FONT_WEIGHT_NORMAL;
const DWRITE_FONT_WEIGHT kMedium = DWRITE_FONT_WEIGHT_MEDIUM;
const DWRITE_FONT_WEIGHT kSemibold = DWRITE_FONT_WEIGHT_SEMI_BOLD;

struct Palette {
    D2D1_COLOR_F top, bottom;   // card background
    D2D1_COLOR_F text, secondary, tertiary;
    D2D1_COLOR_F hairline, red, track, face;
};

Palette Colors(bool dark) {
    if (dark) {
        return {Rgb(0x2C2C2F), Rgb(0x1C1C1E), Rgb(0xF5F5F7), Rgb(0x98989D), Rgb(0x5C5C60),
                Rgb(0xFFFFFF, 0.10f), Rgb(0xFF453A), Rgb(0x3A3A3D), Rgb(0x0B0B0C)};
    }
    return {Rgb(0xFFFFFF), Rgb(0xF5F5F7), Rgb(0x1D1D1F), Rgb(0x86868B), Rgb(0xC7C7CC),
            Rgb(0x000000, 0.07f), Rgb(0xFF3B30), Rgb(0xE8E8ED), Rgb(0xF2F2F5)};
}

// One card being drawn: DIP coordinates, mirrored for right-to-left languages. Text is
// never mirrored, only where it goes; a clock face or a picture isn't mirrored at all.
class Ctx {
public:
    Ctx(Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float unit, float w, float h)
        : g(g), rt(rt), s(s), W(w), H(h), p(Colors(s.dark)) {
        plain_ = D2D1::Matrix3x2F::Scale(unit, unit);
        mirrored_ = s.rtl ? D2D1::Matrix3x2F::Scale(-1, 1) * D2D1::Matrix3x2F::Translation(w, 0) * plain_ : plain_;
        rt->SetTransform(mirrored_);
    }

    Graphics& g;
    ID2D1RenderTarget* rt;
    const Scene& s;
    const float W, H;
    const Palette p;

    void Mirrored() { rt->SetTransform(mirrored_); }
    void Unmirrored() { rt->SetTransform(plain_); }
    float X(float x) const { return s.rtl ? W - x : x; }
    // A unit square at (x, y) of `size` DIPs, in the mirrored frame.
    void Box(float x, float y, float size) {
        rt->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * mirrored_);
    }

    ComPtr<ID2D1SolidColorBrush> Brush(const D2D1_COLOR_F& c) {
        ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(c, b.put());
        return b;
    }
    ID2D1StrokeStyle* Round() {
        if (!round_) {
            ComPtr<ID2D1Factory> f;
            rt->GetFactory(f.put());
            f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                             D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                 nullptr, 0, round_.put());
        }
        return round_.get();
    }
    void Line(float x0, float y0, float x1, float y1, float width, const D2D1_COLOR_F& c) {
        rt->DrawLine({x0, y0}, {x1, y1}, Brush(c).get(), width, Round());
    }
    void Dot(float x, float y, float r, const D2D1_COLOR_F& c) {
        rt->FillEllipse(D2D1::Ellipse({x, y}, r, r), Brush(c).get());
    }

    IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight, bool display = false) {
        return g.Font(size, weight, display);
    }
    // The font at `size`, or smaller down to `smallest` until `text` fits in `width`: a long
    // word in some language ("Преимущественно солнечно") still fits the card.
    IDWriteTextFormat* Fit(const std::wstring& text, float size, float smallest, float width, DWRITE_FONT_WEIGHT weight,
                           bool display = false) {
        IDWriteTextFormat* f = Font(size, weight, display);
        while (size > smallest && TextWidth(g, text, f) > width) {
            size -= 0.5f;
            f = Font(size, weight, display);
        }
        return f;
    }
    void Text(const std::wstring& t, IDWriteTextFormat* f, D2D1_RECT_F box, const D2D1_COLOR_F& c,
              TextOptions o = {}) {
        if (s.rtl) box = {W - box.right, box.top, W - box.left, box.bottom};
        o.rtl = s.rtl;
        rt->SetTransform(plain_);
        DrawText(g, rt, t, f, box, c, o);
        rt->SetTransform(mirrored_);
    }
    const std::wstring& T(const wchar_t* key) const {
        static const std::wstring empty;
        if (!s.texts) return empty;
        auto it = s.texts->find(key);
        return it != s.texts->end() ? it->second : empty;
    }

private:
    D2D1::Matrix3x2F plain_, mirrored_;
    ComPtr<ID2D1StrokeStyle> round_;
};

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::wstring DateText(const Scene& s, const SYSTEMTIME& st, const wchar_t* picture) {
    wchar_t buf[128];
    int n = GetDateFormatEx(s.locale.c_str(), 0, &st, picture, buf, ARRAYSIZE(buf), nullptr);
    return n > 1 ? std::wstring(buf, (size_t)n - 1) : std::wstring();
}

std::wstring Upper(const Scene& s, const std::wstring& t) {
    if (t.empty()) return t;
    std::wstring out(t.size() * 2, L'\0');
    int n = LCMapStringEx(s.locale.c_str(), LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, t.c_str(), (int)t.size(),
                          &out[0], (int)out.size(), nullptr, nullptr, 0);
    return n > 0 ? out.substr(0, (size_t)n) : t;
}

// "październik" -> "Październik": month names stand alone here.
std::wstring Capitalized(const Scene& s, const std::wstring& t) {
    return t.empty() ? t : Upper(s, t.substr(0, 1)) + t.substr(1);
}

std::wstring Fill(std::wstring pattern, const std::wstring& a0, const std::wstring& a1 = L"") {
    size_t at = pattern.find(L"{0}");
    if (at != std::wstring::npos) pattern.replace(at, 3, a0);
    at = pattern.find(L"{1}");
    if (at != std::wstring::npos) pattern.replace(at, 3, a1);
    return pattern;
}

std::wstring Degrees(double t) {
    long v = lround(t);
    // A real minus sign: the hyphen is too short next to the digits. The marks around it
    // keep "22°" in that order inside right-to-left text (Arabic would show "°22").
    return L"‎" + (v < 0 ? L"−" + std::to_wstring(-v) : std::to_wstring(v)) + L"°‎";
}

std::wstring Percent(int v) {
    return std::to_wstring(v) + L"%";
}

// "9" / "21" in 24-hour countries, "9 AM" elsewhere.
std::wstring HourText(const Scene& s, int hour) {
    if (s.clock24) return std::to_wstring(hour);
    SYSTEMTIME st = s.now;
    st.wHour = (WORD)hour;
    st.wMinute = st.wSecond = 0;
    wchar_t buf[32];
    int n = GetTimeFormatEx(s.locale.c_str(), 0, &st, L"h tt", buf, ARRAYSIZE(buf));
    return n > 1 ? std::wstring(buf, (size_t)n - 1) : std::to_wstring(hour % 12 == 0 ? 12 : hour % 12);
}

// The time as Windows writes it for this user (12 / 24 hours, leading zero), no seconds.
std::wstring TimeText(const Scene& s, std::wstring* marker) {
    wchar_t buf[64];
    int n = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS | TIME_NOTIMEMARKER, &s.now, nullptr, buf,
                            ARRAYSIZE(buf));
    std::wstring t = n > 1 ? std::wstring(buf, (size_t)n - 1) : std::wstring();
    // Some formats leave the separator before the marker behind.
    while (!t.empty() && (t.back() == L' ' || t.back() == L' ')) t.pop_back();
    marker->clear();
    if (!s.clock24) {
        n = GetTimeFormatEx(s.locale.c_str(), 0, &s.now, L"tt", buf, ARRAYSIZE(buf));
        if (n > 1) marker->assign(buf, (size_t)n - 1);
    }
    return t;
}

// ---------------------------------------------------------------------------
// Card
// ---------------------------------------------------------------------------

ComPtr<ID2D1PathGeometry> CardShape(Ctx& c, float inset = 0) {
    return Squircle(c.g.D2D(), {inset, inset, c.W - inset, c.H - inset}, kCardRadius - inset, kCardSmoothing);
}

void FillCard(Ctx& c, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom) {
    ComPtr<ID2D1PathGeometry> shape = CardShape(c);
    D2D1_GRADIENT_STOP stops[] = {{0, top}, {1, bottom}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> brush;
    if (!shape || FAILED(c.rt->CreateGradientStopCollection(stops, 2, collection.put())) ||
        FAILED(c.rt->CreateLinearGradientBrush({{0, 0}, {0, c.H}}, collection.get(), brush.put()))) {
        return;
    }
    c.rt->FillGeometry(shape.get(), brush.get());
}

// A hairline along the edge: the card stays defined on a wallpaper of its own color.
void CardEdge(Ctx& c, const D2D1_COLOR_F& color, float unit) {
    float px = 1.0f / unit;
    ComPtr<ID2D1PathGeometry> edge = CardShape(c, px / 2);
    if (edge) c.rt->DrawGeometry(edge.get(), c.Brush(color).get(), px);
}

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

void Hand(Ctx& c, float cx, float cy, float angle, float from, float to, float width, const D2D1_COLOR_F& color) {
    float dx = sinf(angle), dy = -cosf(angle);
    c.Line(cx + dx * from, cy + dy * from, cx + dx * to, cy + dy * to, width, color);
}

void DialFace(Ctx& c, float cx, float cy, float r);
void DialHands(Ctx& c, float cx, float cy, float r);

// A watch-like face: minute ticks, numerals, hands with a thin stem and a soft shadow.
void Dial(Ctx& c, float cx, float cy, float r) {
    c.Unmirrored();
    if (c.s.pass != Pass::Hands) DialFace(c, cx, cy, r);
    if (c.s.pass != Pass::Face) DialHands(c, cx, cy, r);
    c.Mirrored();
}

void DialFace(Ctx& c, float cx, float cy, float r) {
    const Palette& p = c.p;
    c.Dot(cx, cy, r, p.face);
    c.rt->DrawEllipse(D2D1::Ellipse({cx, cy}, r - 0.25f, r - 0.25f), c.Brush(p.hairline).get(), 0.5f);
    for (int i = 0; i < 60; ++i) {
        bool hour = i % 5 == 0;
        float a = i * kPi / 30;
        float dx = sinf(a), dy = -cosf(a);
        float outer = r * 0.94f, inner = outer - (hour ? r * 0.075f : r * 0.04f);
        c.Line(cx + dx * inner, cy + dy * inner, cx + dx * outer, cy + dy * outer, hour ? 1.4f : 0.8f,
               hour ? p.secondary : p.tertiary);
    }
    IDWriteTextFormat* numerals = c.Font(r * 0.2f, kSemibold, true);
    for (int i = 1; i <= 12; ++i) {
        float a = i * kPi / 6, rr = r * 0.665f;
        float x = cx + sinf(a) * rr, y = cy - cosf(a) * rr;
        TextOptions o;
        o.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        o.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        // Numerals stay where a clock has them, in every language.
        DrawText(c.g, c.rt, std::to_wstring(i), numerals, {x - r * 0.3f, y - r * 0.2f, x + r * 0.3f, y + r * 0.2f},
                 p.text, o);
    }
}

void DialHands(Ctx& c, float cx, float cy, float r) {
    const Palette& p = c.p;
    const SYSTEMTIME& t = c.s.now;
    float sec = c.s.seconds ? (float)t.wSecond : 0;
    float minute = (t.wMinute + sec / 60) * kPi / 30;
    float hour = ((t.wHour % 12) + t.wMinute / 60.0f + sec / 3600) * kPi / 6;
    const D2D1_COLOR_F shadow = Rgb(0x000000, c.s.dark ? 0.35f : 0.16f);
    const float stem = r * 0.13f;
    for (int pass = 0; pass < 2; ++pass) {
        float oy = pass == 0 ? r * 0.025f : 0;
        const D2D1_COLOR_F& col = pass == 0 ? shadow : p.text;
        Hand(c, cx, cy + oy, hour, 0, stem, r * 0.035f, col);
        Hand(c, cx, cy + oy, hour, stem, r * 0.52f, r * 0.085f, col);
        Hand(c, cx, cy + oy, minute, 0, stem, r * 0.035f, col);
        Hand(c, cx, cy + oy, minute, stem, r * 0.82f, r * 0.07f, col);
    }
    c.Dot(cx, cy, r * 0.065f, p.text);
    if (c.s.seconds) {
        const D2D1_COLOR_F orange = Rgb(0xFF9500);
        float a = t.wSecond * kPi / 30;
        Hand(c, cx, cy, a, -r * 0.16f, r * 0.88f, r * 0.022f, orange);
        c.Dot(cx, cy, r * 0.05f, orange);
        c.Dot(cx, cy, r * 0.02f, p.face);
    }
}

void DrawClock(Ctx& c) {
    const Palette& p = c.p;
    if (c.s.pass != Pass::Hands) FillCard(c, p.top, p.bottom);
    if (c.s.size == Size::Small) {
        Dial(c, c.W / 2, c.H / 2, c.H / 2 - 13);
        return;
    }
    // Medium: the face on the left, the time in words on the right.
    float r = c.H / 2 - 13;
    Dial(c, c.X(c.H / 2), c.H / 2, r);
    if (c.s.pass == Pass::Face) return;
    std::wstring marker;
    std::wstring time = TimeText(c.s, &marker);
    float left = c.H + 4, right = c.W - kPad;
    IDWriteTextFormat* big = c.Font(46, kMedium, true);
    float tw = TextWidth(c.g, time, big);
    IDWriteTextFormat* small = c.Font(17, kMedium, true);
    float mw = marker.empty() ? 0 : TextWidth(c.g, marker, small) + 5;
    // Too wide for the space (a long format): smaller digits.
    if (tw + mw > right - left) {
        big = c.Font(36, kMedium, true);
        tw = TextWidth(c.g, time, big);
    }
    const float top = 34;
    c.Text(time, big, {left, top, left + tw + 1, top + 64}, p.text);
    if (!marker.empty()) c.Text(marker, small, {left + tw + 5, top + 28, right, top + 56}, p.secondary);
    std::wstring date = DateText(c.s, c.s.now, c.T(L"host.widgets.date.long").c_str());
    date = Capitalized(c.s, date);
    c.Text(date, c.Fit(date, 14, 11, right - left, kSemibold), {left, top + 66, right, top + 88}, p.secondary);
}

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

int DaysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[(month - 1) % 12];
}

// Monday = 0 ... Sunday = 6, for a date's day of the week.
int MondayBased(WORD dayOfWeek) {
    return (dayOfWeek + 6) % 7;
}

// The month of `now` as a grid of 7 columns starting at the user's first day of the week.
void MonthGrid(Ctx& c, D2D1_RECT_F box, float numberSize, float headerSize) {
    const Scene& s = c.s;
    const Palette& p = c.p;
    SYSTEMTIME first = s.now;
    first.wDay = 1;
    FILETIME ft;
    SystemTimeToFileTime(&first, &ft);
    FileTimeToSystemTime(&ft, &first);
    const int offset = (MondayBased(first.wDayOfWeek) - s.firstDay + 7) % 7;
    const int days = DaysInMonth(s.now.wYear, s.now.wMonth);
    const int rows = (offset + days + 6) / 7;

    const float colW = (box.right - box.left) / 7;
    const float headerH = headerSize * 1.9f;
    const float rowH = std::min((box.bottom - box.top - headerH) / rows, numberSize * 3.2f);
    IDWriteTextFormat* header = c.Font(headerSize, kSemibold);
    IDWriteTextFormat* number = c.Font(numberSize, kMedium);
    IDWriteTextFormat* today = c.Font(numberSize, DWRITE_FONT_WEIGHT_BOLD);
    TextOptions center;
    center.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    center.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    for (int col = 0; col < 7; ++col) {
        int weekday = (s.firstDay + col) % 7;
        wchar_t name[32] = L"";
        GetLocaleInfoEx(s.locale.c_str(), LOCALE_SSHORTESTDAYNAME1 + weekday, name, ARRAYSIZE(name));
        float x = box.left + col * colW;
        c.Text(Upper(s, name), header, {x, box.top, x + colW, box.top + headerH}, weekday >= 5 ? p.tertiary : p.secondary,
               center);
    }
    for (int day = 1; day <= days; ++day) {
        int cell = offset + day - 1;
        int col = cell % 7, row = cell / 7;
        int weekday = (s.firstDay + col) % 7;
        float x = box.left + col * colW, y = box.top + headerH + row * rowH;
        D2D1_RECT_F r = {x, y, x + colW, y + rowH};
        if (day == s.now.wDay) {
            float rad = std::min(colW, rowH) * 0.46f;
            c.Dot((r.left + r.right) / 2, (r.top + r.bottom) / 2, rad, p.red);
            c.Text(std::to_wstring(day), today, r, Rgb(0xFFFFFF), center);
        } else {
            c.Text(std::to_wstring(day), number, r, weekday >= 5 ? p.secondary : p.text, center);
        }
    }
}

// Weekday in red, the day's number, month and year: the left side of the calendar.
void DayColumn(Ctx& c, float left, float right) {
    const Scene& s = c.s;
    const Palette& p = c.p;
    std::wstring weekday = Upper(s, DateText(s, s.now, L"dddd"));
    c.Text(weekday, c.Fit(weekday, 13, 10, right - left, kSemibold), {left, 14, right, 34}, p.red);
    c.Text(std::to_wstring(s.now.wDay), c.Font(66, kLight, true), {left - 3, 22, right, 112}, p.text);
    std::wstring month = Capitalized(s, DateText(s, s.now, c.T(L"host.widgets.date.monthYear").c_str()));
    c.Text(month, c.Fit(month, 14, 11, right - left, kSemibold), {left, c.H - 38, right, c.H - 16}, p.secondary);
}

void DrawCalendar(Ctx& c) {
    const Palette& p = c.p;
    FillCard(c, p.top, p.bottom);
    switch (c.s.size) {
        case Size::Small:
            DayColumn(c, kPad, c.W - kPad);
            break;
        case Size::Medium:
            DayColumn(c, kPad, c.H - 8);
            MonthGrid(c, {c.H - 6, 14, c.W - 12, c.H - 10}, 11.5f, 9.5f);
            break;
        case Size::Large: {
            const Scene& s = c.s;
            std::wstring month = Capitalized(s, DateText(s, s.now, L"MMMM"));
            IDWriteTextFormat* title = c.Font(19, kSemibold, true);
            float mw = TextWidth(c.g, month, title);
            c.Text(month, title, {kPad + 2, 16, c.W - kPad, 44}, p.red);
            c.Text(std::to_wstring(s.now.wYear), c.Font(19, kRegular, true), {kPad + 2 + mw + 7, 16, c.W - kPad, 44},
                   p.secondary);
            MonthGrid(c, {12, 52, c.W - 12, c.H - 14}, 15, 11);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------

// The sky behind the forecast: the color of the weather it shows, darker at night.
void SkyColors(const Forecast* f, D2D1_COLOR_F* top, D2D1_COLOR_F* bottom) {
    struct Pair {
        UINT32 top, bottom;
    };
    Pair day = {0x005AFF, 0x5D95FB}, night = {0x0B1B3F, 0x2C4374};
    if (f && f->valid) {
        switch (f->cond.icon) {
            case Icon::Sun: day = {0x005AFF, 0x5D95FB}; break;
            case Icon::Moon: night = {0x0B1B3F, 0x2C4374}; break;
            case Icon::SunCloud: day = {0x2F6FD8, 0x7AA3DA}; break;
            case Icon::MoonCloud: night = {0x1B2640, 0x3B4A69}; break;
            case Icon::Cloud: day = {0x587593, 0x8FA4BB}; night = {0x232B38, 0x464F5E}; break;
            case Icon::Fog: day = {0x6B7B89, 0xA0ADB8}; night = {0x2A3038, 0x4F5762}; break;
            case Icon::Drizzle:
            case Icon::Rain:
            case Icon::Sleet: day = {0x3F5268, 0x76879A}; night = {0x1C2431, 0x3B4555}; break;
            case Icon::Snow: day = {0x6A87A8, 0xA6B9CE}; night = {0x29384B, 0x526379}; break;
            case Icon::Storm: day = {0x2C3146, 0x545C79}; night = {0x181A26, 0x363A50}; break;
        }
    }
    bool isNight = f && f->valid && f->night;
    const Pair& use = isNight ? night : day;
    *top = Rgb(use.top);
    *bottom = Rgb(use.bottom);
}

ComPtr<ID2D1Geometry> CloudShape(ID2D1Factory* f, float x, float y, float w) {
    // A flat-bottomed cloud w wide and 0.62 w high: a capsule and three puffs.
    float h = w * 0.62f;
    ComPtr<ID2D1RoundedRectangleGeometry> base;
    ComPtr<ID2D1EllipseGeometry> left, top, right;
    f->CreateRoundedRectangleGeometry(D2D1::RoundedRect({x, y + h * 0.48f, x + w, y + h}, h * 0.26f, h * 0.26f), base.put());
    f->CreateEllipseGeometry(D2D1::Ellipse({x + w * 0.28f, y + h * 0.62f}, h * 0.32f, h * 0.32f), left.put());
    f->CreateEllipseGeometry(D2D1::Ellipse({x + w * 0.55f, y + h * 0.43f}, h * 0.43f, h * 0.43f), top.put());
    f->CreateEllipseGeometry(D2D1::Ellipse({x + w * 0.79f, y + h * 0.66f}, h * 0.30f, h * 0.30f), right.put());
    ID2D1Geometry* parts[] = {base.get(), left.get(), top.get(), right.get()};
    for (ID2D1Geometry* part : parts) {
        if (!part) return nullptr;
    }
    ComPtr<ID2D1GeometryGroup> group;
    f->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 4, group.put());
    ComPtr<ID2D1Geometry> out;
    if (group) group.As(&out);
    return out;
}

void Sun(Ctx& c, float cx, float cy, float r) {
    const D2D1_COLOR_F yellow = Rgb(0xFFD60A);
    c.Dot(cx, cy, r, yellow);
    for (int i = 0; i < 8; ++i) {
        float a = i * kPi / 4, dx = cosf(a), dy = sinf(a);
        c.Line(cx + dx * r * 1.45f, cy + dy * r * 1.45f, cx + dx * r * 1.9f, cy + dy * r * 1.9f, r * 0.3f, yellow);
    }
}

void Moon(Ctx& c, float cx, float cy, float r) {
    ComPtr<ID2D1Factory> f;
    c.rt->GetFactory(f.put());
    ComPtr<ID2D1EllipseGeometry> disc, bite;
    ComPtr<ID2D1PathGeometry> moon;
    ComPtr<ID2D1GeometrySink> sink;
    f->CreateEllipseGeometry(D2D1::Ellipse({cx, cy}, r, r), disc.put());
    f->CreateEllipseGeometry(D2D1::Ellipse({cx + r * 0.55f, cy - r * 0.38f}, r * 0.82f, r * 0.82f), bite.put());
    if (!disc || !bite || FAILED(f->CreatePathGeometry(moon.put())) || FAILED(moon->Open(sink.put()))) return;
    disc->CombineWithGeometry(bite.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get());
    sink->Close();
    c.rt->FillGeometry(moon.get(), c.Brush(Rgb(0xF4F1E6)).get());
}

void Cloud(Ctx& c, float x, float y, float w, const D2D1_COLOR_F& color) {
    ComPtr<ID2D1Factory> f;
    c.rt->GetFactory(f.put());
    ComPtr<ID2D1Geometry> cloud = CloudShape(f.get(), x, y, w);
    if (cloud) c.rt->FillGeometry(cloud.get(), c.Brush(color).get());
}

void Drops(Ctx& c, int count, float y0, float length, float width) {
    const D2D1_COLOR_F blue = Rgb(0x8AD4FF);
    for (int i = 0; i < count; ++i) {
        float x = 0.30f + i * (0.44f / std::max(1, count - 1));
        float y = y0 + (i % 2) * 0.06f;
        c.Line(x + length * 0.25f, y, x, y + length, width, blue);
    }
}

void Flakes(Ctx& c, int count, float y0) {
    for (int i = 0; i < count; ++i) {
        float x = 0.30f + i * (0.40f / std::max(1, count - 1));
        float y = y0 + (i % 2) * 0.1f;
        c.Dot(x, y, 0.045f, Rgb(0xFFFFFF));
    }
}

// The icon fills a unit square; the caller has set it up with Box().
void DrawIcon(Ctx& c, Icon icon) {
    const D2D1_COLOR_F white = Rgb(0xFFFFFF), soft = Rgb(0xF2F4F7);
    switch (icon) {
        case Icon::Sun:
            Sun(c, 0.5f, 0.5f, 0.2f);
            break;
        case Icon::Moon:
            Moon(c, 0.5f, 0.5f, 0.32f);
            break;
        case Icon::SunCloud:
            Sun(c, 0.36f, 0.36f, 0.15f);
            Cloud(c, 0.16f, 0.36f, 0.82f, white);
            break;
        case Icon::MoonCloud:
            Moon(c, 0.38f, 0.34f, 0.22f);
            Cloud(c, 0.16f, 0.38f, 0.82f, soft);
            break;
        case Icon::Cloud:
            Cloud(c, 0.04f, 0.22f, 0.92f, white);
            break;
        case Icon::Fog:
            Cloud(c, 0.08f, 0.1f, 0.84f, white);
            c.Line(0.14f, 0.74f, 0.86f, 0.74f, 0.07f, Rgb(0xFFFFFF, 0.85f));
            c.Line(0.26f, 0.88f, 0.74f, 0.88f, 0.07f, Rgb(0xFFFFFF, 0.85f));
            break;
        case Icon::Drizzle:
            Cloud(c, 0.08f, 0.06f, 0.84f, white);
            Drops(c, 3, 0.68f, 0.13f, 0.06f);
            break;
        case Icon::Rain:
            Cloud(c, 0.08f, 0.06f, 0.84f, white);
            Drops(c, 3, 0.66f, 0.22f, 0.07f);
            break;
        case Icon::Sleet:
            Cloud(c, 0.08f, 0.06f, 0.84f, white);
            Drops(c, 2, 0.66f, 0.2f, 0.07f);
            Flakes(c, 3, 0.76f);
            break;
        case Icon::Snow:
            Cloud(c, 0.08f, 0.06f, 0.84f, white);
            Flakes(c, 4, 0.72f);
            break;
        case Icon::Storm: {
            Cloud(c, 0.08f, 0.04f, 0.84f, soft);
            ComPtr<ID2D1Factory> f;
            c.rt->GetFactory(f.put());
            ComPtr<ID2D1PathGeometry> bolt;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(f->CreatePathGeometry(bolt.put())) && SUCCEEDED(bolt->Open(sink.put()))) {
                D2D1_POINT_2F pts[] = {{0.56f, 0.54f}, {0.38f, 0.78f}, {0.5f, 0.78f}, {0.42f, 0.98f}, {0.66f, 0.7f},
                                       {0.53f, 0.7f}, {0.62f, 0.54f}};
                sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLines(pts + 1, ARRAYSIZE(pts) - 1);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                c.rt->FillGeometry(bolt.get(), c.Brush(Rgb(0xFFD60A)).get());
            }
            break;
        }
    }
}

void WeatherIcon(Ctx& c, Icon icon, float x, float y, float size) {
    c.Box(x, y, size);
    DrawIcon(c, icon);
    c.Mirrored();
}

std::wstring DayName(Ctx& c, const DayForecast& d, size_t index) {
    if (index == 0) return c.T(L"host.widgets.weather.today");
    return Capitalized(c.s, DateText(c.s, d.date, L"ddd"));
}

// Coldest blue to hottest orange, for the bars of the daily forecast.
D2D1_COLOR_F TempColor(double t, bool fahrenheit) {
    double celsius = fahrenheit ? (t - 32) * 5 / 9 : t;
    float k = (float)std::clamp((celsius + 5) / 35, 0.0, 1.0);
    D2D1_COLOR_F cold = Rgb(0x64D2FF), mild = Rgb(0xA5E37A), warm = Rgb(0xFFD60A), hot = Rgb(0xFF9F0A);
    if (k < 0.33f) return Mix(cold, mild, k / 0.33f);
    if (k < 0.66f) return Mix(mild, warm, (k - 0.33f) / 0.33f);
    return Mix(warm, hot, (k - 0.66f) / 0.34f);
}

void DrawWeather(Ctx& c) {
    const Scene& s = c.s;
    const Forecast* f = s.weather == WeatherState::Ready || s.weather == WeatherState::Offline ? s.forecast : nullptr;
    if (f && !f->valid) f = nullptr;
    D2D1_COLOR_F top, bottom;
    SkyColors(f, &top, &bottom);
    FillCard(c, top, bottom);
    const D2D1_COLOR_F white = Rgb(0xFFFFFF), dim = Rgb(0xFFFFFF, 0.78f);
    const float right = c.W - kPad;

    if (s.weather == WeatherState::NoPlace || s.weather == WeatherState::LocationOff) {
        WeatherIcon(c, Icon::SunCloud, kPad - 2, 14, 40);
        TextOptions wrap;
        wrap.wrap = true;
        wrap.valign = DWRITE_PARAGRAPH_ALIGNMENT_FAR;
        const wchar_t* key = s.weather == WeatherState::NoPlace ? L"host.widgets.weather.noPlace"
                                                                : L"host.widgets.weather.locationOff";
        const std::wstring& text = c.T(key);
        // The longer advice has to fit a small card too.
        IDWriteTextFormat* font = c.Font(text.size() > 60 ? 12.5f : 14, kSemibold);
        c.Text(text, font, {kPad, 60, right, c.H - 14}, white, wrap);
        return;
    }

    // City and temperature (left), condition and today's range: the top of every size.
    const bool small = s.size == Size::Small;
    const float cityRight = small ? right : c.W / 2 + 40;
    c.Text(s.place, c.Fit(s.place, 15, 12, cityRight - kPad, kSemibold), {kPad, 13, cityRight, 33}, white);
    c.Text(f ? Degrees(f->temp) : L"—", c.Font(44, kLight, true), {kPad - 2, 28, c.W / 2 + 40, 90}, white);
    std::wstring status = f ? c.T(SkyKey(f->cond.sky))
                            : c.T(s.weather == WeatherState::Loading ? L"host.widgets.weather.loading"
                                                                     : L"host.widgets.weather.offline");
    std::wstring range = f ? Fill(c.T(L"host.widgets.weather.range"), Degrees(f->high), Degrees(f->low)) : L"";
    if (small) {
        if (f) WeatherIcon(c, f->cond.icon, kPad - 1, 94, 24);
        c.Text(status, c.Fit(status, 13, 10.5f, right - kPad, kSemibold), {kPad, 118, right, 136}, white);
        c.Text(range, c.Fit(range, 13, 10.5f, right - kPad, kSemibold), {kPad, 135, right, 153}, white);
        return;
    }
    TextOptions end;
    end.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    if (f) WeatherIcon(c, f->cond.icon, right - 24, 15, 24);
    c.Text(status, c.Fit(status, 13, 10.5f, right - c.W / 2, kSemibold), {c.W / 2, 42, right, 60}, white, end);
    c.Text(range, c.Fit(range, 13, 10.5f, right - c.W / 2, kSemibold), {c.W / 2, 59, right, 77}, white, end);
    if (!f) return;

    // The coming hours.
    const size_t hours = std::min<size_t>(6, f->hours.size());
    const float colW = (c.W - 2 * kPad + 8) / 6, rowTop = 94;
    TextOptions center;
    center.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    for (size_t i = 0; i < hours; ++i) {
        const HourForecast& h = f->hours[i];
        float x = kPad - 4 + i * colW;
        std::wstring label = i == 0 ? c.T(L"host.widgets.weather.now") : HourText(s, h.hour);
        c.Text(label, c.Font(11.5f, kSemibold), {x, rowTop, x + colW, rowTop + 16}, dim, center);
        WeatherIcon(c, h.cond.icon, x + colW / 2 - 11, rowTop + 18, 22);
        c.Text(Degrees(h.temp), c.Font(13.5f, kSemibold), {x + 2, rowTop + 41, x + colW + 2, rowTop + 59}, white,
               center);
    }
    if (s.size != Size::Large) return;

    // The coming days, each with its range on a bar shared by the whole week.
    c.Line(kPad, 168, c.W - kPad, 168, 0.75f, Rgb(0xFFFFFF, 0.28f));
    const size_t days = std::min<size_t>(5, f->days.size());
    double lo = f->days[0].low, hi = f->days[0].high;
    for (size_t i = 0; i < days; ++i) {
        lo = std::min(lo, f->days[i].low);
        hi = std::max(hi, f->days[i].high);
    }
    const float rowH = (c.H - 178 - 10) / 5.0f;
    const float barLeft = 168, barRight = c.W - kPad - 44;
    for (size_t i = 0; i < days; ++i) {
        const DayForecast& d = f->days[i];
        float y = 178 + i * rowH;
        std::wstring day = DayName(c, d, i);
        c.Text(day, c.Fit(day, 14, 11, 92 - kPad, kSemibold), {kPad, y, 92, y + rowH}, white,
               {DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER});
        WeatherIcon(c, d.cond.icon, 96, y + rowH / 2 - 11, 22);
        c.Text(Degrees(d.low), c.Font(14, kSemibold), {120, y, barLeft - 8, y + rowH}, dim,
               {DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER});
        c.Text(Degrees(d.high), c.Font(14, kSemibold), {barRight + 8, y, c.W - kPad, y + rowH}, white,
               {DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER});
        float by = y + rowH / 2;
        c.Line(barLeft, by, barRight, by, 4.5f, Rgb(0x000000, 0.18f));
        double span = std::max(1.0, hi - lo);
        float x0 = barLeft + (float)((d.low - lo) / span) * (barRight - barLeft);
        float x1 = barLeft + (float)((d.high - lo) / span) * (barRight - barLeft);
        D2D1_GRADIENT_STOP stops[] = {{0, TempColor(d.low, s.fahrenheit)}, {1, TempColor(d.high, s.fahrenheit)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (SUCCEEDED(c.rt->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED(c.rt->CreateLinearGradientBrush({{x0, by}, {std::max(x1, x0 + 1), by}}, collection.get(),
                                                      brush.put()))) {
            c.rt->DrawLine({x0, by}, {std::max(x1, x0 + 0.01f), by}, brush.get(), 4.5f, c.Round());
        }
        if (i == 0) {
            float xn = barLeft + (float)((std::clamp(f->temp, lo, hi) - lo) / span) * (barRight - barLeft);
            c.Dot(xn, by, 3.6f, Rgb(0x000000, 0.25f));
            c.Dot(xn, by, 2.6f, white);
        }
    }
}

// ---------------------------------------------------------------------------
// System gauges
// ---------------------------------------------------------------------------

void Ring(Ctx& c, float cx, float cy, float r, float width, float fraction, const D2D1_COLOR_F& color) {
    c.Unmirrored();
    cx = c.X(cx);
    c.rt->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), c.Brush(c.p.track).get(), width);
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (fraction >= 0.999f) {
        c.rt->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), c.Brush(color).get(), width);
    } else if (fraction > 0.004f) {
        ComPtr<ID2D1Factory> f;
        c.rt->GetFactory(f.put());
        ComPtr<ID2D1PathGeometry> arc;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(f->CreatePathGeometry(arc.put())) && SUCCEEDED(arc->Open(sink.put()))) {
            float a = fraction * 2 * kPi;
            sink->BeginFigure({cx, cy - r}, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc({{cx + r * sinf(a), cy - r * cosf(a)}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                          fraction > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            c.rt->DrawGeometry(arc.get(), c.Brush(color).get(), width, c.Round());
        }
    }
    c.Mirrored();
}

struct Gauge {
    const wchar_t* label;
    int value;
    UINT32 color;
};

void DrawSystem(Ctx& c) {
    const Palette& p = c.p;
    const SystemStats& st = c.s.stats;
    FillCard(c, p.top, p.bottom);
    std::vector<Gauge> gauges = {{L"host.widgets.system.cpu", st.cpu, 0x0A84FF},
                                 {L"host.widgets.system.memory", st.memory, 0xBF5AF2},
                                 {L"host.widgets.system.disk", st.disk, 0xFF9F0A}};
    if (st.battery >= 0) {
        UINT32 color = st.battery <= 10 ? 0xFF453A : st.battery <= 20 ? 0xFF9F0A : 0x30D158;
        gauges.push_back({st.charging ? L"host.widgets.system.charging" : L"host.widgets.system.battery", st.battery,
                          color});
    }
    TextOptions center;
    center.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    center.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    const bool small = c.s.size == Size::Small;
    const int n = (int)gauges.size();
    for (int i = 0; i < n; ++i) {
        float cx, cy, r, width;
        if (small) {
            // Two rows of two (a third one alone is centered).
            int row = i / 2, col = i % 2;
            bool alone = n == 3 && i == 2;
            cx = alone ? c.W / 2 : (col == 0 ? c.W * 0.29f : c.W * 0.71f);
            cy = row == 0 ? 44 : 110;
            r = 21;
            width = 5;
        } else {
            float colW = (c.W - 2 * kPad) / n;
            cx = kPad + colW * (i + 0.5f);
            cy = 70;
            r = 31;
            width = 7;
        }
        Ring(c, cx, cy, r, width, gauges[i].value / 100.0f, Rgb(gauges[i].color));
        c.Text(Percent(gauges[i].value), c.Font(small ? 12.5f : 15, kSemibold, true),
               {cx - r, cy - r, cx + r, cy + r}, p.text, center);
        float labelTop = cy + r + (small ? 3 : 9);
        const std::wstring& label = c.T(gauges[i].label);
        const float half = small ? 36 : 42;
        c.Text(label, c.Fit(label, small ? 10.5f : 12, 8.5f, 2 * half, kSemibold),
               {cx - half, labelTop, cx + half, labelTop + 16}, p.secondary, center);
    }
}

// ---------------------------------------------------------------------------
// Notes and Photo
// ---------------------------------------------------------------------------

void NoteGlyph(Ctx& c, float x, float y) {
    c.rt->FillRoundedRectangle(D2D1::RoundedRect({x, y, x + 22, y + 22}, 6, 6), c.Brush(Rgb(0xFFD60A)).get());
    for (int i = 0; i < 3; ++i) {
        float ly = y + 7.5f + i * 4;
        c.Line(x + 6, ly, x + (i == 2 ? 12 : 16), ly, 1.6f, Rgb(0xFFFFFF));
    }
}

void DrawNotes(Ctx& c) {
    const Palette& p = c.p;
    FillCard(c, p.top, p.bottom);
    NoteGlyph(c, kPad, 15);
    D2D1_RECT_F box = {kPad, 47, c.W - kPad, c.H - 14};
    std::wstring text = c.s.text;
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    if (text.empty()) {
        TextOptions wrap;
        wrap.wrap = true;
        c.Text(c.T(L"host.widgets.notes.empty"), c.Font(13, kRegular), box, p.secondary, wrap);
        return;
    }
    // The first line is the title, like a note in Notes.
    std::wstring clean;
    for (wchar_t ch : text) {
        if (ch != L'\r') clean.push_back(ch);
    }
    size_t firstBreak = clean.find(L'\n');
    UINT32 titleLen = (UINT32)(firstBreak == std::wstring::npos ? clean.size() : firstBreak);

    if (c.s.rtl) box = {c.W - box.right, box.top, c.W - box.left, box.bottom};
    c.Unmirrored();
    IDWriteTextFormat* body = c.Font(13, kRegular);
    const float w = box.right - box.left, h = box.bottom - box.top;
    ComPtr<IDWriteTextLayout> layout;
    // Cut at the last line that fits, with an ellipsis.
    for (int attempt = 0; attempt < 2; ++attempt) {
        layout.reset();
        if (FAILED(c.g.DWrite()->CreateTextLayout(clean.c_str(), (UINT32)clean.size(), body, w, h, layout.put()))) {
            return;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        if (c.s.rtl) layout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
        layout->SetFontSize(15, {0, titleLen});
        layout->SetFontWeight(kSemibold, {0, titleLen});
        layout->SetDrawingEffect(c.Brush(p.text).get(), {0, titleLen});
        DWRITE_TEXT_METRICS m = {};
        layout->GetMetrics(&m);
        if (attempt == 1 || m.height <= h + 0.5f) break;
        std::vector<DWRITE_LINE_METRICS> lines(m.lineCount);
        UINT32 count = 0;
        layout->GetLineMetrics(lines.data(), (UINT32)lines.size(), &count);
        float used = 0;
        UINT32 chars = 0;
        for (UINT32 i = 0; i < count && used + lines[i].height <= h; ++i) {
            used += lines[i].height;
            chars += lines[i].length;
        }
        while (chars > 0 && (clean[chars - 1] == L'\n' || clean[chars - 1] == L' ')) --chars;
        if (chars > 2) chars -= 2;   // room for the ellipsis
        clean = clean.substr(0, chars) + L"…";
        titleLen = std::min(titleLen, (UINT32)clean.size());
    }
    c.rt->DrawTextLayout({box.left, box.top}, layout.get(), c.Brush(p.secondary).get(),
                         D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    c.Mirrored();
}

void DrawPhoto(Ctx& c) {
    ComPtr<ID2D1Bitmap> bitmap;
    if (c.s.photo) c.rt->CreateBitmapFromWicBitmap(c.s.photo, nullptr, bitmap.put());
    if (!bitmap) {
        FillCard(c, Rgb(c.s.dark ? 0x3A3F55 : 0xB9C7E8), Rgb(c.s.dark ? 0x23263A : 0x8FA4D6));
        // A landscape in a frame: the "choose a picture" placeholder.
        const D2D1_COLOR_F white = Rgb(0xFFFFFF);
        c.Box(kPad, 16, 30);
        c.rt->DrawRoundedRectangle(D2D1::RoundedRect({0.05f, 0.15f, 0.95f, 0.85f}, 0.14f, 0.14f), c.Brush(white).get(),
                                   0.08f);
        c.Dot(0.33f, 0.39f, 0.08f, white);
        ComPtr<ID2D1Factory> f;
        c.rt->GetFactory(f.put());
        ComPtr<ID2D1PathGeometry> hills;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(f->CreatePathGeometry(hills.put())) && SUCCEEDED(hills->Open(sink.put()))) {
            D2D1_POINT_2F pts[] = {{0.38f, 0.62f}, {0.52f, 0.48f}, {0.88f, 0.78f}, {0.12f, 0.78f}, {0.3f, 0.6f}};
            sink->BeginFigure({0.12f, 0.78f}, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(pts, ARRAYSIZE(pts));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            c.rt->FillGeometry(hills.get(), c.Brush(white).get());
        }
        c.Mirrored();
        TextOptions wrap;
        wrap.wrap = true;
        wrap.valign = DWRITE_PARAGRAPH_ALIGNMENT_FAR;
        c.Text(c.T(L"host.widgets.photo.empty"), c.Font(14, kSemibold), {kPad, 56, c.W - kPad, c.H - 16}, white, wrap);
        return;
    }
    // The picture fills the card; it is never mirrored.
    c.Unmirrored();
    ComPtr<ID2D1PathGeometry> shape = CardShape(c);
    ComPtr<ID2D1Layer> layer;
    if (shape && SUCCEEDED(c.rt->CreateLayer(nullptr, layer.put()))) {
        c.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.get()), layer.get());
        c.rt->DrawBitmap(bitmap.get(), {0, 0, c.W, c.H}, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        c.rt->PopLayer();
    }
    c.Mirrored();
}

}  // namespace

void DrawCard(Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float unit, int widthPx, int heightPx) {
    Ctx c(g, rt, s, unit, widthPx / unit, heightPx / unit);
    D2D1_COLOR_F edge = c.p.hairline;
    switch (s.kind) {
        case Kind::Clock: DrawClock(c); break;
        case Kind::Calendar: DrawCalendar(c); break;
        case Kind::Weather:
            DrawWeather(c);
            edge = Rgb(0xFFFFFF, 0.14f);
            break;
        case Kind::System: DrawSystem(c); break;
        case Kind::Notes: DrawNotes(c); break;
        case Kind::Photo:
            DrawPhoto(c);
            edge = Rgb(0x000000, 0.12f);
            break;
        case Kind::Count: break;
    }
    c.Mirrored();
    if (s.pass != Pass::Hands) CardEdge(c, edge, unit);
}

}  // namespace widgets
