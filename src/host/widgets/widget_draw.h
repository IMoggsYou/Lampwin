// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <string>
#include <unordered_map>
#include "host/widgets/canvas.h"
#include "host/widgets/media.h"
#include "host/widgets/weather.h"
#include "host/widgets/widget_config.h"

// What each widget looks like. Everything is drawn in DIPs on a card of the widget's
// size; the caller supplies the data (time, forecast, gauges) in a Scene.
namespace widgets {

// The host's texts the widgets use ("host.widgets.*"), in the UI language.
typedef std::unordered_map<std::wstring, std::wstring> Texts;

// The Network widget's chart: the last samples, oldest first.
const int kNetworkHistory = 30;

struct SystemStats {
    int cpu = 0;        // percent
    int memory = 0;
    int disk = 0;       // the Windows drive, used space
    int battery = -1;   // percent, -1 = no battery
    bool charging = false;
    int batteryMinutes = -1;   // left on battery, as Windows estimates it; -1 = unknown
    bool pluggedIn = false;
    // Bytes per second through the adapter the internet goes through, since the previous
    // sample, and that adapter's link speed (bits per second, 0 = unknown).
    double down = 0, up = 0;
    ULONGLONG link = 0;
    float downHistory[kNetworkHistory] = {};
    float upHistory[kNetworkHistory] = {};
};

// NoPlace: neither "my location" nor a chosen place; LocationOff: Windows won't tell where
// this PC is and no place is chosen.
enum class WeatherState { NoPlace, LocationOff, Loading, Ready, Offline };

// What of a card to draw. A clock's face doesn't change, so it is drawn once (Face) and
// every tick only the hands go on top of a copy of it (Hands).
enum class Pass { All, Face, Hands };

struct Scene {
    Kind kind = Kind::Clock;
    Size size = Size::Small;
    Pass pass = Pass::All;
    bool dark = false;
    bool rtl = false;
    const Texts* texts = nullptr;
    std::wstring locale;   // dates in the UI language ("pl-PL")
    SYSTEMTIME now = {};
    bool clock24 = true;   // from the Windows time format
    bool seconds = true;
    int firstDay = 0;      // first day of the week, 0 = Monday ... 6 = Sunday
    // Weather
    WeatherState weather = WeatherState::NoPlace;
    bool fahrenheit = false;
    const Forecast* forecast = nullptr;
    std::wstring place;
    // System
    SystemStats stats;
    // Notes, Photo; Countdown (text is its title)
    std::wstring text;
    IWICBitmapSource* photo = nullptr;
    std::wstring date;   // yyyy-MM-dd
    // Now playing
    const MediaInfo* media = nullptr;
};

// The card's corner (DIPs) and Figma-style smoothing: a continuous corner like the
// widgets on macOS.
const float kCardRadius = 24.0f;
const float kCardSmoothing = 0.7f;

// Draws the widget's card over the whole target, `unit` px per DIP.
void DrawCard(Graphics& g, ID2D1RenderTarget* rt, const Scene& s, float unit, int widthPx, int heightPx);

}  // namespace widgets
