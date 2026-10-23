// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#include "host/injector.h"
#include "common/names.h"
#include "host/log.h"

namespace {

BOOL CALLBACK PostNullToWindow(HWND hwnd, LPARAM) {
    PostMessageW(hwnd, WM_NULL, 0, 0);
    return TRUE;
}

void NudgeAllThreads() {
    EnumWindows(PostNullToWindow, 0);
}

}  // namespace

bool Injector::Start(const std::wstring& dllPath) {
    dll_ = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll_) {
        Log(L"injector: LoadLibrary(%ls) failed: %lu", dllPath.c_str(), GetLastError());
        return false;
    }
    cbtProc_ = (HOOKPROC)GetProcAddress(dll_, MACWIN_CBT_PROC);
    getMsgProc_ = (HOOKPROC)GetProcAddress(dll_, MACWIN_GETMSG_PROC);
    if (!cbtProc_ || !getMsgProc_) {
        Log(L"injector: hook procedures not exported by %ls", dllPath.c_str());
        return false;
    }
    cbt_ = SetWindowsHookExW(WH_CBT, cbtProc_, dll_, 0);
    if (!cbt_) {
        Log(L"injector: SetWindowsHookEx(WH_CBT) failed: %lu", GetLastError());
        return false;
    }
    Log(L"injector: WH_CBT installed (%ls)", dllPath.c_str());
    return true;
}

void Injector::BeginPreload() {
    if (!getMsgProc_ || getMsg_) return;
    getMsg_ = SetWindowsHookExW(WH_GETMESSAGE, getMsgProc_, dll_, 0);
    if (!getMsg_) {
        Log(L"injector: SetWindowsHookEx(WH_GETMESSAGE) failed: %lu", GetLastError());
        return;
    }
    NudgeAllThreads();
}

void Injector::EndPreload() {
    if (getMsg_) {
        UnhookWindowsHookEx(getMsg_);
        getMsg_ = nullptr;
        Log(L"injector: preload hook removed");
    }
}

void Injector::Stop() {
    EndPreload();
    if (cbt_) {
        UnhookWindowsHookEx(cbt_);
        cbt_ = nullptr;
    }
    // Windows drops its reference to the DLL in each process the next time one of
    // its threads handles a message; nudge them all so that happens now.
    NudgeAllThreads();
    if (dll_) {
        FreeLibrary(dll_);
        dll_ = nullptr;
    }
    Log(L"injector: stopped");
}
