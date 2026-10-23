// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

#include <windows.h>
#include "hook/runtime.h"

HWND HostIpcWindow() {
    const SharedSettings* s = Settings();
    if (!s) return nullptr;
    HWND h = (HWND)(uintptr_t)s->hostIpcHwnd;
    return (h && IsWindow(h)) ? h : nullptr;
}

// Synchronous: the host only duplicates handles, uploads the snapshot and queues the
// animation before replying, so this returns within a couple of milliseconds.
// SMTO_BLOCK keeps this (usually UI) thread from dispatching unrelated sent messages
// while it waits, so nothing re-enters the window procedure mid-request.
uint32_t SendAnimRequest(HWND hHost, const AnimRequest& req) {
    COPYDATASTRUCT cds;
    cds.dwData = MW_IPC_ANIM;
    cds.cbData = sizeof(req);
    cds.lpData = (void*)&req;
    DWORD_PTR result = MW_REPLY_FAIL;
    if (!SendMessageTimeoutW(hHost, WM_COPYDATA, 0, (LPARAM)&cds,
                             SMTO_BLOCK | SMTO_ABORTIFHUNG, 150, &result)) {
        return MW_REPLY_FAIL;
    }
    return (uint32_t)result;
}
