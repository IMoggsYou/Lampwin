// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once

// Sunrise and sunset, worked out on the PC (NOAA's formulas, to about a minute): the
// appearance schedule never asks the internet when the sun sets.
namespace sun {

enum class Day { Normal, PolarDay, PolarNight };

// For the calendar day `year-month-day` at `latitude` / `longitude` (degrees, east and north
// positive): sunrise and sunset in minutes after 00:00 UTC of that day. They can be below
// 0 or past 1440 far from Greenwich. Without a sunrise or sunset that day, says why.
Day Times(double latitude, double longitude, int year, int month, int day, double* riseUtc, double* setUtc);

}  // namespace sun
