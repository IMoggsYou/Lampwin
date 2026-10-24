// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/widget_config.h"
#include <windows.h>

namespace widgets {

namespace {

const wchar_t* const kKindNames[] = {L"clock", L"calendar", L"weather", L"system", L"notes", L"photo"};
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
    return s != Size::Large || (k != Kind::Clock && k != Kind::System);
}

}  // namespace widgets
