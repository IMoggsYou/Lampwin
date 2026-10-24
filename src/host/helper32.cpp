// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/helper32.h"
#include <vector>
#include "host/log.h"

namespace {
const UINT WM_APP_RESCAN = WM_APP + 2;   // see src/helper32/main.cpp
const int kMaxRestarts = 3;
}  // namespace

bool Helper32::Start(const std::wstring& exe, uint32_t hostInstance, HWND notify, UINT exitedMsg) {
    exe_ = exe;
    instance_ = hostInstance;
    notify_ = notify;
    exitedMsg_ = exitedMsg;
    if (GetFileAttributesW(exe_.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"helper32: %ls not found, 32-bit programs keep the normal animations", exe_.c_str());
        return false;
    }
    return Launch();
}

bool Helper32::Launch() {
    wchar_t args[64];
    swprintf(args, 64, L" --instance %08X", instance_);
    std::wstring cmd = L"\"" + exe_ + L"\"" + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe_.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        Log(L"helper32: CreateProcess failed: %lu", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    thread_ = pi.dwThreadId;
    RegisterWaitForSingleObject(&wait_, process_, OnProcessExit, this, INFINITE, WT_EXECUTEONLYONCE);
    Log(L"helper32: started (pid %lu)", pi.dwProcessId);
    return true;
}

void CALLBACK Helper32::OnProcessExit(void* self, BOOLEAN) {
    auto* h = (Helper32*)self;
    if (!h->stopping_) PostMessageW(h->notify_, h->exitedMsg_, 0, 0);
}

void Helper32::OnExited() {
    if (stopping_ || !process_) return;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    if (wait_) UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
    wait_ = nullptr;
    CloseHandle(process_);
    process_ = nullptr;
    if (++restarts_ > kMaxRestarts) {
        Log(L"helper32: exited (code %lu) too often, not restarting", code);
        return;
    }
    Log(L"helper32: exited unexpectedly (code %lu), restarting", code);
    Launch();
}

void Helper32::Rescan() {
    if (process_) PostThreadMessageW(thread_, WM_APP_RESCAN, 0, 0);
}

void Helper32::Stop() {
    stopping_ = true;
    if (wait_) UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
    wait_ = nullptr;
    if (process_) {
        // It leaves on the shutdown event; it holds no state worth waiting long for.
        if (WaitForSingleObject(process_, 3000) == WAIT_TIMEOUT) TerminateProcess(process_, 0);
        CloseHandle(process_);
        process_ = nullptr;
    }
}
