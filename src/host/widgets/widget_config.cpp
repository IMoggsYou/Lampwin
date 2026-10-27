// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_config.h"
#include <windows.h>
#include <stdio.h>

namespace widgets {

namespace {

const wchar_t* const kKindNames[] = {L"clock", L"calendar", L"weather", L"system", L"notes", L"photo", L"countdown", L"nowPlaying"};
static_assert(ARRAYSIZE(kKindNames) == (int)Kind::Count, "one name per kind");

}  // namespace

const wchar_t* KindName(Kind k) {
    return kKindNames[(int)k];
}

bool KindFromName(const std::wstring& name, Kind* out) {
    for (int i = 0; i < (int)Kind::Count; ++i) {
        if (name == kKindNames[i]) {
            *out = (Kind)i;
            return true;
        }
    }
    return false;
}

const wchar_t* SizeName(Size s) {
    return s == Size::Large ? L"large" : s == Size::Medium ? L"medium" : L"small";
}

Size SizeFromName(const std::wstring& name) {
    return name == L"large" ? Size::Large : name == L"medium" ? Size::Medium : Size::Small;
}

const wchar_t* LookName(Look l) {
    return l == Look::Dark ? L"dark" : l == Look::Light ? L"light" : L"auto";
}

Look LookFromName(const std::wstring& name) {
    return name == L"dark" ? Look::Dark : name == L"light" ? Look::Light : Look::Auto;
}

bool Supports(Kind k, Size s) {
    return s != Size::Large || (k != Kind::Clock && k != Kind::System && k != Kind::Countdown && k != Kind::NowPlaying);
}

bool ParseDay(const std::wstring& iso, SYSTEMTIME* out) {
    int y = 0, m = 0, d = 0;
    wchar_t rest = 0;
    if (iso.size() != 10 || swscanf(iso.c_str(), L"%4d-%2d-%2d%lc", &y, &m, &d, &rest) != 3) return false;
    SYSTEMTIME st = {};
    st.wYear = (WORD)y;
    st.wMonth = (WORD)m;
    st.wDay = (WORD)d;
    FILETIME ft;
    // SystemTimeToFileTime refuses 2026-02-30; the way back fills in the weekday.
    if (y < 1601 || y > 30827 || !SystemTimeToFileTime(&st, &ft) || !FileTimeToSystemTime(&ft, out)) return false;
    return true;
}

int DaysBetween(const SYSTEMTIME& from, const SYSTEMTIME& to) {
    auto day = [](const SYSTEMTIME& t) -> LONGLONG {
        SYSTEMTIME d = {};
        d.wYear = t.wYear;
        d.wMonth = t.wMonth;
        d.wDay = t.wDay;
        FILETIME ft = {};
        SystemTimeToFileTime(&d, &ft);
        return (LONGLONG)(((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / (10000000ULL * 86400));
    };
    return (int)(day(to) - day(from));
}

}  // namespace widgets
