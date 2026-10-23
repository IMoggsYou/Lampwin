// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

#include <windows.h>
#include <MinHook.h>
#include "hook/runtime.h"

// Creates (but does not enable) an inline hook on a user32 export. GetProcAddress
// follows forwarders, so e.g. DefWindowProcW lands on the ntdll implementation every
// caller ends up in - the same target Windhawk's Wh_SetFunctionHook resolves to.
bool CreateUser32Hook(const char* name, void* detour, void** original) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    void* target = (void*)GetProcAddress(user32, name);
    if (!target) return false;
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK) {
        DbgLog(L"MH_CreateHook(%hs) failed: %d", name, (int)st);
        return false;
    }
    return true;
}
