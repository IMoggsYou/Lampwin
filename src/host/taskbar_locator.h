// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

#pragma once
#include <windows.h>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "host/render/genie_effect.h"

// Finds the taskbar button of a window, so the genie lands right in its icon.
//
// Windows 11 names each taskbar button after the app it groups: its UI Automation id is
// "Appid: <AppUserModelID>". A window's AppUserModelID is its explicit one (window
// property), its package's (Store apps), or the implicit one the shell derives from the
// exe path - so matching on it finds exactly the right button. Matching by title /
// process name (Potassiumuncher's scoring from the mod) is only the fallback.
//
// UI Automation never runs on the app's UI thread at minimize time: the buttons (id,
// name, rect) are cached and refreshed on a background MTA thread whenever taskbar-
// relevant windows appear or disappear (debounced); a minimize matches against the cache.
class TaskbarLocator {
public:
    bool Start();
    void Stop();

    // Decides now if it can: the button under `click` (a press on the taskbar, null for a
    // title-bar / keyboard minimize), or a strong match in the fresh cached list.
    bool TryResolve(HWND hwnd, HMONITOR hMon, const POINT* click, DockTarget* out);
    // Refreshes the list if needed, then calls done(target) on the locator thread.
    void ResolveAsync(HWND hwnd, HMONITOR hMon, DockTarget fallback, std::function<void(DockTarget)> done);
    // Per-window, then per-process learned target, else defaultX on the taskbar.
    DockTarget Fallback(HWND hwnd, HMONITOR hMon, int defaultX);
    void Invalidate();
    void Forget(HWND hwnd);

private:
    struct Button {
        std::wstring name;    // lowercase
        std::wstring aumid;   // lowercase AppUserModelID; empty on non-app buttons
        RECT rc;
        HWND tray;
    };
    struct WinInfo {
        std::wstring title, proc, hint, processKey;
        std::vector<std::wstring> aumids;   // lowercase candidates, see Describe
    };
    struct Request {
        HWND hwnd;
        HMONITOR hMon;
        DockTarget fallback;
        std::function<void(DockTarget)> done;
    };

    void ThreadMain();
    bool InitUia();
    void ReleaseUia();
    void Refresh();
    WinInfo Describe(HWND hwnd, HMONITOR hMon) const;
    bool MatchLocked(const WinInfo& w, HMONITOR hMon, DockTarget* out, int* score) const;
    const Button* ButtonAtLocked(POINT pt) const;
    void LearnLocked(HWND hwnd, const WinInfo& w, const DockTarget& t, bool perProcess);
    DockTarget FallbackLocked(HWND hwnd, const WinInfo& w, const DockTarget& def) const;
    void Resolve(Request& r, bool* refreshed);

    std::thread thread_;
    HANDLE wake_ = nullptr;
    volatile bool stop_ = false;

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Button> buttons_;
    bool valid_ = false;
    volatile bool dirty_ = true;
    ULONGLONG refreshDue_ = 0;   // GetTickCount64 of the debounced refresh, 0 = none
    std::unordered_map<HWND, DockTarget> perWindow_;
    std::unordered_map<std::wstring, DockTarget> perProcess_;
    std::deque<Request> queue_;

    // UIA objects (locator thread only).
    void* uia_ = nullptr;
    void* condition_ = nullptr;
    void* cacheRequest_ = nullptr;
};
