// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

// Unit tests for the pure parts of MacWin (no framework: CTest runs this exe, a
// non-zero exit code is a failure). Build: cmake --build --preset x64; run: ctest --test-dir build/x64

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "common/strutil.h"
#include "host/i18n.h"
#include "host/render/genie_effect.h"
#include "host/json.h"
#include "host/updater.h"
#include "host/widgets/location.h"
#include "host/widgets/media.h"
#include "host/widgets/weather.h"
#include "host/widgets/widget_layout.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

void TestVersions() {
    CHECK(CompareVersions(L"1.0.0", L"1.0.0") == 0);
    CHECK(CompareVersions(L"v1.0.0", L"1.0.0") == 0);
    CHECK(CompareVersions(L"1.0.1", L"1.0.0") > 0);
    CHECK(CompareVersions(L"1.10.0", L"1.9.9") > 0);
    CHECK(CompareVersions(L"2.0", L"1.99.99") > 0);
    CHECK(CompareVersions(L"1.2.0-beta", L"1.2.0") < 0);
    CHECK(CompareVersions(L"1.2.0", L"1.2.0-rc.1") > 0);
    CHECK(CompareVersions(L"0.9.9", L"1.0.0") < 0);
}

const wchar_t* kRelease = LR"({
  "tag_name": "v1.2.0", "html_url": "https://github.com/IMoggsYou/MacWin/releases/tag/v1.2.0",
  "draft": false, "prerelease": false,
  "body": "### Added\r\n\r\n- Scale\r\n\r\n\r\n---\r\n**Download:** `MacWinSetup-1.2.0.exe`",
  "assets": [
    {"name": "MacWinSetup-1.2.0.exe",
     "browser_download_url": "https://github.com/IMoggsYou/MacWin/releases/download/v1.2.0/MacWinSetup-1.2.0.exe"},
    {"name": "SHA256SUMS.txt",
     "browser_download_url": "https://github.com/IMoggsYou/MacWin/releases/download/v1.2.0/SHA256SUMS.txt"}
  ]
})";

void TestRelease() {
    Release r;
    CHECK(ParseLatestRelease(kRelease, &r));
    CHECK(r.version == L"1.2.0");
    CHECK(r.installerName == L"MacWinSetup-1.2.0.exe");
    CHECK(r.notesUrl == L"https://github.com/IMoggsYou/MacWin/releases/tag/v1.2.0");
    CHECK(r.sumsUrl.find(L"SHA256SUMS.txt") != std::wstring::npos);
    CHECK(r.notes == L"### Added\r\n\r\n- Scale");   // without the download footer

    std::wstring draft = kRelease;
    draft.replace(draft.find(L"\"draft\": false"), 14, L"\"draft\": true ");
    CHECK(!ParseLatestRelease(draft, &r));

    // Assets from anywhere else are never used.
    std::wstring foreign = kRelease;
    size_t at = foreign.find(L"https://github.com/IMoggsYou/MacWin/releases/download/v1.2.0/MacWinSetup");
    foreign.replace(at, 19, L"https://evil.example");
    CHECK(!ParseLatestRelease(foreign, &r));

    // After the rename GitHub links the files under the new name; another repository of
    // the same owner is still refused.
    auto rename = [](std::wstring text, const std::wstring& to) {
        for (size_t p; (p = text.find(L"IMoggsYou/MacWin/releases/download")) != std::wstring::npos;)
            text.replace(p + 10, 6, to);
        return text;
    };
    CHECK(ParseLatestRelease(rename(kRelease, L"Lampwin"), &r));
    CHECK(r.installerUrl == L"https://github.com/IMoggsYou/Lampwin/releases/download/v1.2.0/MacWinSetup-1.2.0.exe");
    CHECK(!ParseLatestRelease(rename(kRelease, L"Other"), &r));

    std::wstring badName = kRelease;
    badName.replace(badName.find(L"\"MacWinSetup-1.2.0.exe\""), 23, L"\"..\\\\MacWinSetup.exe\"");
    CHECK(!ParseLatestRelease(badName, &r));

    CHECK(!ParseLatestRelease(L"{\"message\": \"Not Found\"}", &r));
    CHECK(!ParseLatestRelease(L"not json", &r));
}

