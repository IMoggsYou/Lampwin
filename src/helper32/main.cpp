// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

// MacWinHelper32.exe - gets MacWinHook32.dll into 32-bit programs. Started by MacWin.exe
// ("--instance XXXXXXXX") and gone together with it.
//
// Not a global hook on purpose: a global hook installed by a 32-bit process makes every
// 64-bit program call over into this process (marshaled, synchronously) on each hooked
// window event - a cost every program would pay for the few 32-bit ones. Instead, this
// watches for top-level windows appearing (out-of-context WinEvents, no injection) and
// puts a thread-specific WH_GETMESSAGE hook on one GUI thread of each 32-bit program.
// That loads the DLL in-process; its inline hooks then cover the whole program. Drawing
// stays in the 64-bit MacWin.exe.

#include <windows.h>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include "common/names.h"
#include "common/settings.h"
#include "common/strutil.h"

namespace {

const UINT WM_APP_RESCAN = WM_APP + 2;   // posted by MacWin.exe after a settings change

struct Proc {
    HANDLE process = nullptr;   // to tell a live process from a reused PID
    HHOOK hook = nullptr;       // set for 32-bit programs the DLL went into
    HHOOK cbt = nullptr;        // HCBT_MINMAX (async minimize / restore, e.g. "Show desktop")
    DWORD thread = 0;           // the hooked GUI thread
    bool is32 = false;
};

HMODULE g_dll = nullptr;
HOOKPROC g_getMsgProc = nullptr;
HOOKPROC g_cbtProc = nullptr;
DWORD g_selfPid = 0;
const SharedSettings* g_settings = nullptr;
std::unordered_map<DWORD, Proc> g_procs;

// Same rules the DLL applies in DllMain: skipping here avoids a refused load being
// retried on every message of that thread.
bool AllowedInto(HANDLE process) {
    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    mw::ImageNames names;
    if (!QueryFullProcessImageNameW(process, 0, path, &len) || !mw::MakeImageNames(path, &names)) return false;
    if (wcscmp(names.base, MACWIN_HOST_EXE) == 0 || wcscmp(names.base, MACWIN_HELPER32_EXE) == 0) return false;
    if (wcscmp(names.base, MACWIN_SETTINGS_EXE) == 0) return true;
    if (!g_settings) return false;
    static wchar_t list[MACWIN_LIST_CHARS];
    SettingsCopyList(g_settings, g_settings->noInject, list, MACWIN_LIST_CHARS);
    if (mw::ListMatches(list, names)) return false;
    if (!g_settings->devMode) return true;
    SettingsCopyList(g_settings, g_settings->devAllowlist, list, MACWIN_LIST_CHARS);
    return mw::ListMatches(list, names);
}

void Consider(HWND hwnd) {
    if (!hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (!tid || pid == g_selfPid) return;

    auto it = g_procs.find(pid);
    if (it != g_procs.end()) {
        if (WaitForSingleObject(it->second.process, 0) == WAIT_TIMEOUT) return;   // known, still running
        if (it->second.hook) UnhookWindowsHookEx(it->second.hook);
        if (it->second.cbt) UnhookWindowsHookEx(it->second.cbt);
        CloseHandle(it->second.process);
        g_procs.erase(it);   // the PID was reused by a new process
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return;   // e.g. a protected process, or elevated while we are not
    Proc p;
    p.process = process;
    p.thread = tid;
    BOOL wow64 = FALSE;
    p.is32 = IsWow64Process(process, &wow64) && wow64;
    if (p.is32 && AllowedInto(process)) {
        p.hook = SetWindowsHookExW(WH_GETMESSAGE, g_getMsgProc, g_dll, tid);
        if (p.hook && g_cbtProc) p.cbt = SetWindowsHookExW(WH_CBT, g_cbtProc, g_dll, tid);
        // Wake the thread so the DLL loads now rather than at its next message.
        if (p.hook) PostMessageW(hwnd, WM_NULL, 0, 0);
    }
    g_procs.emplace(pid, p);
}

BOOL CALLBACK ConsiderWindow(HWND hwnd, LPARAM) {
    if (IsWindowVisible(hwnd)) Consider(hwnd);
    return TRUE;
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF) Consider(hwnd);
}

// The program lists changed: 32-bit programs we skipped may be allowed now.
void Rescan() {
    for (auto it = g_procs.begin(); it != g_procs.end();) {
        if (it->second.is32 && !it->second.hook) {
            CloseHandle(it->second.process);
            it = g_procs.erase(it);
        } else {
            ++it;
        }
    }
    EnumWindows(ConsiderWindow, 0);
}

std::wstring HelperDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? dir : dir.substr(0, slash);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    unsigned instance = 0;
    const wchar_t* arg = cmdLine ? wcsstr(cmdLine, L"--instance") : nullptr;
    if (!arg || swscanf(arg + 10, L"%x", &instance) != 1) return 2;
    g_selfPid = GetCurrentProcessId();

    // Created before anything else, so MacWin.exe can post WM_APP_RESCAN right away.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    wchar_t name[96];
    swprintf(name, 96, MACWIN_SHUTDOWN_EVENT_FMT, instance);
    HANDLE shutdown = OpenEventW(SYNCHRONIZE, FALSE, name);
    swprintf(name, 96, MACWIN_ALIVE_MUTEX_FMT, instance);
    HANDLE alive = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    HANDLE section = OpenFileMappingW(FILE_MAP_READ, FALSE, MACWIN_SETTINGS_SECTION);
    if (section) {
        g_settings = (const SharedSettings*)MapViewOfFile(section, FILE_MAP_READ, 0, 0, sizeof(SharedSettings));
        if (g_settings && (g_settings->magic != MACWIN_SETTINGS_MAGIC || g_settings->size != sizeof(SharedSettings))) {
            UnmapViewOfFile(g_settings);
            g_settings = nullptr;
        }
    }
    std::wstring dll = HelperDir() + L"\\MacWinHook32.dll";
    g_dll = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    g_getMsgProc = g_dll ? (HOOKPROC)GetProcAddress(g_dll, MACWIN_GETMSG_PROC) : nullptr;
    g_cbtProc = g_dll ? (HOOKPROC)GetProcAddress(g_dll, MACWIN_CBT_PROC) : nullptr;
    if (!shutdown || !alive || !g_settings || !g_getMsgProc) return 1;

    HWINEVENTHOOK showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, 0, 0,
                                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    EnumWindows(ConsiderWindow, 0);   // programs that were already running

    HANDLE waits[2] = {shutdown, alive};
    bool running = true;
    while (running) {
        DWORD r = MsgWaitForMultipleObjects(2, waits, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 + 2) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) running = false;
                if (msg.message == WM_APP_RESCAN && !msg.hwnd) Rescan();
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }
        if (r == WAIT_OBJECT_0 + 1 || r == WAIT_ABANDONED_0 + 1) ReleaseMutex(alive);   // MacWin.exe died
        running = false;   // shutdown signaled, MacWin.exe gone, or the wait failed
    }

    // The DLLs shut themselves down on the same signal; dropping the hooks and waking the
    // threads lets Windows release its references to the DLL in those programs too.
    if (showHook) UnhookWinEvent(showHook);
    for (auto& [pid, p] : g_procs) {
        if (p.cbt) UnhookWindowsHookEx(p.cbt);
        if (p.hook) {
            UnhookWindowsHookEx(p.hook);
            PostThreadMessageW(p.thread, WM_NULL, 0, 0);
        }
        CloseHandle(p.process);
    }
    g_procs.clear();
    if (g_settings) UnmapViewOfFile(g_settings);
    if (section) CloseHandle(section);
    CloseHandle(shutdown);
    CloseHandle(alive);
    FreeLibrary(g_dll);
    return 0;
}
