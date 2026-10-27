// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_snapshot.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include "host/i18n.h"
#include "host/widgets/canvas.h"
#include "host/widgets/widget_draw.h"
#include "host/widgets/widget_layout.h"

namespace widgets {

namespace {

const float kUnit = 1.5f;   // a 150 % display

struct Sample {
    Kind kind;
    Size size;
    Icon icon = Icon::Sun;
    bool night = false;
    WeatherState weather = WeatherState::Ready;
};

Forecast SampleForecast(Icon icon, bool night) {
    Forecast f;
    f.valid = true;
    f.temp = 22;
    f.low = 18;
    f.high = 25;
    f.night = night;
    const struct {
        Icon icon;
        Sky day, night;
    } skies[] = {{Icon::Sun, Sky::Sunny, Sky::Clear},       {Icon::Moon, Sky::Sunny, Sky::Clear},
                 {Icon::SunCloud, Sky::PartlyCloudy, Sky::PartlyCloudy},
                 {Icon::MoonCloud, Sky::PartlyCloudy, Sky::PartlyCloudy},
                 {Icon::Cloud, Sky::Cloudy, Sky::Cloudy},   {Icon::Fog, Sky::Fog, Sky::Fog},
                 {Icon::Drizzle, Sky::Drizzle, Sky::Drizzle}, {Icon::Rain, Sky::Rain, Sky::Rain},
                 {Icon::Sleet, Sky::FreezingRain, Sky::FreezingRain}, {Icon::Snow, Sky::Snow, Sky::Snow},
                 {Icon::Storm, Sky::Thunderstorm, Sky::Thunderstorm}};
    for (const auto& s : skies) {
        if (s.icon == icon) f.cond = {night ? s.night : s.day, icon};
    }
    const Icon hours[] = {icon, Icon::Sun, Icon::SunCloud, Icon::Cloud, Icon::Rain, Icon::Moon};
    const double temps[] = {22, 23, 24, 21, 19, 17};
    for (int i = 0; i < 6; ++i) f.hours.push_back({(14 + i) % 24, temps[i], {Sky::Sunny, hours[i]}});
    const Icon days[] = {icon, Icon::Rain, Icon::Cloud, Icon::SunCloud, Icon::Sun, Icon::Storm};
    const double lows[] = {18, 14, 11, 9, 12, 15}, highs[] = {25, 19, 16, 17, 21, 23};
    SYSTEMTIME now;
    GetLocalTime(&now);
    FILETIME ft;
    SystemTimeToFileTime(&now, &ft);
    for (int i = 0; i < 6; ++i) {
        DayForecast d;
        ULARGE_INTEGER t = {{ft.dwLowDateTime, ft.dwHighDateTime}};
        t.QuadPart += (ULONGLONG)i * 24 * 3600 * 10000000ULL;
        FILETIME day = {t.LowPart, t.HighPart};
        FileTimeToSystemTime(&day, &d.date);
        d.low = lows[i];
        d.high = highs[i];
        d.cond = {Sky::Sunny, days[i]};
        f.days.push_back(d);
    }
    return f;
}

// src over dst, both premultiplied BGRA.
void Blend(BYTE* dst, int dstW, int dstH, const Surface& src, int x, int y) {
    for (int sy = 0; sy < src.Height(); ++sy) {
        int dy = y + sy;
        if (dy < 0 || dy >= dstH) continue;
        for (int sx = 0; sx < src.Width(); ++sx) {
            int dx = x + sx;
            if (dx < 0 || dx >= dstW) continue;
            const BYTE* s = src.Bits() + ((size_t)sy * src.Width() + sx) * 4;
            BYTE* d = dst + ((size_t)dy * dstW + dx) * 4;
            int inv = 255 - s[3];
            for (int k = 0; k < 4; ++k) d[k] = (BYTE)(s[k] + (d[k] * inv + 127) / 255);
        }
    }
}

bool SavePng(Graphics& g, const BYTE* bits, int w, int h, const std::wstring& path) {
    IWICImagingFactory* wic = g.Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(w, h))) {
        return false;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;   // opaque here, so the same as premultiplied
    return SUCCEEDED(frame->SetPixelFormat(&format)) &&
           SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(bits))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

}  // namespace

