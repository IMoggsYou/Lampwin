// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <vector>
#include <MinHook.h>
#include "common/names.h"
#include "common/strutil.h"
#include "hook/runtime.h"

std::atomic<bool> g_unloading{false};
std::atomic<long> g_activeCalls{0};

namespace {

enum State : int {
    kUninit = 0,   // nothing installed yet
    kIniting,      // one thread is activating
    kActive,       // hooks installed, watcher running
    kWaitHost,     // shut down cleanly; re-activates for a new host instance
    kDead,         // never activate in this process (host process, or failed unload)
};

std::atomic<int> g_state{kUninit};
bool g_isHostProcess = false;
bool g_alwaysAllowed = false;

HANDLE g_hSettingsMap = nullptr;
const SharedSettings* g_settings = nullptr;
uint32_t g_waitInstance = 0;   // host instance we shut down for (kWaitHost)
std::atomic<ULONGLONG> g_nextPeek{0};   // kWaitHost: earliest GetTickCount64 of the next look

HANDLE g_hShutdown = nullptr;  // host's per-instance manual-reset event
HANDLE g_hAlive = nullptr;     // host's per-instance mutex, abandoned if it crashes
HANDLE g_hStop = nullptr;      // local: wakes our workers at shutdown
HANDLE g_hActiveMark = nullptr;   // MACWIN_ACTIVE_FMT for this process (64-bit only)

mw::ImageNames g_self;
std::atomic<uint32_t> g_allowedGen{0xFFFFFFFFu};
std::atomic<bool> g_allowed{false};

SRWLOCK g_workersLock = SRWLOCK_INIT;
std::vector<HANDLE> g_workers;

bool MapSettings() {
    if (g_settings) return true;
    g_hSettingsMap = OpenFileMappingW(FILE_MAP_READ, FALSE, MACWIN_SETTINGS_SECTION);
    if (!g_hSettingsMap) return false;
    g_settings = (const SharedSettings*)MapViewOfFile(g_hSettingsMap, FILE_MAP_READ, 0, 0,
                                                      sizeof(SharedSettings));
    if (!g_settings || g_settings->magic != MACWIN_SETTINGS_MAGIC ||
        g_settings->size != sizeof(SharedSettings)) {
        if (g_settings) UnmapViewOfFile(g_settings);
        g_settings = nullptr;
        CloseHandle(g_hSettingsMap);
        g_hSettingsMap = nullptr;
        return false;
    }
    return true;
}

// The section is only held while active: a DLL that is shut down (but not yet unloaded
// by Windows) must not keep an elevated host's section alive, or the next host - maybe
// not elevated - could not create its own.
void UnmapSettings() {
    if (g_settings) UnmapViewOfFile(g_settings);
    if (g_hSettingsMap) CloseHandle(g_hSettingsMap);
    g_settings = nullptr;
    g_hSettingsMap = nullptr;
}

// Instance of the host that published the section right now (no lasting mapping).
bool PeekHostInstance(uint32_t* instance) {
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, MACWIN_SETTINGS_SECTION);
    if (!map) return false;
    auto* s = (const SharedSettings*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(SharedSettings));
    bool ok = s && s->magic == MACWIN_SETTINGS_MAGIC && s->size == sizeof(SharedSettings);
    if (ok) *instance = s->hostInstance;
    if (s) UnmapViewOfFile(s);
    CloseHandle(map);
    return ok;
}

bool WorkersFinished() {
    AcquireSRWLockShared(&g_workersLock);
    bool done = true;
    for (HANDLE h : g_workers) {
        if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) {
            done = false;
            break;
        }
    }
    ReleaseSRWLockShared(&g_workersLock);
    return done;
}

void CloseWorkers() {
    AcquireSRWLockExclusive(&g_workersLock);
    for (HANDLE h : g_workers) CloseHandle(h);
    g_workers.clear();
    g_workers.shrink_to_fit();
    ReleaseSRWLockExclusive(&g_workersLock);
}

void CloseActiveMark() {
    if (g_hActiveMark) CloseHandle(g_hActiveMark);
    g_hActiveMark = nullptr;
}

void CloseHostHandles() {
    CloseActiveMark();
    if (g_hShutdown) CloseHandle(g_hShutdown);
    if (g_hAlive) CloseHandle(g_hAlive);
    if (g_hStop) CloseHandle(g_hStop);
    g_hShutdown = g_hAlive = g_hStop = nullptr;
}

DWORD WINAPI WatcherThread(LPVOID);

