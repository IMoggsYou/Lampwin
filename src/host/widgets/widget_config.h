// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <string>
#include <vector>

// Desktop widgets as settings.json stores them ("widgets"). The settings app adds and
// configures them; the host places them, and writes back where the user dragged them.
namespace widgets {

enum class Kind { Clock, Calendar, Weather, System, Notes, Photo, Count };
enum class Size { Small, Medium, Large };
// "theme": the Windows app mode, or always light / dark.
enum class Look { Auto, Light, Dark };

struct Item {
    std::wstring id;
    Kind kind = Kind::Clock;
    Size size = Size::Small;
    Look look = Look::Auto;
    // Where it sits: a monitor (its device name) and a grid slot counted from the top-right
    // corner of that monitor's work area. A new widget has no slot until the host finds one.
    bool placed = false;
    std::wstring monitor;
    int col = 0;
    int row = 0;
    std::wstring text;   // Notes
    std::wstring path;   // Photo

    bool operator==(const Item& o) const = default;
};

// The place the weather is for (picked in the settings app, which looks it up).
struct Place {
    std::wstring name;
    double latitude = 0;
    double longitude = 0;
    bool valid = false;

    bool operator==(const Place& o) const = default;
};

struct Config {
    bool show = true;
    int scale = 100;   // percent of the normal size
    bool fahrenheit = false;
    bool clockSeconds = true;
    // The weather where Windows says this PC is; `place` when that isn't known.
    bool autoPlace = true;
    Place place;
    std::vector<Item> items;

    bool operator==(const Config& o) const = default;
};

const int kMinScale = 80;
const int kMaxScale = 130;

const wchar_t* KindName(Kind k);
bool KindFromName(const std::wstring& name, Kind* out);
const wchar_t* SizeName(Size s);
Size SizeFromName(const std::wstring& name);
const wchar_t* LookName(Look l);
Look LookFromName(const std::wstring& name);
// The sizes a kind comes in (a clock or the system gauges have no large one).
bool Supports(Kind k, Size s);

}  // namespace widgets
