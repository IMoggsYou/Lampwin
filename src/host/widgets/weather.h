// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <string>
#include <vector>

// The forecast for the Weather widget, from Open-Meteo (open-meteo.com: free, no key, no
// account). Only the chosen place's coordinates are sent, and only while a Weather widget
// is on the desktop.
namespace widgets {

// What the widget says (host.widgets.weather.<name>) and draws.
enum class Sky {
    Sunny, Clear, MostlySunny, MostlyClear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, HeavyRain,
    FreezingRain, Snow, HeavySnow, Showers, Thunderstorm,
};
enum class Icon { Sun, Moon, SunCloud, MoonCloud, Cloud, Fog, Drizzle, Rain, Sleet, Snow, Storm };

struct Condition {
    Sky sky = Sky::Cloudy;
    Icon icon = Icon::Cloud;
};

// A WMO weather interpretation code (what Open-Meteo reports), by day or by night.
Condition FromWmo(int code, bool day);
const wchar_t* SkyKey(Sky sky);   // "host.widgets.weather.sunny"

struct HourForecast {
    int hour = 0;   // 0-23, the place's local time
    double temp = 0;
    Condition cond;
};

struct DayForecast {
    SYSTEMTIME date = {};   // wYear, wMonth, wDay and wDayOfWeek
    double low = 0, high = 0;
    Condition cond;
};

struct Forecast {
    bool valid = false;
    double temp = 0, low = 0, high = 0;
    Condition cond;
    bool night = false;   // at the place, right now
    std::vector<HourForecast> hours;   // from the current hour on
    std::vector<DayForecast> days;     // from today on
};

std::wstring ForecastUrl(double latitude, double longitude, bool fahrenheit);
// Open-Meteo's answer to ForecastUrl.
bool ParseForecast(const std::wstring& json, Forecast* out);

// Lets another thread abort a FetchJson (or a location lookup) that is waiting (shutting down).
class HttpCancel {
public:
    void Cancel();
    bool Cancelled();

private:
    friend bool FetchJson(const std::wstring& url, std::wstring* body, HttpCancel* cancel);
    SRWLOCK lock_ = SRWLOCK_INIT;
    void* request_ = nullptr;   // HINTERNET in flight
    bool cancelled_ = false;
};

// HTTPS GET of a small JSON document; false on any error or when cancelled.
bool FetchJson(const std::wstring& url, std::wstring* body, HttpCancel* cancel);

}  // namespace widgets
