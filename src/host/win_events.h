// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#pragma once
#include <windows.h>
#include <functional>

// Out-of-context WinEvents (no injection; delivered to the host's main thread):
//  - a destroyed window drops its cached snapshot and learned icon position,
//  - taskbar-relevant windows appearing / disappearing mark the taskbar list stale.
class WinEvents {
public:
    struct Callbacks {
        std::function<void(HWND)> destroyed;
        std::function<void()> taskbarChanged;
    };
    bool Start(Callbacks cb);
    void Stop();

private:
    static void CALLBACK Proc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    HWINEVENTHOOK hook_ = nullptr;
};