// Switches every hook off and waits (bounded) until no thread runs hook code or a worker
// any more. False if either fails: the DLL must then stay loaded, its hooks passing
// straight through (g_unloading).
bool DisableAndDrain() {
    g_unloading.store(true);
    CloseActiveMark();   // other processes stop handing their minimizes to us
    if (g_hStop) SetEvent(g_hStop);
    // MinHook fails only if it cannot write the code back (or freeze the threads for
    // it); a hook that stays on keeps passing through, so trying again is safe.
    bool off = false;
    for (int i = 0; i < 5 && !off; ++i) {
        off = MH_DisableHook(MH_ALL_HOOKS) == MH_OK;
        if (!off) Sleep(20);
    }
    if (!off) {
        DbgLog(L"could not disable the hooks; staying loaded");
        return false;
    }

    // Wait for threads still inside hook code or a worker. A thread can sit inside a
    // hooked call for a long time; if it never leaves, the DLL just stays loaded with
    // every hook off - never unmap code that is still on a stack.
    bool drained = false;
    for (int i = 0; i < 500 && !drained; ++i) {   // ~10 s
        if (g_activeCalls.load() == 0 && WorkersFinished()) {
            // Grace period for a thread that jumped into a detour but has not yet
            // reached its HookScope.
            Sleep(50);
            drained = g_activeCalls.load() == 0 && WorkersFinished();
        }
        if (!drained) Sleep(20);
    }
    if (!drained) DbgLog(L"could not drain hook calls; staying loaded (hooks disabled)");
    return drained;
}

State Activate() {
    if (!MapSettings()) return kWaitHost;

    const uint32_t instance = g_settings->hostInstance;
    // Any failure below waits for the next host instance instead of retrying on
    // every hook call.
    g_waitInstance = instance;
    wchar_t name[96];
    swprintf(name, 96, MACWIN_SHUTDOWN_EVENT_FMT, instance);
    g_hShutdown = OpenEventW(SYNCHRONIZE, FALSE, name);
    swprintf(name, 96, MACWIN_ALIVE_MUTEX_FMT, instance);
    g_hAlive = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    g_hStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hShutdown || !g_hAlive || !g_hStop) {
        CloseHostHandles();
        g_waitInstance = instance;
        return kWaitHost;
    }
    // Host already gone or going: don't install anything for it.
    bool hostGone = WaitForSingleObject(g_hShutdown, 0) == WAIT_OBJECT_0;
    if (!hostGone) {
        DWORD r = WaitForSingleObject(g_hAlive, 0);
        if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
            ReleaseMutex(g_hAlive);
            hostGone = true;
        }
    }
    if (hostGone) {
        CloseHostHandles();
        g_waitInstance = instance;
        return kWaitHost;
    }

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    mw::MakeImageNames(path, &g_self);
    g_allowedGen.store(0xFFFFFFFFu);

    // Our own module reference: the watcher drops it (FreeLibraryAndExitThread) only
    // after every hook has been removed and every thread has left our code. Windows
    // holds a separate reference for the window hook itself.
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&Activate, &self)) {
        CloseHostHandles();
        return kWaitHost;
    }

    g_unloading.store(false);
    if (MH_Initialize() != MH_OK) {
        CloseHostHandles();
        FreeLibrary(self);   // Windows' own reference keeps us mapped
        return kWaitHost;
    }
    bool ok = GenieInstallHooks() && CornersInstallHooks() && MH_QueueEnableHook(MH_ALL_HOOKS) == MH_OK;
    bool live = false;   // hooks may be on - and running in other threads - from here on
    if (ok) {
        live = true;
        ok = MH_ApplyQueued() == MH_OK;
    }
    HANDLE hWatcher = ok ? CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr) : nullptr;
    if (!hWatcher) {
        // Undone like a shutdown (a window may have been cut meanwhile). If a hook cannot
        // be taken out (or a thread does not leave our code), our own module reference
        // is kept for good: the detours stay valid and pass straight through.
        const bool cornersGone = !live || CornersOnShutdown();
        if ((live && !DisableAndDrain()) || !cornersGone || MH_Uninitialize() != MH_OK) {
            g_unloading.store(true);
            DbgLog(L"activation failed and could not be undone; staying loaded (hooks pass through)");
            return kDead;
        }
        GenieOnShutdown();
        CloseWorkers();
        CloseHostHandles();
        FreeLibrary(self);   // Windows' own reference keeps us mapped
        return kWaitHost;
    }
    CloseHandle(hWatcher);
    CornersOnActivate();
#ifdef _WIN64
    // The global WH_CBT hook reaches every thread here, so HCBT_MINMAX animates any of our
    // windows whoever minimizes it (see PeerAnimatesItself). The 32-bit helper hooks one
    // thread per program, so 32-bit programs leave it to the caller.
    swprintf(name, 96, MACWIN_ACTIVE_FMT, instance, GetCurrentProcessId());
    g_hActiveMark = CreateEventW(nullptr, TRUE, FALSE, name);
#endif
    DbgLog(L"active (host instance %08X)", instance);
    return kActive;
}

// Returns true when everything was removed and the module may be released. Otherwise
// the DLL stays loaded for good (kDead) with every hook passing straight through.
bool Shutdown() {
    // Needs the hooks: each window's thread undoes its own region and destroys its grip.
    const bool cornersGone = CornersOnShutdown();
    if (!DisableAndDrain()) {
        g_state.store(kDead);
        return false;
    }
    if (!cornersGone) {
        DbgLog(L"a corner grip is left (hung window); staying loaded (hooks disabled)");
        g_state.store(kDead);
        return false;
    }
    // Frees the trampolines: only once nothing can be in them (drained above).
    if (MH_Uninitialize() != MH_OK) {
        DbgLog(L"MH_Uninitialize failed; staying loaded (hooks disabled)");
        g_state.store(kDead);
        return false;
    }
    GenieOnShutdown();
    CloseWorkers();
    g_waitInstance = g_settings ? g_settings->hostInstance : 0;
    CloseHostHandles();
    UnmapSettings();
    g_state.store(kWaitHost, std::memory_order_release);
    return true;
}

