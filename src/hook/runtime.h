// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <atomic>
#include "common/ipc.h"
#include "common/settings.h"

// Shared by the pieces of the hook DLL.

extern HMODULE g_hModule;

// Set while the DLL is shutting down: hooks pass straight through, workers wind down.
extern std::atomic<bool> g_unloading;

// Threads currently executing hook code (see HookScope). The DLL only unloads once
// this is zero, so no stack ever returns into unmapped code.
extern std::atomic<long> g_activeCalls;

// Guaranteed tail call (the pass-through fast paths leave no frame of ours on the stack).
#if defined(__clang__)
#define MW_MUSTTAIL [[clang::musttail]]
#else
#define MW_MUSTTAIL [[gnu::musttail]]
#endif

struct HookScope {
    HookScope() { g_activeCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~HookScope() { g_activeCalls.fetch_sub(1, std::memory_order_acq_rel); }
    HookScope(const HookScope&) = delete;
    HookScope& operator=(const HookScope&) = delete;
};

// loader.cpp
void LoaderSetHostProcess(bool isHost);
void LoaderSetAlwaysAllowed(bool allowed);   // ignore the program lists in this process
void LoaderOnProcessDetach();
void EnsureInit();
const SharedSettings* Settings();   // valid while hooks are installed
bool EffectsActiveHere();           // enabled, not unloading, process still allowed
HANDLE LocalStopEvent();            // manual-reset, set when the DLL starts shutting down
// True if process `pid` runs the 64-bit DLL for the same host: its own HCBT_MINMAX
// animates its windows, whichever thread minimizes them.
bool PeerAnimatesItself(DWORD pid);
// Starts a tracked worker thread (joined before unload). Refused while unloading.
bool SpawnWorker(LPTHREAD_START_ROUTINE proc, void* param, bool suspended, HANDLE* outThread);
// OutputDebugString; compiled in only with MACWIN_DEBUG_LOG (no strings, no calls otherwise).
#ifdef MACWIN_DEBUG_LOG
void DbgLog(const wchar_t* fmt, ...);
#else
inline void DbgLog(const wchar_t*, ...) {}
#endif

// hooks.cpp
bool CreateUser32Hook(const char* name, void* detour, void** original);

// capture.cpp: window-only capture (PrintWindow) into a new pagefile section.
HANDLE CaptureToSection(HWND hWnd, int rawW, int rawH);

// ipc_client.cpp
HWND HostIpcWindow();
uint32_t SendAnimRequest(HWND hHost, const AnimRequest& req);

// effects/genie_hooks.cpp
bool GenieInstallHooks();
void GenieOnShutdown();
// HCBT_MINMAX: called in the window's own thread right before it is minimized / restored,
// whoever asked for it (also ShowWindowAsync from Explorer's "Show desktop").
void GenieOnMinMax(HWND hWnd, int showCmd);

// effects/corners.cpp: window corners as a window region (see there).
typedef LRESULT(WINAPI* DefWindowProcFn)(HWND, UINT, WPARAM, LPARAM);
extern UINT g_cornersMsg;   // MACWIN_CORNERS_MESSAGE once active
inline bool CornersInteresting(UINT msg) {
    return msg == WM_WINDOWPOSCHANGED || msg == WM_EXITSIZEMOVE || msg == WM_NCDESTROY || msg == g_cornersMsg;
}
// DefWindowProc for the messages CornersInteresting picks out.
LRESULT CornersDefWindowProc(DefWindowProcFn original, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// effects/icons.cpp: app icons the user chose (see there). Runs before DefWindowProc and
// returns the lParam to pass on: the chosen icon for WM_SETICON, else lParam unchanged.
inline bool IconsInteresting(UINT msg) {
    return msg == WM_SETICON || msg == WM_SHOWWINDOW || msg == WM_NCDESTROY || (g_cornersMsg && msg == g_cornersMsg);
}
LPARAM IconsBeforeDefWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void IconsOnActivate();   // windows that were open before MacWin came get theirs too
void IconsOnShutdown();   // every window gets the program's own icons back
bool CornersInstallHooks();  // DwmSetWindowAttribute (created, enabled with the rest)
void CornersOnActivate();   // after the hooks are on
// Before they go. False if a window of ours (a grip, which runs DLL code) is left over:
// the DLL must then stay loaded.
bool CornersOnShutdown();
// The genie cloaked / uncloaked a window: its shade (shadow + edge) follows (any thread).
void CornersOnCloak(HWND hWnd, bool cloaked);
