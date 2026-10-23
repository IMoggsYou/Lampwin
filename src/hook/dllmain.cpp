// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include <windows.h>
#include "common/names.h"
#include "common/strutil.h"
#include "hook/runtime.h"

HMODULE g_hModule = nullptr;

// System processes the DLL never stays in, whatever the settings say: session and logon
// infrastructure, the compositor, the font driver host, the UAC prompt, the lock screen,
// the shell's own UI hosts and the crash reporter (names without ".exe").
static const wchar_t* const kNeverLoad[] = {
    L"csrss", L"winlogon", L"logonui", L"lsass", L"dwm", L"fontdrvhost", L"consent",
    L"lockapp", L"searchhost", L"startmenuexperiencehost", L"shellexperiencehost",
    L"textinputhost", L"werfault", L"werfaultsecure",
};

// Anti-cheat that ships in a folder next to the game. It may take any foreign module
// for a cheat, so the DLL stays out of every program installed there.
static const wchar_t* const kAntiCheatDirs[] = {L"EasyAntiCheat", L"EasyAntiCheat_EOS", L"BattlEye"};

// True if folder `dir` (its first `len` characters, no trailing backslash) holds one of
// kAntiCheatDirs.
static bool HasAntiCheatDir(const wchar_t* dir, size_t len) {
    wchar_t probe[MAX_PATH + 32];
    if (len == 0 || len + 32 > ARRAYSIZE(probe)) return false;
    for (const wchar_t* name : kAntiCheatDirs) {
        wmemcpy(probe, dir, len);
        probe[len] = L'\\';
        wcscpy(probe + len + 1, name);
        DWORD attr = GetFileAttributesW(probe);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    return false;
}

// `path`: the exe as GetModuleFileName gave it; `lower`: the same, lowercase (same length).
// Looks next to the exe and, for Unreal games (<game>\<Project>\Binaries\Win64\<exe>),
// also in <Project> and <game>, where their anti-cheat usually sits.
static bool NextToAntiCheat(const wchar_t* path, const wchar_t* lower) {
    const wchar_t* slash = wcsrchr(lower, L'\\');
    if (!slash) return false;
    const size_t dirLen = (size_t)(slash - lower);
    if (HasAntiCheatDir(path, dirLen)) return true;
    const size_t tail = 15;   // "\binaries\win64"
    if (dirLen <= tail || (wcsncmp(lower + dirLen - tail, L"\\binaries\\win64", tail) != 0 &&
                           wcsncmp(lower + dirLen - tail, L"\\binaries\\win32", tail) != 0)) {
        return false;
    }
    const size_t projectLen = dirLen - tail;
    if (HasAntiCheatDir(path, projectLen)) return true;
    size_t gameLen = projectLen;
    while (gameLen > 0 && lower[gameLen - 1] != L'\\') --gameLen;
    return gameLen > 1 && HasAntiCheatDir(path, gameLen - 1);
}

// Runs under the loader lock: kernel32/ntdll only. Decides whether this process may
// keep the DLL at all - refusing here unmaps it again, so programs on the "do not
// inject" list (and, in test mode, everything outside the allowlist) never have
// MacWin code in them.
static bool ShouldLoadIntoThisProcess(bool* isHost, bool* alwaysAllowed) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;
    mw::ImageNames self;
    if (!mw::MakeImageNames(path, &self)) return false;

    // The host loads the DLL itself (to hand its hook procedures to Windows); it
    // keeps it, but never activates anything in it.
    if (wcscmp(self.base, MACWIN_HOST_EXE) == 0 || wcscmp(self.base, MACWIN_HELPER32_EXE) == 0) {
        *isHost = true;
        return true;
    }
    // Our own settings app: always animated (even in test mode), never excluded.
    if (wcscmp(self.base, MACWIN_SETTINGS_EXE) == 0) {
        *alwaysAllowed = true;
        return true;
    }
    for (const wchar_t* name : kNeverLoad) {
        if (wcscmp(self.baseNoExt, name) == 0) return false;
    }

    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, MACWIN_SETTINGS_SECTION);
    if (!hMap) return false;
    auto* s = (const SharedSettings*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SharedSettings));
    bool allow = false;
    if (s && s->magic == MACWIN_SETTINGS_MAGIC && s->size == sizeof(SharedSettings)) {
        static wchar_t list[MACWIN_LIST_CHARS];   // DllMain calls are serialized
        SettingsCopyList(s, s->noInject, list, MACWIN_LIST_CHARS);
        if (!mw::ListMatches(list, self)) {
            if (s->devMode) {
                SettingsCopyList(s, s->devAllowlist, list, MACWIN_LIST_CHARS);
                allow = mw::ListMatches(list, self);
            } else {
                allow = true;
            }
        }
    }
    if (s) UnmapViewOfFile(s);
    CloseHandle(hMap);
    // Last: the only check that touches the disk.
    return allow && !NextToAntiCheat(path, self.full);
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hInst;
        DisableThreadLibraryCalls(hInst);
        bool isHost = false, alwaysAllowed = false;
        if (!ShouldLoadIntoThisProcess(&isHost, &alwaysAllowed)) return FALSE;
        LoaderSetHostProcess(isHost);
        LoaderSetAlwaysAllowed(alwaysAllowed);
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        LoaderOnProcessDetach();
    }
    return TRUE;
}

// Windows calls these in every GUI thread of the session. They are only the vehicle
// that gets the DLL loaded; the real work is done by the inline hooks EnsureInit
// installs. WH_CBT stays installed (it fires rarely: window create / activate /
// min-max); WH_GETMESSAGE is installed for a moment at host start-up so programs
// that were already running pick the DLL up right away.
extern "C" __declspec(dllexport) LRESULT CALLBACK MacWinCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    EnsureInit();
    if (code == HCBT_MINMAX) GenieOnMinMax((HWND)wParam, (int)LOWORD(lParam));
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK MacWinGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    EnsureInit();
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
