// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/desktop/sun.h"
#include <math.h>

namespace sun {

namespace {

const double kPi = 3.14159265358979323846;

double Rad(double degrees) {
    return degrees * kPi / 180;
}

double Deg(double radians) {
    return radians * 180 / kPi;
}

// Julian day number of a calendar date (the Julian day that starts at its noon).
long JulianDayNumber(int year, int month, int day) {
    const long a = (14 - month) / 12, y = year + 4800 - a, m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

}  // namespace

// The sunrise equation: the sun's mean anomaly, its ecliptic longitude, the solar noon,
// the declination, and the hour angle at which its upper edge touches the horizon
// (-0.833 degrees: refraction and the sun's radius).
Day Times(double latitude, double longitude, int year, int month, int day, double* riseUtc, double* setUtc) {
    const long jdn = JulianDayNumber(year, month, day);
    const double n = jdn - 2451545.0 + 0.0008;
    const double meanNoon = n - longitude / 360;
    const double m = fmod(357.5291 + 0.98560028 * meanNoon, 360);
    const double center = 1.9148 * sin(Rad(m)) + 0.02 * sin(Rad(2 * m)) + 0.0003 * sin(Rad(3 * m));
    const double lambda = fmod(m + center + 180 + 102.9372, 360);
    const double transit = 2451545.0 + meanNoon + 0.0053 * sin(Rad(m)) - 0.0069 * sin(Rad(2 * lambda));
    const double sinDecl = sin(Rad(lambda)) * sin(Rad(23.4397));
    const double cosDecl = cos(asin(sinDecl));
    const double cosHour = (sin(Rad(-0.833)) - sin(Rad(latitude)) * sinDecl) / (cos(Rad(latitude)) * cosDecl);
    if (cosHour < -1) return Day::PolarDay;
    if (cosHour > 1) return Day::PolarNight;
    const double hour = Deg(acos(cosHour));
    const double midnight = jdn - 0.5;   // 00:00 UTC of that day, as a Julian date
    *riseUtc = (transit - hour / 360 - midnight) * 1440;
    *setUtc = (transit + hour / 360 - midnight) * 1440;
    return Day::Normal;
}

}  // namespace sun