int WidgetSnapshot(const std::wstring& dir, const std::wstring& language, bool dark) {
    i18n::Init(language.empty() ? L"auto" : language);
    CreateDirectoryW(dir.c_str(), nullptr);
    Graphics g;
    if (!g.Init(i18n::LocaleName())) return 1;
    Texts texts = i18n::All(L"host.widgets.");

    std::vector<Sample> samples = {
        {Kind::Clock, Size::Small},       {Kind::Clock, Size::Medium},      {Kind::Calendar, Size::Small},
        {Kind::Calendar, Size::Medium},   {Kind::Weather, Size::Small},     {Kind::Weather, Size::Medium},
        {Kind::System, Size::Small},      {Kind::System, Size::Medium},     {Kind::Calendar, Size::Large},
        {Kind::Weather, Size::Large},     {Kind::Notes, Size::Large},       {Kind::Notes, Size::Small},
        {Kind::Photo, Size::Small},       {Kind::Notes, Size::Medium},      {Kind::Photo, Size::Medium},
        {Kind::Countdown, Size::Small},   {Kind::Countdown, Size::Medium},  {Kind::NowPlaying, Size::Small},
        {Kind::NowPlaying, Size::Medium},
    };
    const Icon icons[] = {Icon::Moon, Icon::SunCloud, Icon::MoonCloud, Icon::Cloud, Icon::Fog, Icon::Drizzle,
                          Icon::Rain, Icon::Sleet, Icon::Snow, Icon::Storm};
    for (Icon icon : icons) {
        Sample s{Kind::Weather, Size::Small, icon, icon == Icon::Moon || icon == Icon::MoonCloud};
        samples.push_back(s);
    }
    samples.push_back({Kind::Weather, Size::Small, Icon::Sun, false, WeatherState::NoPlace});
    samples.push_back({Kind::Weather, Size::Small, Icon::Sun, false, WeatherState::LocationOff});
    samples.push_back({Kind::Weather, Size::Small, Icon::Sun, false, WeatherState::Loading});

    // Flow layout: six cells per row; a large widget takes two rows.
    RECT work = {0, 0, 100000, 100000};
    Grid grid(work, kUnit);
    const int cols = 6, cell = grid.CellPx(), gap = (int)lroundf(kGapDip * kUnit), margin = (int)lroundf(48 * kUnit);
    std::vector<std::vector<bool>> used;
    struct Placed {
        Sample s;
        int x, y;
        SIZE size;
    };
    std::vector<Placed> placed;
    for (const Sample& s : samples) {
        Span span = SpanOf(s.size);
        span.cols /= kColSteps;   // whole cells here
        span.rows /= kRowSteps;
        for (int row = 0;; ++row) {
            while ((int)used.size() < row + span.rows) used.push_back(std::vector<bool>(cols, false));
            int found = -1;
            for (int col = 0; col + span.cols <= cols && found < 0; ++col) {
                bool free = true;
                for (int r = row; r < row + span.rows; ++r) {
                    for (int c = col; c < col + span.cols; ++c) free = free && !used[r][c];
                }
                if (free) found = col;
            }
            if (found < 0) continue;
            for (int r = row; r < row + span.rows; ++r) {
                for (int c = found; c < found + span.cols; ++c) used[r][c] = true;
            }
            placed.push_back({s, margin + found * (cell + gap), margin + row * (cell + gap), grid.CardSize(SpanOf(s.size))});
            break;
        }
    }
    const int width = 2 * margin + cols * cell + (cols - 1) * gap;
    const int height = 2 * margin + (int)used.size() * (cell + gap) - gap;

    // A wallpaper-like backdrop.
    Surface backdrop;
    if (!backdrop.Resize(g, width, height)) return 1;
    if (ID2D1RenderTarget* rt = backdrop.Begin()) {
        D2D1_GRADIENT_STOP stops[] = {{0, Rgb(dark ? 0x1D2440 : 0x4B79C8)}, {0.55f, Rgb(dark ? 0x3A2650 : 0x8E7CC3)},
                                      {1, Rgb(dark ? 0x5A2E3E : 0xE7A6A1)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> brush;
        rt->CreateGradientStopCollection(stops, 3, collection.put());
        rt->CreateLinearGradientBrush({{0, 0}, {(float)width, (float)height}}, collection.get(), brush.put());
        rt->FillRectangle({0, 0, (float)width, (float)height}, brush.get());
        backdrop.End();
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    now.wHour = 10;
    now.wMinute = 9;
    now.wSecond = 32;
    MediaInfo media;
    media.present = media.playing = true;
    media.title = L"Golden Hour on the Coast Road";
    media.artist = L"The Lanterns";
    media.app = L"Spotify";
    wchar_t timeFormat[80] = L"";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, timeFormat, ARRAYSIZE(timeFormat));
    for (const Placed& p : placed) {
        const int reach = (int)lroundf(kShadowReach * kUnit);
        Surface shadow, card;
        if (!shadow.Resize(g, p.size.cx + 2 * reach, p.size.cy + 2 * reach) || !card.Resize(g, p.size.cx, p.size.cy)) {
            return 1;
        }
        DrawShadow(g, shadow, {(float)reach, (float)reach, (float)(reach + p.size.cx), (float)(reach + p.size.cy)},
                   kCardRadius * kUnit, kCardSmoothing, kUnit);
        Forecast forecast = SampleForecast(p.s.icon, p.s.night);
        Scene scene;
        scene.kind = p.s.kind;
        scene.size = p.s.size;
        scene.dark = dark;
        scene.rtl = i18n::IsRightToLeft();
        scene.texts = &texts;
        scene.locale = i18n::LocaleName();
        scene.now = now;
        scene.clock24 = wcschr(timeFormat, L'H') != nullptr;
        scene.firstDay = 0;
        scene.weather = p.s.weather;
        scene.forecast = &forecast;
        scene.place = L"San Francisco";   // sample data only: users pick their own place
        scene.stats = {37, 62, 48, 81, true};
        if (p.s.kind == Kind::Notes && p.s.size != Size::Small) {
            scene.text = L"Shopping\nMilk, bread, coffee beans\nCall the dentist on Monday\nPick up the parcel\n"
                         L"Book train tickets for the weekend trip to the mountains\nWater the plants";
        }
        if (p.s.kind == Kind::Countdown) {
            // Twelve days from the sample's today.
            FILETIME ft;
            SystemTimeToFileTime(&now, &ft);
            ULARGE_INTEGER t = {{ft.dwLowDateTime, ft.dwHighDateTime}};
            t.QuadPart += 12 * 86400ULL * 10000000ULL;
            SYSTEMTIME day;
            FileTimeToSystemTime((FILETIME*)&t, &day);
            wchar_t date[16];
            swprintf(date, ARRAYSIZE(date), L"%04u-%02u-%02u", day.wYear, day.wMonth, day.wDay);
            scene.date = date;
            scene.text = L"Trip to Lisbon";
        }
        if (p.s.kind == Kind::NowPlaying) scene.media = &media;
        if (ID2D1RenderTarget* rt = card.Begin()) {
            DrawCard(g, rt, scene, kUnit, p.size.cx, p.size.cy);
            card.End();
        }
        Blend(backdrop.Bits(), width, height, shadow, p.x - reach, p.y - reach);
        Blend(backdrop.Bits(), width, height, card, p.x, p.y);
    }
    std::wstring path = dir + L"\\widgets.png";
    return SavePng(g, backdrop.Bits(), width, height, path) ? 0 : 1;
}

}  // namespace widgets