void TestSums() {
    std::string sums =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  MacWinSetup-1.2.0.exe\r\n"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff *Other.zip\n";
    CHECK(FindSha256(sums, L"MacWinSetup-1.2.0.exe") == L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(FindSha256(sums, L"other.zip") == L"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    CHECK(FindSha256(sums, L"Missing.exe").empty());
    CHECK(FindSha256("zz  MacWinSetup-1.2.0.exe\n", L"MacWinSetup-1.2.0.exe").empty());
}

void TestHosts() {
    CHECK(IsTrustedDownloadHost(L"github.com"));
    CHECK(IsTrustedDownloadHost(L"api.github.com"));
    CHECK(IsTrustedDownloadHost(L"objects.githubusercontent.com"));
    CHECK(IsTrustedDownloadHost(L"release-assets.githubusercontent.com"));
    CHECK(!IsTrustedDownloadHost(L"githubusercontent.com.evil.com"));
    CHECK(!IsTrustedDownloadHost(L"evilgithub.com"));
    CHECK(!IsTrustedDownloadHost(L"example.com"));
}

// A check never calls an unusable release "up to date" (what 1.3.1 did after the rename).
void TestCheckOutcome() {
    CHECK(CheckOutcome(true, 200, true, true, true, L"idle") == L"available");
    CHECK(CheckOutcome(true, 200, true, false, false, L"idle") == L"upToDate");
    CHECK(CheckOutcome(false, 404, false, false, true, L"idle") == L"upToDate");
    CHECK(CheckOutcome(true, 200, false, false, true, L"upToDate") == L"unusable");
    CHECK(CheckOutcome(false, 0, false, false, true, L"upToDate") == L"failed");
    // Scheduled checks that fail keep what was shown.
    CHECK(CheckOutcome(true, 200, false, false, false, L"available") == L"available");
    CHECK(CheckOutcome(false, 0, false, false, false, L"checking") == L"idle");
}

void TestLanguages() {
    using i18n::Resolve;
    CHECK(Resolve(L"auto", {L"pl-PL"}) == L"pl");
    CHECK(Resolve(L"auto", {L"de-DE", L"fr-FR"}) == L"fr");
    CHECK(Resolve(L"auto", {L"de-DE", L"it-IT"}) == L"en");
    CHECK(Resolve(L"auto", {L"zh-TW"}) == L"zh-Hans");
    CHECK(Resolve(L"auto", {L"zh-Hans-CN"}) == L"zh-Hans");
    CHECK(Resolve(L"auto", {L"pt-PT"}) == L"pt-BR");
    CHECK(Resolve(L"auto", {L"ar-SA"}) == L"ar");
    CHECK(Resolve(L"auto", {L"bn-IN"}) == L"bn");
    CHECK(Resolve(L"auto", {L"hi-IN"}) == L"hi");
    CHECK(Resolve(L"auto", {L"ja-JP"}) == L"ja");
    CHECK(Resolve(L"auto", {L"ru-RU"}) == L"ru");
    CHECK(Resolve(L"auto", {L"es-MX"}) == L"es");
    CHECK(Resolve(L"auto", {L"en-GB"}) == L"en");
    CHECK(Resolve(L"auto", {}) == L"en");
    CHECK(Resolve(L"ja", {L"pl-PL"}) == L"ja");
    CHECK(Resolve(L"PT-br", {L"pl-PL"}) == L"pt-BR");
    CHECK(Resolve(L"xx", {L"es-ES"}) == L"es");
    CHECK(Resolve(L"", {L"ru-RU"}) == L"ru");
}

bool Matches(std::vector<std::wstring> entries, const wchar_t* path) {
    std::wstring list;
    for (const std::wstring& e : entries) list += e + L'\0';
    list += L'\0';
    list += L'\0';
    mw::ImageNames names;
    mw::MakeImageNames(path, &names);
    return mw::ListMatches(list.c_str(), names);
}

void TestProgramLists() {
    CHECK(Matches({L"notepad.exe"}, L"C:\\Windows\\notepad.exe"));
    CHECK(Matches({L"notepad"}, L"C:\\Windows\\NOTEPAD.EXE"));
    CHECK(Matches({L"c:\\windows\\notepad.exe"}, L"C:\\Windows\\notepad.exe"));
    CHECK(!Matches({L"note"}, L"C:\\Windows\\notepad.exe"));
    CHECK(Matches({L"\\steamapps\\common\\"}, L"D:\\Steam\\steamapps\\common\\Game\\game.exe"));
    CHECK(!Matches({L"\\steamapps\\common\\"}, L"C:\\Program Files\\Steam\\steam.exe"));
    CHECK(Matches({L"\\epic games\\"}, L"C:\\Program Files\\Epic Games\\Fortnite\\x.exe"));
    // A full path to an exe is an exact entry, not a folder pattern.
    CHECK(!Matches({L"c:\\games\\a.exe"}, L"C:\\Games\\a.exe.backup\\b.exe"));
}

void TestJson() {
    json::Value root;
    CHECK(json::Parse(L"{\"future\": {\"x\": [1, 2.5, \"s\"]}, \"enabled\": true, \"name\": \"a\\\"b\\\\c\\u00e9\"}", &root));
    json::Set(&root, L"enabled", json::Bool(false));
    json::Set(&root, L"language", json::String(L"pl"));
    std::wstring text = json::Serialize(root);
    json::Value again;
    CHECK(json::Parse(text, &again));
    CHECK(again.GetBool(L"enabled", true) == false);
    const json::Value* future = again.Get(L"future");
    CHECK(future && future->type == json::Value::Object && future->Get(L"x") && future->Get(L"x")->items.size() == 3);
    CHECK(again.Get(L"name") && again.Get(L"name")->str == L"a\"b\\c\u00e9");
    CHECK(again.Get(L"language") && again.Get(L"language")->str == L"pl");
    CHECK(again.members.front().first == L"future");   // order kept
}

void TestWidgetGrid() {
    using namespace widgets;
    // 1920 x 1040 work area at 100 %: cells of 164 px, 16 px apart, 24 px from the edges
    // (12 from the taskbar), rows half a cell high.
    Grid grid({0, 0, 1920, 1040}, 1.0);
    CHECK(grid.Cols() == 10);
    CHECK(grid.Rows() == 11);
    CHECK(grid.CardSize(SpanOf(Size::Small)).cx == 164);
    CHECK(grid.CardSize(SpanOf(Size::Medium)).cx == 344);
    CHECK(grid.CardSize(SpanOf(Size::Large)).cy == 344);
    // Column 0 is the rightmost; a wider widget grows to the left.
    RECT r = grid.Rect({0, 0, SpanOf(Size::Small)});
    CHECK(r.right == 1920 - 24 && r.top == 24 && r.left == 1920 - 24 - 164);
    RECT m = grid.Rect({0, 2, SpanOf(Size::Medium)});
    CHECK(m.right == 1920 - 24 && m.left == 1920 - 24 - 344 && m.top == 24 + 180);
    CHECK(grid.Rect({0, 1, SpanOf(Size::Small)}).top == 24 + 90);
    CHECK(!grid.Inside({9, 0, SpanOf(Size::Medium)}));
    CHECK(grid.Inside({8, 7, SpanOf(Size::Large)}));
    CHECK(!grid.Inside({8, 8, SpanOf(Size::Large)}));
    // The last row lies on the bottom margin, right above the taskbar, clear of the one
    // above it.
    RECT low = grid.Rect({0, 9, SpanOf(Size::Small)});
    CHECK(low.bottom == 1040 - 12 && low.top >= grid.Rect({0, 7, SpanOf(Size::Small)}).bottom + 16);
    CHECK(grid.Rect({0, 7, SpanOf(Size::Large)}).bottom == 1040 - 12);

    // New widgets fill the top row from the right.
    std::vector<Slot> taken;
    Slot s;
    CHECK(grid.First(SpanOf(Size::Medium), taken, &s) && s.col == 0 && s.row == 0);
    taken.push_back(s);
    CHECK(grid.First(SpanOf(Size::Small), taken, &s) && s.col == 2 && s.row == 0);
    taken.push_back(s);
    CHECK(Overlaps({1, 0, SpanOf(Size::Large)}, taken[0]));
    CHECK(!Overlaps({3, 0, SpanOf(Size::Small)}, taken[1]));
    // Dropped near a taken slot: the closest free one.
    RECT first = grid.Rect({0, 0, SpanOf(Size::Small)});
    CHECK(grid.Nearest(SpanOf(Size::Small), {first.left + 30, first.top + 20}, taken, &s));
    CHECK(s.row == 2 && s.col == 0);

    // Nothing fits on a tiny screen.
    Grid tiny({0, 0, 200, 150}, 1.0);
    CHECK(!tiny.First(SpanOf(Size::Small), {}, &s));
    // 150 %: whole pixels.
    Grid big({0, 0, 2560, 1400}, 1.5);
    CHECK(big.CellPx() == 246 && big.CardSize(SpanOf(Size::Medium)).cx == 246 * 2 + 24);
}

void TestWidgetNames() {
    using namespace widgets;
    Kind k;
    CHECK(KindFromName(L"weather", &k) && k == Kind::Weather);
    CHECK(!KindFromName(L"stocks", &k));
    for (int i = 0; i < (int)Kind::Count; ++i) CHECK(KindFromName(KindName((Kind)i), &k) && k == (Kind)i);
    CHECK(SizeFromName(L"large") == Size::Large && SizeFromName(L"huge") == Size::Small);
    CHECK(LookFromName(L"dark") == Look::Dark && LookFromName(L"") == Look::Auto);
    CHECK(!Supports(Kind::Clock, Size::Large) && Supports(Kind::Calendar, Size::Large));
    CHECK(!Supports(Kind::Countdown, Size::Large) && Supports(Kind::NowPlaying, Size::Medium));
}

void TestCountdown() {
    using namespace widgets;
    SYSTEMTIME day = {}, today = {};
    CHECK(ParseDay(L"2026-12-24", &day) && day.wYear == 2026 && day.wMonth == 12 && day.wDay == 24);
    CHECK(day.wDayOfWeek == 4);   // a Thursday
    CHECK(!ParseDay(L"2026-02-30", &day) && !ParseDay(L"2026-13-01", &day) && !ParseDay(L"", &day));
    CHECK(!ParseDay(L"24.12.2026", &day) && !ParseDay(L"2026-1-024", &day) && !ParseDay(L"2026-12-24x", &day));
    CHECK(ParseDay(L"2026-10-27", &today));
    today.wHour = 23;   // the time of day doesn't count
    today.wMinute = 59;
    CHECK(ParseDay(L"2026-10-28", &day) && DaysBetween(today, day) == 1);
    CHECK(ParseDay(L"2026-10-26", &day) && DaysBetween(today, day) == -1);
    CHECK(ParseDay(L"2027-10-27", &day) && DaysBetween(today, day) == 365);
    CHECK(DaysBetween(today, today) == 0);
}

void TestMediaAppName() {
    using namespace widgets;
    CHECK(MediaAppName(L"Spotify.exe") == L"Spotify");
    CHECK(MediaAppName(L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify") == L"Spotify");
    CHECK(MediaAppName(L"MSEdge") == L"Microsoft Edge");
    CHECK(MediaAppName(L"Chrome") == L"Google Chrome");
    CHECK(MediaAppName(L"308046B0AF4A39CB") == L"Firefox");
    CHECK(MediaAppName(L"Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == L"Media Player");
    CHECK(MediaAppName(L"foobar2000.exe") == L"Foobar2000");
    CHECK(MediaAppName(L"6E7F1A2B3C4D5E6F") == L"");
    CHECK(MediaAppName(L"") == L"");
}

const wchar_t* kForecast = LR"({
  "latitude": 50.3, "longitude": 18.9,
  "current": {"time": "2026-10-23T14:15", "interval": 900, "temperature_2m": 21.6, "weather_code": 1, "is_day": 1},
  "hourly": {
    "time": ["2026-10-23T12:00", "2026-10-23T13:00", "2026-10-23T14:00", "2026-10-23T15:00", "2026-10-23T16:00"],
    "temperature_2m": [19.0, 20.5, 21.0, 22.4, null],
    "weather_code": [3, 2, 1, 61, 0],
    "is_day": [1, 1, 1, 1, 0]
  },
  "daily": {
    "time": ["2026-10-23", "2026-10-24", "2026-10-25"],
    "weather_code": [1, 63, 95],
    "temperature_2m_max": [24.8, 17.0, 15.2],
    "temperature_2m_min": [12.1, 10.0, -0.4]
  }
})";

void TestForecast() {
    using namespace widgets;
    Forecast f;
    CHECK(ParseForecast(kForecast, &f));
    CHECK(f.valid && f.temp > 21.5 && f.temp < 21.7 && !f.night);
    CHECK(f.cond.sky == Sky::MostlySunny && f.cond.icon == Icon::Sun);
    CHECK(f.low > 12 && f.low < 12.2 && f.high > 24.7 && f.high < 24.9);
    // Now (14:xx, the current values), then 15:00; 16:00 has no temperature.
    CHECK(f.hours.size() == 2);
    CHECK(f.hours.size() == 2 && f.hours[0].hour == 14 && f.hours[1].hour == 15 &&
          f.hours[1].cond.sky == Sky::Rain);
    CHECK(f.days.size() == 3 && f.days[0].date.wDayOfWeek == 5 && f.days[2].date.wDay == 25);
    CHECK(f.days.size() == 3 && f.days[2].cond.icon == Icon::Storm && f.days[2].low < 0);
    CHECK(!ParseForecast(L"{\"error\": true, \"reason\": \"x\"}", &f));
    CHECK(!ParseForecast(L"not json", &f));

    CHECK(FromWmo(0, false).sky == Sky::Clear && FromWmo(0, false).icon == Icon::Moon);
    CHECK(FromWmo(2, true).icon == Icon::SunCloud && FromWmo(45, true).icon == Icon::Fog);
    CHECK(FromWmo(77, true).sky == Sky::Snow && FromWmo(57, true).icon == Icon::Sleet);
    CHECK(FromWmo(1234, true).sky == Sky::Cloudy);
    std::wstring url = ForecastUrl(50.2945, 18.9178, true);
    CHECK(url.find(L"https://api.open-meteo.com/v1/forecast?latitude=50.2945&longitude=18.9178") == 0);
    CHECK(url.find(L"temperature_unit=fahrenheit") != std::wstring::npos);
}

void TestLocation() {
    using namespace widgets;
    // Rounded to about a kilometre before anything leaves the PC.
    CHECK(RoundCoordinate(50.29634) == 50.3 && RoundCoordinate(-18.91777) == -18.92);
    CHECK(DistanceKm(50.30, 18.92, 50.30, 18.92) < 0.001);
    double km = DistanceKm(52.2297, 21.0122, 50.0647, 19.9450);   // Warsaw - Krakow
    CHECK(km > 250 && km < 254);
    std::wstring url = PlaceNameUrl(50.2963, 18.9177, L"pl-PL\"&x");
    CHECK(url == L"https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=12&lat=50.30&lon=18.92"
                 L"&accept-language=pl-PLx");
    std::wstring name;
    CHECK(ParsePlaceName(LR"({"name": "x", "address": {"town": "Bytom", "county": "Silesia"}})", &name) &&
          name == L"Bytom");
    CHECK(ParsePlaceName(LR"({"address": {"city": "Kraków", "town": "y"}})", &name) && name == L"Kraków");
    CHECK(ParsePlaceName(LR"({"name": "Sea", "address": {}})", &name) && name == L"Sea");
    CHECK(!ParsePlaceName(LR"({"error": "Unable to geocode"})", &name));
    CHECK(!ParsePlaceName(L"<html>", &name));
}

// The genie's time curves must stay symmetric (a running genie turns around without a
// jump), and every style must start on the window and end faded out.
void TestGenieCurves() {
    for (int style : {kGenieModern, kGenieClassic, kGenieScale}) {
        GenieParams g = {};
        g.style = style;
        for (int i = 0; i <= 20; ++i) {
            float x = i / 20.0f;
            CHECK(fabsf(GenieEase(g, 1.0f - x) - (1.0f - GenieEase(g, x))) < 1e-5f);
        }
    }
    for (int style : {kGenieModern, kGenieClassic, kGenieScale, kGenieShatter, kGenieFragments, kGenieZoom}) {
        for (bool closing : {false, true}) {
            GenieParams g = {};
            g.style = style;
            g.closing = closing;
            CHECK(GenieEase(g, 0.0f) == 0.0f && GenieEase(g, 1.0f) == 1.0f);
            CHECK(GenieFade(g, 0.0f) == 1.0f && GenieFade(g, 1.0f) == 0.0f);
        }
    }
    // Pieces flying together slow down as they settle: the opening curve ends flat.
    GenieParams open = {};
    open.style = kGenieFragments;
    CHECK(GenieEase(open, 1.0f) - GenieEase(open, 0.99f) < 0.001f);

    // Zoom opening: a gentle overshoot (t dips below 0, the window grows past its size by
    // about kZoomOvershoot), never a wild one; fully shown while it does.
    GenieParams zoom = {};
    zoom.style = kGenieZoom;
    float peak = 0.0f;
    for (int i = 0; i <= 200; ++i) peak = fmaxf(peak, GenieEase(zoom, i / 200.0f));
    CHECK(peak > 1.005f && peak < 1.03f);
    CHECK(GenieFade(zoom, 1.0f - peak) == 1.0f);
    zoom.closing = true;
    CHECK(fabsf(GenieEase(zoom, 0.5f) - 0.5f) < 1e-5f);   // closing: the smooth symmetric curve
    GenieParams g = {};
    g.style = kGenieScale;
    g.win = {100, 200, 800, 600};
    g.icon = {1000, 1400, 32, 24};
    float x, y;
    GenieVertex(g, 0.0f, 0.0f, 0.0f, &x, &y);
    CHECK(fabsf(x - 100) < 0.01f && fabsf(y - 200) < 0.01f);
    GenieVertex(g, 1.0f, 1.0f, 0.0f, &x, &y);
    CHECK(fabsf(x - 900) < 0.01f && fabsf(y - 800) < 0.01f);
    GenieVertex(g, 0.0f, 0.0f, 1.0f, &x, &y);
    CHECK(fabsf(x - 1000) < 0.01f && fabsf(y - 1400) < 0.01f);
    GenieVertex(g, 1.0f, 1.0f, 1.0f, &x, &y);
    CHECK(fabsf(x - 1032) < 0.01f && fabsf(y - 1424) < 0.01f);
    CHECK(!GenieFrameDegenerate(g, 0.5f));

    // Shatter: 9 cells along the longer side, fragments 5; never fewer than 3 along either.
    int cols = 0, rows = 0;
    ShatterGrid(1200, 800, kShatterCells, &cols, &rows);
    CHECK(cols == 9 && rows == 6);
    ShatterGrid(500, 1000, kShatterCells, &cols, &rows);
    CHECK(cols == 5 && rows == 9);
    ShatterGrid(100, 2000, kShatterCells, &cols, &rows);
    CHECK(cols == 3 && rows == 9);
    ShatterGrid(1200, 800, kFragmentCells, &cols, &rows);
    CHECK(cols == 5 && rows == 3);
    for (int style : {kGenieShatter, kGenieFragments}) {
        g.style = style;
        CHECK(!GenieFrameDegenerate(g, 0.5f) && GenieFrameDegenerate(g, 1.0f));
    }
}

}  // namespace

int wmain() {
    TestVersions();
    TestRelease();
    TestSums();
    TestHosts();
    TestCheckOutcome();
    TestLanguages();
    TestProgramLists();
    TestJson();
    TestWidgetGrid();
    TestWidgetNames();
    TestCountdown();
    TestMediaAppName();
    TestForecast();
    TestLocation();
    TestGenieCurves();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}
