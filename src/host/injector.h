// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>

// Gets MacWinHook64.dll into GUI processes via SetWindowsHookEx. Windows then calls our
// hook procedures on the thread that installed them for processes of the other bitness,
// so that thread (the host's main thread) must keep pumping messages.
class Injector {
public:
    bool Start(const std::wstring& dllPath);
    // One-off WH_GETMESSAGE + WM_NULL to every top-level window, so programs that are
    // already running load the DLL now rather than at their next window event.
    void BeginPreload();
    void EndPreload();
    void Stop();

private:
    HMODULE dll_ = nullptr;
    HOOKPROC cbtProc_ = nullptr;
    HOOKPROC getMsgProc_ = nullptr;
    HHOOK cbt_ = nullptr;
    HHOOK getMsg_ = nullptr;
};
