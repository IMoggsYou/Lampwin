// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <string>
#include "host/widgets/weather.h"

// "My location" for the Weather widget: where Windows says this PC is, and the name of the
// town there (OpenStreetMap's Nominatim). Coordinates are rounded to about a kilometre
// before they leave the PC.
namespace widgets {

// Rough: Windows only guesses (from the internet address, kilometres off); a place the user
// chose is used instead, if there is one.
enum class LocationStatus { Unknown, Found, Denied, Unavailable, Rough };

// Positions less precise than this are only a guess: a chosen place is better.
const double kRoughLocationMeters = 5000;

// Windows location services (Windows asks the user first if it wants to). Waits up to
// 10 seconds; a position up to 30 minutes old is good enough. `accuracy`: metres, as
// Windows estimates it. COM must be initialized on the calling thread (multithreaded: the
// answer arrives on another thread).
LocationStatus WindowsLocation(double* latitude, double* longitude, double* accuracy, HttpCancel* cancel);

// 0.01 degree (~1 km): enough for the weather, and no more precise than that.
double RoundCoordinate(double degrees);
double DistanceKm(double lat1, double lon1, double lat2, double lon2);

std::wstring PlaceNameUrl(double latitude, double longitude, const std::wstring& language);
// The town in Nominatim's reverse-geocoding answer.
bool ParsePlaceName(const std::wstring& json, std::wstring* name);

}  // namespace widgets
