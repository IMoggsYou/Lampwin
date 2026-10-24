// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

// App icons: a program whose icon the user chose (the settings app's App icons page puts
// it at %LOCALAPPDATA%\MacWin\icons\apps\<program>.ico) shows it on its windows - title
// bar, taskbar button, Alt+Tab. Every icon the program sets reaches DefWindowProc as
// WM_SETICON and is swapped there; a window that never sets one (Electron apps use their
// class icon) gets it when it is shown, or when MacWin arrives. The program's own icons
// are kept on the window and given back when the choice is removed, the option is turned
// off or MacWin goes away.
//
// Cost: three message numbers compared per DefWindowProc call; the file is looked at once
// per settings change, and the icons are loaded once per process.

#include <windows.h>
#include <wchar.h>
#include "common/settings.h"
#include "common/strutil.h"
#include "hook/runtime.h"

namespace {

const wchar_t kPropOurs[] = L"MacWin.AppIcon";          // the window shows our icon
const wchar_t kPropBig[] = L"MacWin.AppIcon.Big";       // the program's own ones
const wchar_t kPropSmall[] = L"MacWin.AppIcon.Small";

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_checked = false;
uint32_t g_generation = 0;
bool g_active = false;          // an icon applies to this process now
HICON g_big = nullptr, g_small = nullptr;
FILETIME g_stamp = {};          // the file the icons were loaded from
volatile bool g_leaving = false;
wchar_t g_path[MAX_PATH];       // empty: no place to look (no LOCALAPPDATA)
bool g_pathMade = false;

void MakePath() {
    g_pathMade = true;
    g_path[0] = 0;
    wchar_t base[MAX_PATH], exe[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    DWORD m = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    mw::ImageNames self;
    if (!n || n >= MAX_PATH || !m || m >= MAX_PATH || !mw::MakeImageNames(exe, &self)) return;
    if (_snwprintf(g_path, MAX_PATH, L"%ls\\MacWin\\icons\\apps\\%ls.ico", base, self.baseNoExt) <= 0) g_path[0] = 0;
}

// The icons for this process now, looked at again whenever the settings change (the
// settings app publishes a new generation after it adds, replaces or removes an icon).
bool Current(HWND hWnd, HICON* big, HICON* small) {
    const SharedSettings* s = Settings();
    if (!s || g_leaving) return false;
    const uint32_t gen = s->generation;
    AcquireSRWLockShared(&g_lock);
    if (g_checked && g_generation == gen) {
        bool active = g_active;
        *big = g_big;
        *small = g_small;
        ReleaseSRWLockShared(&g_lock);
        return active;
    }
    ReleaseSRWLockShared(&g_lock);

    AcquireSRWLockExclusive(&g_lock);
    g_checked = true;
    g_generation = gen;
    g_active = false;
    if (!g_pathMade) MakePath();
    WIN32_FILE_ATTRIBUTE_DATA file;
    if (s->appIcons && EffectsActiveHere() && g_path[0] &&
        GetFileAttributesExW(g_path, GetFileExInfoStandard, &file)) {
        if (g_big && CompareFileTime(&file.ftLastWriteTime, &g_stamp) == 0) {
            g_active = true;
        } else {
            UINT dpi = GetDpiForWindow(hWnd);
            if (!dpi) dpi = 96;
            const int cxBig = GetSystemMetricsForDpi(SM_CXICON, dpi);
            const int cxSmall = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
            HICON nb = (HICON)LoadImageW(nullptr, g_path, IMAGE_ICON, cxBig, cxBig, LR_LOADFROMFILE);
            HICON ns = (HICON)LoadImageW(nullptr, g_path, IMAGE_ICON, cxSmall, cxSmall, LR_LOADFROMFILE);
            if (nb && ns) {
                // The icons replaced stay: windows may still show them until they get the new.
                g_big = nb;
                g_small = ns;
                g_stamp = file.ftLastWriteTime;
                g_active = true;
            } else {
                if (nb) DestroyIcon(nb);
                if (ns) DestroyIcon(ns);
            }
        }
    }
    bool active = g_active;
    *big = g_big;
    *small = g_small;
    ReleaseSRWLockExclusive(&g_lock);
    return active;
}

bool IsAppWindow(HWND hWnd) {
    return !(GetWindowLongPtrW(hWnd, GWL_STYLE) & WS_CHILD) && GetAncestor(hWnd, GA_ROOT) == hWnd;
}

// Our icon on the window, the program's own ones kept to give back. In the window's own
// thread only (it asks the window for its icons).
void Apply(HWND hWnd, HICON big, HICON small) {
    if (!GetPropW(hWnd, kPropOurs)) {
        SetPropW(hWnd, kPropBig, (HANDLE)SendMessageW(hWnd, WM_GETICON, ICON_BIG, 0));
        SetPropW(hWnd, kPropSmall, (HANDLE)SendMessageW(hWnd, WM_GETICON, ICON_SMALL, 0));
        SetPropW(hWnd, kPropOurs, (HANDLE)1);
    }
    SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

// The program's own icons back (null ones too: then Windows shows its class icon).
void Restore(HWND hWnd, bool post) {
    if (!GetPropW(hWnd, kPropOurs)) return;
    HICON big = (HICON)RemovePropW(hWnd, kPropBig);
    HICON small = (HICON)RemovePropW(hWnd, kPropSmall);
    RemovePropW(hWnd, kPropOurs);
    if (post) {
        PostMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        PostMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    } else {
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)big);
        SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    }
}

// Windows that were open before MacWin came: each handles the refresh in its own thread.
BOOL CALLBACK NudgeOwnWindow(HWND hWnd, LPARAM msg) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId() && IsAppWindow(hWnd)) PostMessageW(hWnd, (UINT)msg, 0, 0);
    return TRUE;
}

BOOL CALLBACK RestoreOwnWindow(HWND hWnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId()) Restore(hWnd, true);
    return TRUE;
}

}  // namespace

LPARAM IconsBeforeDefWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCDESTROY) {
        if (GetPropW(hWnd, kPropOurs)) {
            RemovePropW(hWnd, kPropBig);
            RemovePropW(hWnd, kPropSmall);
            RemovePropW(hWnd, kPropOurs);
        }
        return lParam;
    }
    if (g_leaving || !IsAppWindow(hWnd)) return lParam;
    HICON big, small;
    const bool active = Current(hWnd, &big, &small);
    if (msg == WM_SETICON) {
        if (!active) return lParam;
        HICON mine = wParam == ICON_BIG ? big : small;
        if ((HICON)lParam != big && (HICON)lParam != small) {
            // The program changed its icon: kept, ours stays on the window.
            SetPropW(hWnd, wParam == ICON_BIG ? kPropBig : kPropSmall, (HANDLE)lParam);
            SetPropW(hWnd, kPropOurs, (HANDLE)1);
        }
        return (LPARAM)mine;
    }
    if (msg == WM_SHOWWINDOW) {
        if (active && wParam && !GetPropW(hWnd, kPropOurs)) Apply(hWnd, big, small);
        return lParam;
    }
    // MACWIN_CORNERS_MESSAGE: the settings changed (the host posts it to every window after
    // a reload), or MacWin just arrived in this process.
    if (active) Apply(hWnd, big, small);
    else Restore(hWnd, false);
    return lParam;
}

void IconsOnActivate() {
    if (g_cornersMsg) EnumWindows(NudgeOwnWindow, (LPARAM)g_cornersMsg);
}

void IconsOnShutdown() {
    // From here on WM_SETICON passes untouched: the posted originals land as they are.
    g_leaving = true;
    EnumWindows(RestoreOwnWindow, 0);
}
