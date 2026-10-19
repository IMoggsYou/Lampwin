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
void DbgLog(const wchar_t* fmt, ...);

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
