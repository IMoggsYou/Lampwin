// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/weather.h"
#include <winhttp.h>
#include <stdio.h>
#include "host/json.h"
#include "version.h"

namespace widgets {

namespace {

const wchar_t kUserAgent[] = L"MacWin/" MACWIN_VERSION_W L" (+https://github.com/IMoggsYou/Lampwin)";
const size_t kMaxBytes = 512 * 1024;

const wchar_t* const kSkyKeys[] = {
    L"host.widgets.weather.sunny",        L"host.widgets.weather.clear",
    L"host.widgets.weather.mostlySunny",  L"host.widgets.weather.mostlyClear",
    L"host.widgets.weather.partlyCloudy", L"host.widgets.weather.cloudy",
    L"host.widgets.weather.fog",          L"host.widgets.weather.drizzle",
    L"host.widgets.weather.rain",         L"host.widgets.weather.heavyRain",
    L"host.widgets.weather.freezingRain", L"host.widgets.weather.snow",
    L"host.widgets.weather.heavySnow",    L"host.widgets.weather.showers",
    L"host.widgets.weather.thunderstorm",
};
static_assert(ARRAYSIZE(kSkyKeys) == (int)Sky::Thunderstorm + 1, "one text per sky");

const json::Value* Member(const json::Value* v, const wchar_t* key, json::Value::Type type) {
    const json::Value* m = v ? v->Get(key) : nullptr;
    return m && m->type == type ? m : nullptr;
}

// Element i of an array of numbers; Open-Meteo writes null where it has no value.
bool NumberAt(const json::Value* array, size_t i, double* out) {
    if (!array || i >= array->items.size() || array->items[i].type != json::Value::Number) return false;
    *out = array->items[i].number;
    return true;
}

bool StringAt(const json::Value* array, size_t i, std::wstring* out) {
    if (!array || i >= array->items.size() || array->items[i].type != json::Value::String) return false;
    *out = array->items[i].str;
    return true;
}

// "2026-10-23" (or the date part of "2026-10-23T14:00") with its day of the week.
bool ParseDate(const std::wstring& iso, SYSTEMTIME* out) {
    int y = 0, m = 0, d = 0;
    if (swscanf(iso.c_str(), L"%4d-%2d-%2d", &y, &m, &d) != 3) return false;
    SYSTEMTIME st = {};
    st.wYear = (WORD)y;
    st.wMonth = (WORD)m;
    st.wDay = (WORD)d;
    FILETIME ft;
    // The round trip fills in wDayOfWeek (and rejects impossible dates).
    if (!SystemTimeToFileTime(&st, &ft) || !FileTimeToSystemTime(&ft, out)) return false;
    return true;
}

int HourOf(const std::wstring& iso) {
    size_t t = iso.find(L'T');
    return t != std::wstring::npos && t + 3 <= iso.size() ? _wtoi(iso.substr(t + 1, 2).c_str()) : -1;
}

}  // namespace

Condition FromWmo(int code, bool day) {
    Icon sun = day ? Icon::Sun : Icon::Moon;
    Icon partly = day ? Icon::SunCloud : Icon::MoonCloud;
    switch (code) {
        case 0: return {day ? Sky::Sunny : Sky::Clear, sun};
        case 1: return {day ? Sky::MostlySunny : Sky::MostlyClear, sun};
        case 2: return {Sky::PartlyCloudy, partly};
        case 3: return {Sky::Cloudy, Icon::Cloud};
        case 45: case 48: return {Sky::Fog, Icon::Fog};
        case 51: case 53: case 55: return {Sky::Drizzle, Icon::Drizzle};
        case 56: case 57: case 66: case 67: return {Sky::FreezingRain, Icon::Sleet};
        case 61: case 63: return {Sky::Rain, Icon::Rain};
        case 65: case 82: return {Sky::HeavyRain, Icon::Rain};
        case 71: case 73: case 77: case 85: case 86: return {Sky::Snow, Icon::Snow};
        case 75: return {Sky::HeavySnow, Icon::Snow};
        case 80: case 81: return {Sky::Showers, Icon::Rain};
        case 95: case 96: case 99: return {Sky::Thunderstorm, Icon::Storm};
    }
    return {Sky::Cloudy, Icon::Cloud};
}

const wchar_t* SkyKey(Sky sky) {
    return kSkyKeys[(int)sky];
}

std::wstring ForecastUrl(double latitude, double longitude, bool fahrenheit) {
    wchar_t url[512];
    swprintf(url, ARRAYSIZE(url),
             L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             L"&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code,is_day"
             L"&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=7"
             L"&temperature_unit=%ls",
             latitude, longitude, fahrenheit ? L"fahrenheit" : L"celsius");
    return url;
}

bool ParseForecast(const std::wstring& text, Forecast* out) {
    json::Value root;
    if (!json::Parse(text, &root) || root.type != json::Value::Object) return false;
    const json::Value* current = Member(&root, L"current", json::Value::Object);
    const json::Value* hourly = Member(&root, L"hourly", json::Value::Object);
    const json::Value* daily = Member(&root, L"daily", json::Value::Object);
    const json::Value* now = Member(current, L"time", json::Value::String);
    const json::Value* temp = Member(current, L"temperature_2m", json::Value::Number);
    const json::Value* code = Member(current, L"weather_code", json::Value::Number);
    if (!now || !temp || !code) return false;

    Forecast f;
    f.valid = true;
    f.temp = temp->number;
    const json::Value* isDay = Member(current, L"is_day", json::Value::Number);
    f.night = isDay && isDay->number == 0;
    f.cond = FromWmo((int)code->number, !f.night);

    // Today's range and the coming days.
    const json::Value* dTime = Member(daily, L"time", json::Value::Array);
    const json::Value* dCode = Member(daily, L"weather_code", json::Value::Array);
    const json::Value* dMax = Member(daily, L"temperature_2m_max", json::Value::Array);
    const json::Value* dMin = Member(daily, L"temperature_2m_min", json::Value::Array);
    for (size_t i = 0; dTime && i < dTime->items.size(); ++i) {
        DayForecast d;
        std::wstring date;
        double c = 0;
        if (!StringAt(dTime, i, &date) || !ParseDate(date, &d.date) || !NumberAt(dMin, i, &d.low) ||
            !NumberAt(dMax, i, &d.high) || !NumberAt(dCode, i, &c)) {
            continue;
        }
        d.cond = FromWmo((int)c, true);
        f.days.push_back(d);
    }
    if (f.days.empty()) return false;
    f.low = f.days[0].low < f.temp ? f.days[0].low : f.temp;
    f.high = f.days[0].high > f.temp ? f.days[0].high : f.temp;

    // The hours from now on: the first one is "now", with the current temperature.
    const json::Value* hTime = Member(hourly, L"time", json::Value::Array);
    const json::Value* hTemp = Member(hourly, L"temperature_2m", json::Value::Array);
    const json::Value* hCode = Member(hourly, L"weather_code", json::Value::Array);
    const json::Value* hDay = Member(hourly, L"is_day", json::Value::Array);
    const std::wstring hourNow = now->str.substr(0, 13);   // "2026-10-23T14"
    size_t start = 0;
    while (hTime && start < hTime->items.size()) {
        std::wstring t;
        if (StringAt(hTime, start, &t) && t.compare(0, 13, hourNow) >= 0) break;
        ++start;
    }
    HourForecast first;
    first.hour = HourOf(now->str);
    first.temp = f.temp;
    first.cond = f.cond;
    f.hours.push_back(first);
    for (size_t i = start + 1; hTime && i < hTime->items.size() && f.hours.size() < 12; ++i) {
        HourForecast h;
        std::wstring t;
        double c = 0, day = 1;
        if (!StringAt(hTime, i, &t) || !NumberAt(hTemp, i, &h.temp) || !NumberAt(hCode, i, &c)) continue;
        NumberAt(hDay, i, &day);
        h.hour = HourOf(t);
        h.cond = FromWmo((int)c, day != 0);
        f.hours.push_back(h);
    }
    *out = f;
    return true;
}

void HttpCancel::Cancel() {
    AcquireSRWLockExclusive(&lock_);
    cancelled_ = true;
    // Closing the handle of a request in progress makes its blocked call return at once.
    if (request_) WinHttpCloseHandle(request_);
    request_ = nullptr;
    ReleaseSRWLockExclusive(&lock_);
}

bool HttpCancel::Cancelled() {
    AcquireSRWLockShared(&lock_);
    bool cancelled = cancelled_;
    ReleaseSRWLockShared(&lock_);
    return cancelled;
}

bool FetchJson(const std::wstring& url, std::wstring* body, HttpCancel* cancel) {
    URL_COMPONENTS uc = {sizeof(uc)};
    wchar_t host[256] = L"", path[2048] = L"";
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAYSIZE(path) - 512;
    uc.lpszExtraInfo = path + ARRAYSIZE(path) - 512;
    uc.dwExtraInfoLength = 512;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    std::wstring object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) +
                          std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) return false;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(session, 8000, 8000, 10000, 10000);

    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    bool started = false;
    if (request) {
        AcquireSRWLockExclusive(&cancel->lock_);
        started = !cancel->cancelled_;
        if (started) cancel->request_ = request;
        ReleaseSRWLockExclusive(&cancel->lock_);
        if (!started) WinHttpCloseHandle(request);
    }
    if (started) {
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
        WinHttpAddRequestHeaders(request, L"Accept: application/json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        DWORD status = 0, size = sizeof(status);
        std::string bytes;
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
            status == 200) {
            ok = true;
            char chunk[16 * 1024];
            for (;;) {
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk, sizeof(chunk), &read)) {
                    ok = false;
                    break;
                }
                if (read == 0) break;
                bytes.append(chunk, read);
                if (bytes.size() > kMaxBytes) {
                    ok = false;
                    break;
                }
            }
        }
        // Whoever takes the handle out closes it: we, or Cancel() a moment ago.
        AcquireSRWLockExclusive(&cancel->lock_);
        bool mine = cancel->request_ == request;
        if (mine) cancel->request_ = nullptr;
        ok = ok && !cancel->cancelled_;
        ReleaseSRWLockExclusive(&cancel->lock_);
        if (mine) WinHttpCloseHandle(request);
        if (ok) {
            int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
            body->assign((size_t)n, L'\0');
            if (n) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &(*body)[0], n);
        }
    }
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

}  // namespace widgets
