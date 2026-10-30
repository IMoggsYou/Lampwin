// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/desktop/appearance.h"
#include <objbase.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "host/desktop/sun.h"
#include "host/log.h"
#include "host/widgets/location.h"

namespace {

const ULONGLONG kTicksPerMinute = 600000000ULL;   // FILETIME counts 100 ns
const ULONGLONG kTicksPerDay = 1440 * kTicksPerMinute;
// Windows' location is asked again after this long (the PC may have travelled).
const ULONGLONG kLookupEveryMs = 6ULL * 60 * 60 * 1000;
const wchar_t kPersonalize[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

ULONGLONG Ticks(const FILETIME& ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

FILETIME FromTicks(ULONGLONG t) {
    return {(DWORD)t, (DWORD)(t >> 32)};
}

// The local calendar day `offset` days from today.
SYSTEMTIME LocalDay(int offset) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    now.wHour = now.wMinute = now.wSecond = now.wMilliseconds = 0;
    FILETIME ft;
    SystemTimeToFileTime(&now, &ft);
    const FILETIME shifted = FromTicks(Ticks(ft) + (LONGLONG)offset * kTicksPerDay);
    SYSTEMTIME day;
    FileTimeToSystemTime(&shifted, &day);
    return day;
}

// `minutes` after local midnight of `day`, as UTC.
ULONGLONG LocalTimeUtc(SYSTEMTIME day, int minutes) {
    day.wHour = (WORD)(minutes / 60);
    day.wMinute = (WORD)(minutes % 60);
    SYSTEMTIME utc;
    FILETIME ft = {};
    if (TzSpecificLocalTimeToSystemTime(nullptr, &day, &utc)) SystemTimeToFileTime(&utc, &ft);
    return Ticks(ft);
}

struct Change {
    ULONGLONG at;   // UTC
    bool day;       // the day starts (else the night)
};

bool DwordEquals(const wchar_t* name, DWORD want) {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kPersonalize, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
               ERROR_SUCCESS &&
           value == want;
}

// On a worker thread: these can wait on other programs (the broadcast tells every window).
void SwitchWindows(bool day, AppearanceSettings s) {
    if (s.darkMode) {
        const DWORD light = day ? 1 : 0;
        if (!DwordEquals(L"AppsUseLightTheme", light) || !DwordEquals(L"SystemUsesLightTheme", light)) {
            LSTATUS a = RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize, L"AppsUseLightTheme", REG_DWORD, &light,
                                        sizeof(light));
            LSTATUS b = RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize, L"SystemUsesLightTheme", REG_DWORD, &light,
                                        sizeof(light));
            if (a != ERROR_SUCCESS || b != ERROR_SUCCESS) Log(L"appearance: colour mode not set: %ld %ld", a, b);
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG,
                                3000, nullptr);
            Log(L"appearance: %ls mode", day ? L"light" : L"dark");
        }
    }
    const std::wstring& wallpaper = day ? s.dayWallpaper : s.nightWallpaper;
    if (s.wallpaper && !wallpaper.empty()) {
        wchar_t current[MAX_PATH] = {};
        SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, current, 0);
        if (_wcsicmp(current, wallpaper.c_str()) == 0) return;
        if (GetFileAttributesW(wallpaper.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log(L"appearance: wallpaper %ls is gone", wallpaper.c_str());
            return;
        }
        if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)wallpaper.c_str(),
                                   SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)) {
            Log(L"appearance: wallpaper not set: %lu", GetLastError());
        } else {
            Log(L"appearance: %ls wallpaper", day ? L"day" : L"night");
        }
    }
}

}  // namespace

void Appearance::Init(HWND hwnd, UINT_PTR timer, UINT located) {
    hwnd_ = hwnd;
    timer_ = timer;
    located_ = located;
}

void Appearance::Stop() {
    KillTimer(hwnd_, timer_);
    cancel_.Cancel();
    if (locator_.joinable()) locator_.join();
}

void Appearance::Apply(const AppearanceSettings& settings, const widgets::Place& place) {
    // Any other setting changed: a wallpaper the user picked in Windows meanwhile stays.
    const AppearanceSettings& o = settings_;
    const bool same = started_ && settings.darkMode == o.darkMode && settings.wallpaper == o.wallpaper &&
                      settings.dayWallpaper == o.dayWallpaper && settings.nightWallpaper == o.nightWallpaper &&
                      settings.followSun == o.followSun && settings.dayStartMinutes == o.dayStartMinutes &&
                      settings.nightStartMinutes == o.nightStartMinutes && place.valid == place_.valid &&
                      place.latitude == place_.latitude && place.longitude == place_.longitude;
    settings_ = settings;
    place_ = place;
    if (same) return;
    started_ = true;
    Evaluate(true);
}