DWORD WINAPI WatcherThread(LPVOID) {
    HANDLE handles[2] = {g_hShutdown, g_hAlive};
    DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0 + 1 || r == WAIT_ABANDONED_0 + 1) {
        ReleaseMutex(g_hAlive);   // host died: let the other processes see it too
    }
    if (Shutdown()) FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

}  // namespace

void LoaderSetHostProcess(bool isHost) {
    g_isHostProcess = isHost;
}

void LoaderSetAlwaysAllowed(bool allowed) {
    g_alwaysAllowed = allowed;
}

void LoaderOnProcessDetach() {
    // Dynamic unload (Windows dropped the hook, our reference is gone): hooks are
    // already off. Only the settings view may still be mapped.
    UnmapSettings();
}

void EnsureInit() {
    int s = g_state.load(std::memory_order_acquire);
    if (s == kActive || s == kIniting || s == kDead) return;
    if (s == kWaitHost) {
        // Re-activate only once a new host instance has published its settings. Hook
        // calls come all the time (activations, clicks, ...): one look a second will do.
        const ULONGLONG now = GetTickCount64();
        ULONGLONG next = g_nextPeek.load(std::memory_order_relaxed);
        if (now < next || !g_nextPeek.compare_exchange_strong(next, now + 1000, std::memory_order_relaxed)) return;
        uint32_t instance = 0;
        if (!PeekHostInstance(&instance) || instance == g_waitInstance) return;
    }
    if (g_isHostProcess) {
        g_state.store(kDead);
        return;
    }
    if (!g_state.compare_exchange_strong(s, kIniting, std::memory_order_acq_rel)) return;
    const State result = Activate();
    if (result == kWaitHost) UnmapSettings();   // kDead keeps everything: hooks may still run
    g_state.store(result, std::memory_order_release);
}

const SharedSettings* Settings() {
    return g_settings;
}

bool EffectsActiveHere() {
    const SharedSettings* s = g_settings;
    if (!s || !s->enabled || g_unloading.load(std::memory_order_relaxed)) return false;
    if (g_alwaysAllowed) return true;
    // The lists can change while we are loaded: re-evaluate once per publish.
    uint32_t gen = s->generation;
    if (g_allowedGen.load(std::memory_order_acquire) != gen) {
        static wchar_t list[MACWIN_LIST_CHARS];
        static SRWLOCK lock = SRWLOCK_INIT;
        AcquireSRWLockExclusive(&lock);
        SettingsCopyList(s, s->noInject, list, MACWIN_LIST_CHARS);
        bool allowed = !mw::ListMatches(list, g_self);
        if (allowed && s->devMode) {
            SettingsCopyList(s, s->devAllowlist, list, MACWIN_LIST_CHARS);
            allowed = mw::ListMatches(list, g_self);
        }
        g_allowed.store(allowed, std::memory_order_relaxed);
        g_allowedGen.store(gen, std::memory_order_release);
        ReleaseSRWLockExclusive(&lock);
    }
    return g_allowed.load(std::memory_order_relaxed);
}

HANDLE LocalStopEvent() {
    return g_hStop;
}

bool PeerAnimatesItself(DWORD pid) {
    const SharedSettings* s = g_settings;
    if (!s || !pid) return false;
    wchar_t name[96];
    swprintf(name, 96, MACWIN_ACTIVE_FMT, s->hostInstance, pid);
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!h) return false;   // also an elevated process we may not open: keep doing it here
    CloseHandle(h);
    return true;
}

bool SpawnWorker(LPTHREAD_START_ROUTINE proc, void* param, bool suspended, HANDLE* outThread) {
    AcquireSRWLockExclusive(&g_workersLock);
    if (g_unloading.load()) {
        ReleaseSRWLockExclusive(&g_workersLock);
        return false;
    }
    for (size_t i = g_workers.size(); i-- > 0;) {
        if (WaitForSingleObject(g_workers[i], 0) == WAIT_OBJECT_0) {
            CloseHandle(g_workers[i]);
            g_workers.erase(g_workers.begin() + i);
        }
    }
    HANDLE h = CreateThread(nullptr, 0, proc, param, suspended ? CREATE_SUSPENDED : 0, nullptr);
    if (h) g_workers.push_back(h);
    ReleaseSRWLockExclusive(&g_workersLock);
    if (outThread) *outThread = h;
    return h != nullptr;
}

#ifdef MACWIN_DEBUG_LOG
void DbgLog(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    int n = swprintf(buf, 512, L"[MacWin %lu] ", GetCurrentProcessId());
    if (n < 0) n = 0;
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf + n, 512 - n - 2, fmt, ap);
    va_end(ap);
    wcscat(buf, L"\n");
    OutputDebugStringW(buf);
}
#endif
