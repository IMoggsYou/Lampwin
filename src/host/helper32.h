// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

// Runs MacWinHelper32.exe (gets MacWinHook32.dll into 32-bit programs) for as long as the
// host runs, with the host's own rights (so elevated 32-bit programs are covered when the
// host is elevated). Restarted if it dies; it exits on its own at host shutdown.
class Helper32 {
public:
    // `notify` gets `exitedMsg` posted when the helper exits unexpectedly.
    bool Start(const std::wstring& exe, uint32_t hostInstance, HWND notify, UINT exitedMsg);
    void OnExited();   // call on `exitedMsg`: restarts (a few times) unless stopping
    void Rescan();     // program lists changed
    void Stop();       // after the shutdown event is set

private:
    bool Launch();
    static void CALLBACK OnProcessExit(void* self, BOOLEAN);

    std::wstring exe_;
    uint32_t instance_ = 0;
    HWND notify_ = nullptr;
    UINT exitedMsg_ = 0;
    HANDLE process_ = nullptr;
    HANDLE wait_ = nullptr;
    DWORD thread_ = 0;
    int restarts_ = 0;
    bool stopping_ = false;
};