void Appearance::Check() {
    Evaluate(false);
}

bool Appearance::KnownPlace(double* latitude, double* longitude) const {
    if (haveLocation_) {
        *latitude = latitude_;
        *longitude = longitude_;
        return true;
    }
    if (place_.valid) {
        *latitude = place_.latitude;
        *longitude = place_.longitude;
        return true;
    }
    return false;
}

// Where the PC is, from Windows (on the PC: no internet, and rounded like the weather's).
void Appearance::Locate() {
    if (locating_) return;
    if (locator_.joinable()) locator_.join();   // finished: returns at once
    locating_ = true;
    lastLookup_ = GetTickCount64();
    HWND hwnd = hwnd_;
    UINT message = located_;
    widgets::HttpCancel* cancel = &cancel_;
    std::atomic<bool>* locating = &locating_;
    locator_ = std::thread([hwnd, message, cancel, locating] {
        auto* result = new LocatedPlace();
        double accuracy = 0;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        result->found = widgets::WindowsLocation(&result->latitude, &result->longitude, &accuracy, cancel) ==
                        widgets::LocationStatus::Found;
        CoUninitialize();
        result->latitude = widgets::RoundCoordinate(result->latitude);
        result->longitude = widgets::RoundCoordinate(result->longitude);
        *locating = false;
        if (cancel->Cancelled() || !PostMessageW(hwnd, message, 0, (LPARAM)result)) delete result;
    });
}

void Appearance::OnLocated(LPARAM lp) {
    std::unique_ptr<LocatedPlace> place((LocatedPlace*)lp);
    if (!place->found) {
        Log(L"appearance: Windows doesn't say where this PC is; using the times of day");
        return;
    }
    const bool moved = !haveLocation_ || widgets::DistanceKm(latitude_, longitude_, place->latitude, place->longitude) > 30;
    haveLocation_ = true;
    latitude_ = place->latitude;
    longitude_ = place->longitude;
    if (moved) Evaluate(false);
}

// Works out whether it is day or night now and when that changes next; sets Windows to it
// when it changed (or always, with `force`), and the timer to the next change.
void Appearance::Evaluate(bool force) {
    KillTimer(hwnd_, timer_);
    if (!settings_.Any()) {
        applied_ = -1;
        return;
    }
    double latitude = 0, longitude = 0;
    const bool sun = settings_.followSun && KnownPlace(&latitude, &longitude);
    if (settings_.followSun && !place_.valid &&
        (!haveLocation_ || GetTickCount64() - lastLookup_ > kLookupEveryMs)) {
        Locate();
    }

    // Yesterday, today and tomorrow: enough to know the last change and the next.
    std::vector<Change> changes;
    int polar = -1;   // today has no sunrise or no sunset: 1 day all day, 0 night all day
    for (int offset = -1; offset <= 1; ++offset) {
        SYSTEMTIME day = LocalDay(offset);
        if (!sun) {
            changes.push_back({LocalTimeUtc(day, settings_.dayStartMinutes), true});
            changes.push_back({LocalTimeUtc(day, settings_.nightStartMinutes), false});
            continue;
        }
        double rise = 0, set = 0;
        sun::Day kind = sun::Times(latitude, longitude, day.wYear, day.wMonth, day.wDay, &rise, &set);
        if (kind != sun::Day::Normal) {
            if (offset == 0) polar = kind == sun::Day::PolarDay ? 1 : 0;
            continue;
        }
        SYSTEMTIME midnight = day;
        FILETIME ft;
        SystemTimeToFileTime(&midnight, &ft);   // `day` read as UTC: 00:00 UTC of that date
        changes.push_back({Ticks(ft) + (LONGLONG)(rise * kTicksPerMinute), true});
        changes.push_back({Ticks(ft) + (LONGLONG)(set * kTicksPerMinute), false});
    }
    std::sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.at < b.at; });

    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const ULONGLONG now = Ticks(nowFt);
    int isDay = polar;
    ULONGLONG next = 0;
    for (const Change& c : changes) {
        if (c.at <= now) {
            if (polar < 0) isDay = c.day ? 1 : 0;
        } else if (!next) {
            next = c.at;
        }
    }
    if (isDay < 0) isDay = 1;

    // Checked again at the next change, and at least every hour (the clock may have changed).
    ULONGLONG waitMs = next ? (next - now) / 10000 + 1000 : 60 * 60 * 1000;
    SetTimer(hwnd_, timer_, (UINT)std::min<ULONGLONG>(waitMs, 60 * 60 * 1000), nullptr);

    if (!force && isDay == applied_) return;
    applied_ = isDay;
    std::thread(SwitchWindows, isDay == 1, settings_).detach();
}
