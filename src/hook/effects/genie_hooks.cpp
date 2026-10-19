// Genie minimize / restore: the decision, hide and reveal logic of the Windhawk mod
// "MacOS Minimize Animation" v3.1.7 by Abdullah Masood (MIT), including Potassiumuncher's
// genie engine - see THIRD_PARTY_NOTICES.md. Everything that decides WHEN to animate and
// how the real window is hidden / revealed is kept from the mod. What changed:
//  - rendering moved out of the app into the MacWin host (GPU, see src/host/render);
//    StartMacGenieAnim now sends an AnimRequest instead of spawning a draw thread,
//  - the snapshot cache and the taskbar-button lookup (UIA) live in the host,
//  - settings come from the shared section, hooks have pass-through fast paths,
//  - DefWindowProcA is hooked too, so ANSI windows animate as well.

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <unordered_map>
#include <unordered_set>
#include "common/strutil.h"
#include "hook/runtime.h"

namespace {

typedef LRESULT(WINAPI* DefWindowProc_t)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL(WINAPI* ShowWindow_t)(HWND, int);
typedef BOOL(WINAPI* SetWindowPlacement_t)(HWND, const WINDOWPLACEMENT*);
typedef BOOL(WINAPI* CloseWindow_t)(HWND);
typedef BOOL(WINAPI* SetWindowPos_t)(HWND, HWND, int, int, int, int, UINT);

DefWindowProc_t DefWindowProcW_Original;
DefWindowProc_t DefWindowProcA_Original;
ShowWindow_t ShowWindow_Original;
ShowWindow_t ShowWindowAsync_Original;
SetWindowPlacement_t SetWindowPlacement_Original;
CloseWindow_t CloseWindow_Original;
SetWindowPos_t SetWindowPos_Original;

// Set on a window while our own deferred minimize (auto-hide path) runs through
// DefWindowProc, so it is not animated a second time.
const wchar_t kBypassProp[] = L"MacWinGenieBypass";

SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_set<HWND> g_LaunchSeen;   // windows already shown / animated once
// Latest genie started from this process per window. A request in the SAME direction
// while one is in flight is a duplicate (e.g. two hooked paths for one minimize); the
// OPPOSITE direction goes through - the host turns the running genie around.
struct ActiveAnim {
    uint64_t token;
    BOOL rising;
};
std::unordered_map<HWND, ActiveAnim> g_AnimActive;
uint64_t g_nextToken = 0;

// Set while one of our own hooked calls (ShowWindow, SC_MINIMIZE, ...) changes a window's
// state on this thread: the HCBT_MINMAX that comes out of it is handled already.
thread_local int t_ownStateChange = 0;
struct OwnStateChange {
    OwnStateChange() { ++t_ownStateChange; }
    ~OwnStateChange() { --t_ownStateChange; }
};

// Which path started a request (shows up in MacWin's log).
thread_local uint32_t t_source = MW_SRC_UNKNOWN;
struct SourceScope {
    uint32_t old;
    explicit SourceScope(uint32_t s) : old(t_source) { t_source = s; }
    ~SourceScope() { t_source = old; }
};

struct Locked {
    Locked() { AcquireSRWLockExclusive(&g_lock); }
    ~Locked() { ReleaseSRWLockExclusive(&g_lock); }
};

int DurationMs() { return Settings()->genieDurationMs; }
bool RestoreEnabled() { return Settings()->genieRestore != 0; }
bool LaunchEnabled() { return Settings()->genieLaunch != 0; }
bool UnhideEnabled() { return Settings()->genieUnhideTaskbar != 0; }
int UnhideDurationMs() { return Settings()->genieUnhideDurationMs; }

bool IsMinimizeCmd(int cmd) {
    return cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE;
}

// The mod's "Excluded programs": matched against the process that OWNS the window,
// because the hooks animate a window from whichever process manipulates it.
bool MacGenieIsExcluded(HWND hWnd) {
    static wchar_t list[MACWIN_LIST_CHARS];
    static SRWLOCK listLock = SRWLOCK_INIT;
    const SharedSettings* s = Settings();
    if (!s->genieExcluded[0]) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    mw::ImageNames names;
    if (!mw::ProcessImageNames(pid, &names)) return false;
    AcquireSRWLockExclusive(&listLock);
    SettingsCopyList(s, s->genieExcluded, list, MACWIN_LIST_CHARS);
    bool excluded = mw::ListMatches(list, names);
    ReleaseSRWLockExclusive(&listLock);
    return excluded;
}

void MacGenieSetDwmTransitions(HWND hWnd, BOOL enable) {
    BOOL disable = !enable;
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
}

// DWM-level hide: the window stays "visible" to Win32 and keeps painting, it is just
// not composed - so a restore happens invisibly under the genie, without a flash.
void MacGenieSetCloak(HWND hWnd, BOOL cloak) {
    DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
}

void MacGenieUndoRisingHide(HWND hWnd, LONG_PTR originalExStyle, BOOL cloakHidden) {
    if (cloakHidden) {
        MacGenieSetCloak(hWnd, FALSE);
    } else {
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
        if (!(originalExStyle & WS_EX_LAYERED)) {
            SetWindowLongPtrW(hWnd, GWL_EXSTYLE, originalExStyle);
        }
    }
    MacGenieSetDwmTransitions(hWnd, TRUE);
}

// Only real top-level windows of a reasonable size.
bool MacGenieShouldAnimate(HWND hWnd) {
    if (!hWnd) return false;
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (style & WS_CHILD) return false;
    RECT r;
    if (IsIconic(hWnd)) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hWnd, &wp)) return false;
        r = wp.rcNormalPosition;
    } else if (!GetWindowRect(hWnd, &r)) {
        return false;
    }
    return (r.right - r.left) >= 40 && (r.bottom - r.top) >= 40;
}

// A top-level window with a title bar (no child / tool / popup / splash windows).
bool MacGenieIsLaunchWindow(HWND hWnd) {
    if (!hWnd) return false;
    if (GetAncestor(hWnd, GA_ROOT) != hWnd) return false;
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return false;
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    return MacGenieShouldAnimate(hWnd);
}

// Geometry and capture in physical pixels whatever the app's own DPI awareness, so the
// crop fractions and the host's own (per-monitor aware) rects describe the same pixels.
struct DpiScope {
    DPI_AWARENESS_CONTEXT old;
    DpiScope() : old(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~DpiScope() {
        if (old) SetThreadDpiAwarenessContext(old);
    }
};

// Everything the worker needs to finish an animation once the host is done with it
// (the mod's COMMON TEARDOWN, minus the rendering resources).
struct GenieTeardown {
    HWND hRealWnd;
    BOOL isRising;
    LONG_PTR originalExStyle;
    BOOL hiddenByCloak;
    BOOL deferredMinimize;
    BOOL requestedUnhide;
    HWND hNextApp;
    int durationMs;
    int unhideDurationMs;
    HANDLE hDone;
    HANDLE hRevealed;
    uint64_t token;
    volatile LONG cancelled;   // request failed: exit without touching anything
};

DWORD WINAPI GenieTeardownThread(LPVOID param) {
    GenieTeardown* d = (GenieTeardown*)param;
    if (d->cancelled) {
        delete d;
        return 0;
    }

    // The host signals hDone right after the last frame. The bound only matters if
    // the host dies mid-animation: the real window must never stay hidden.
    HANDLE waits[2] = {d->hDone, LocalStopEvent()};
    WaitForMultipleObjects(2, waits, FALSE, (DWORD)d->durationMs + 3000);

    if (d->isRising && IsWindow(d->hRealWnd)) {
        // Reveal the real window the same way it was hidden.
        if (d->hiddenByCloak) {
            MacGenieSetCloak(d->hRealWnd, FALSE);
        } else {
            SetLayeredWindowAttributes(d->hRealWnd, 0, 255, LWA_ALPHA);
            if (!(d->originalExStyle & WS_EX_LAYERED)) {
                SetWindowLongPtrW(d->hRealWnd, GWL_EXSTYLE, d->originalExStyle);
            }
        }
    }
    // The host keeps its last frame up until it sees this, so there is no gap
    // between the genie and the real window.
    if (d->hRevealed) SetEvent(d->hRevealed);

    // The auto-hide deferred path keeps DWM transitions off through the real
    // (deferred) minimize below; every other path re-enables them now.
    if (!d->deferredMinimize) {
        MacGenieSetDwmTransitions(d->hRealWnd, TRUE);
    }

    // Auto-hide "unhide" completion (Potassiumuncher's engine): the caller cloaked the
    // window after our first frame and deferred the real minimize to us, so the
    // revealed taskbar did not slide away mid-animation.
    if (d->deferredMinimize) {
        if (d->requestedUnhide && d->unhideDurationMs > d->durationMs) {
            int remaining = d->unhideDurationMs - d->durationMs;
            while (remaining > 0 && !g_unloading.load(std::memory_order_relaxed)) {
                int chunk = remaining < 20 ? remaining : 20;
                Sleep(chunk);
                remaining -= chunk;
            }
        }
        if (!g_unloading.load(std::memory_order_relaxed) && IsWindow(d->hRealWnd)) {
            SetPropW(d->hRealWnd, kBypassProp, (HANDLE)1);
            SendMessageTimeoutW(d->hRealWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0, SMTO_NORMAL, 1000, nullptr);
            RemovePropW(d->hRealWnd, kBypassProp);
        }
        // Never leave an invisible, non-minimized window behind.
        if (IsWindow(d->hRealWnd)) {
            MacGenieSetCloak(d->hRealWnd, FALSE);
            MacGenieSetDwmTransitions(d->hRealWnd, TRUE);
        }
        // Shift focus so the taskbar slides back down natively.
        if (d->requestedUnhide && !g_unloading.load(std::memory_order_relaxed)) {
            if (d->hNextApp && IsWindow(d->hNextApp) && IsWindowVisible(d->hNextApp)) {
                SetForegroundWindow(d->hNextApp);
            } else {
                SetForegroundWindow(FindWindowW(L"Progman", nullptr));
            }
        }
    }

    {
        Locked l;
        auto it = g_AnimActive.find(d->hRealWnd);
        if (it != g_AnimActive.end() && it->second.token == d->token) g_AnimActive.erase(it);
    }
    CloseHandle(d->hDone);
    if (d->hRevealed) CloseHandle(d->hRevealed);
    delete d;
    return 0;
}

// Returns true if the host took the animation. `originalExStyle` is the window's TRUE
// extended style captured before any WS_EX_LAYERED was added for a hide; `cloakHidden`
// says how a rising caller hid the window. The deferred* args drive the auto-hide
// path (falling only): the caller cloaks after our first frame and the worker does
// the real minimize at the end.
bool StartMacGenieAnim(HWND hWnd, BOOL rising, LONG_PTR originalExStyle,
                       BOOL cloakHidden = FALSE, BOOL deferredMinimize = FALSE,
                       BOOL requestedUnhide = FALSE, HWND hNextApp = nullptr) {
    auto bail = [&]() -> bool {
        if (rising) MacGenieUndoRisingHide(hWnd, originalExStyle, cloakHidden);
        else MacGenieSetDwmTransitions(hWnd, TRUE);
        return false;
    };

    if (MacGenieIsExcluded(hWnd)) return bail();
    HWND hHost = HostIpcWindow();
    if (!hHost) return bail();

    // DWM extended frame bounds: GetWindowRect includes the invisible resize borders,
    // the extended frame is where the pixels really are. The snapshot is the full
    // window rect; the host crops it to the extended frame. The crop is computed in
    // physical pixels (extended frame bounds are always physical) and sent as
    // fractions, so it also fits a snapshot PrintWindow renders at a DPI-virtualized
    // size. The capture itself runs in the thread's own DPI context.
    RECT physRect, extRect;
    {
        DpiScope dpi;
        if (!GetWindowRect(hWnd, &physRect)) return bail();
        if (FAILED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &extRect, sizeof(extRect)))) {
            extRect = physRect;
        }
    }
    RECT winRect;
    if (!GetWindowRect(hWnd, &winRect)) return bail();
    const int rawW = winRect.right - winRect.left;
    const int rawH = winRect.bottom - winRect.top;
    const int physW = physRect.right - physRect.left;
    const int physH = physRect.bottom - physRect.top;
    int cropL = extRect.left - physRect.left, cropT = extRect.top - physRect.top;
    int cropR = extRect.right - physRect.left, cropB = extRect.bottom - physRect.top;
    if (cropL < 0) cropL = 0;
    if (cropT < 0) cropT = 0;
    if (cropR > physW) cropR = physW;
    if (cropB > physH) cropB = physH;
    if (rawW <= 0 || rawH <= 0 || physW <= 0 || physH <= 0 || cropR - cropL <= 0 || cropB - cropT <= 0) {
        return bail();
    }

    if (g_unloading.load(std::memory_order_relaxed)) return bail();

    // One genie per window and direction. Any window we animate also counts as "seen",
    // so the launch path won't fire on it later.
    uint64_t token = 0;
    {
        Locked l;
        auto it = g_AnimActive.find(hWnd);
        if (it != g_AnimActive.end() && it->second.rising == rising) {
            // Duplicate of the genie already in flight: it owns the window's hide and
            // transitions and finishes them; nothing to undo here.
            return false;
        }
        token = ++g_nextToken;
        g_AnimActive[hWnd] = {token, rising};
        g_LaunchSeen.insert(hWnd);
    }
    auto release = [&]() {
        Locked l;
        auto it = g_AnimActive.find(hWnd);
        if (it != g_AnimActive.end() && it->second.token == token) g_AnimActive.erase(it);
    };

    HANDLE hDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE hRevealed = rising ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
    HANDLE hFirst = rising ? nullptr : CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto closeEvents = [&]() {
        if (hDone) CloseHandle(hDone);
        if (hRevealed) CloseHandle(hRevealed);
        if (hFirst) CloseHandle(hFirst);
    };
    if (!hDone || (rising && !hRevealed) || (!rising && !hFirst)) {
        closeEvents();
        release();
        return bail();
    }

    // The worker is created up front (suspended), so once the host has accepted the
    // request there is always someone to finish it.
    GenieTeardown* td = new GenieTeardown{hWnd, rising, originalExStyle, cloakHidden,
                                          deferredMinimize, requestedUnhide, hNextApp,
                                          DurationMs(), UnhideDurationMs(), hDone, hRevealed, token, 0};
    HANDLE hWorker = nullptr;
    if (!SpawnWorker(GenieTeardownThread, td, true, &hWorker)) {
        delete td;
        closeEvents();
        release();
        return bail();
    }

    AnimRequest req = {};
    req.version = MW_IPC_VERSION;
    req.kind = rising ? MW_ANIM_RESTORE : MW_ANIM_MINIMIZE;
    req.hwnd = (uint64_t)(uintptr_t)hWnd;
    req.pid = GetCurrentProcessId();
    req.rawW = rawW;
    req.rawH = rawH;
    req.cropU0 = (float)cropL / (float)physW;
    req.cropV0 = (float)cropT / (float)physH;
    req.cropU1 = (float)cropR / (float)physW;
    req.cropV1 = (float)cropB / (float)physH;
    req.hFirstFrame = (uint64_t)(uintptr_t)hFirst;
    req.hDone = (uint64_t)(uintptr_t)hDone;
    req.hRevealed = (uint64_t)(uintptr_t)hRevealed;
    req.flags |= (t_source << MW_REQ_SOURCE_SHIFT) & MW_REQ_SOURCE_MASK;

    // Falling: capture now, while the window is still on screen. Rising: the host may
    // still hold the snapshot from the minimize; capture only if it asks.
    HANDLE hSection = rising ? nullptr : CaptureToSection(hWnd, rawW, rawH);
    if (hSection) {
        req.flags |= MW_REQ_HAS_SNAPSHOT;
        req.hSection = (uint64_t)(uintptr_t)hSection;
    }
    uint32_t reply = SendAnimRequest(hHost, req);
    if (rising && reply == MW_REPLY_NEED_SNAPSHOT) {
        // No cached minimize snapshot (e.g. minimized before MacWin started): capture
        // the freshly restored, still cloaked window.
        hSection = CaptureToSection(hWnd, rawW, rawH);
        if (hSection) {
            req.flags |= MW_REQ_HAS_SNAPSHOT;
            req.hSection = (uint64_t)(uintptr_t)hSection;
            reply = SendAnimRequest(hHost, req);
        } else {
            reply = MW_REPLY_FAIL;
        }
    }
    if (hSection) CloseHandle(hSection);   // the host has its own duplicate now

    if (reply != MW_REPLY_OK) {
        InterlockedExchange(&td->cancelled, 1);
        ResumeThread(hWorker);
        closeEvents();
        release();
        return bail();
    }

    ResumeThread(hWorker);   // owns hDone / hRevealed from here on
    if (hFirst) {
        // Hold the real minimize back until the genie's first frame is on screen,
        // otherwise the window would vanish a few frames before the genie appears.
        WaitForSingleObject(hFirst, 200);   // capped: worst case = no sync
        CloseHandle(hFirst);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Launch animation (experimental, off by default)
// ---------------------------------------------------------------------------

struct MacGenieLaunchData {
    HWND hWnd;
    LONG_PTR originalExStyle;
    BOOL hiddenByCloak;
};

struct MacGenieGeckoSearch {
    HWND self;
    DWORD pid;
    bool found;
};

BOOL CALLBACK MacGenieFindOtherGeckoWindow(HWND w, LPARAM lp) {
    MacGenieGeckoSearch* s = (MacGenieGeckoSearch*)lp;
    if (w == s->self || (!IsWindowVisible(w) && !IsIconic(w)) || GetAncestor(w, GA_ROOT) != w) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    WCHAR cls[64];
    if (pid == s->pid && GetClassNameW(w, cls, ARRAYSIZE(cls)) &&
        _wcsicmp(cls, L"MozillaWindowClass") == 0) {
        s->found = true;
        return FALSE;
    }
    return TRUE;
}

// Firefox-family browsers: is hWnd the process's first browser window?
bool MacGenieIsFirstGeckoWindow(HWND hWnd) {
    WCHAR cls[64];
    if (!GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) || _wcsicmp(cls, L"MozillaWindowClass") != 0) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    MacGenieGeckoSearch search = {hWnd, pid, false};
    EnumWindows(MacGenieFindOtherGeckoWindow, (LPARAM)&search);
    return !search.found;
}

DWORD WINAPI MacGenieLaunchThread(LPVOID lpParam);

// Arm the launch animation before the real show call: check eligibility, kill the
// DWM transition and hide the window (DWM cloak; the layered hide only for a first
// Firefox-family window, an app-cloaked window, or when cloaking is refused).
bool MacGenieLaunchPrepare(HWND hWnd, int nCmdShow, LONG_PTR* origExOut, BOOL* cloakedOut) {
    if (g_unloading.load(std::memory_order_relaxed)) return false;
    if (!LaunchEnabled()) return false;
    if (nCmdShow != SW_SHOW && nCmdShow != SW_SHOWNORMAL && nCmdShow != SW_SHOWDEFAULT &&
        nCmdShow != SW_SHOWMAXIMIZED) {
        return false;
    }
    if (IsWindowVisible(hWnd) || IsIconic(hWnd)) return false;
    if (!MacGenieIsLaunchWindow(hWnd)) return false;
    {
        Locked l;
        if (!g_LaunchSeen.insert(hWnd).second) return false;
    }
    if (MacGenieIsExcluded(hWnd)) return false;
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    *origExOut = exStyle;
    MacGenieSetDwmTransitions(hWnd, FALSE);

    UINT cloaked = 0;
    bool appCloaked = SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
                      (cloaked & DWM_CLOAKED_APP);
    BOOL cloak = TRUE;
    if (!appCloaked && !MacGenieIsFirstGeckoWindow(hWnd) &&
        SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
        *cloakedOut = TRUE;
        return true;
    }
    SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
    *cloakedOut = FALSE;
    return true;
}

void MacGenieLaunchCommit(HWND hWnd, LONG_PTR originalExStyle, BOOL cloaked) {
    MacGenieLaunchData* ld = new MacGenieLaunchData{hWnd, originalExStyle, cloaked};
    if (!SpawnWorker(MacGenieLaunchThread, ld, false, nullptr)) {
        delete ld;
        MacGenieUndoRisingHide(hWnd, originalExStyle, cloaked);
    }
}

DWORD WINAPI MacGenieLaunchThread(LPVOID lpParam) {
    MacGenieLaunchData* ld = (MacGenieLaunchData*)lpParam;
    HWND hWnd = ld->hWnd;
    LONG_PTR originalExStyle = ld->originalExStyle;
    BOOL hiddenByCloak = ld->hiddenByCloak;
    delete ld;

    // Let the freshly shown (still hidden) window paint; UWP apps stay cloaked by the
    // shell behind their splash for a while - wait (bounded) for that cloak to lift.
    // Our own cloak reports as DWM_CLOAKED_APP, so that bit is masked out.
    UINT ownCloak = hiddenByCloak ? (UINT)DWM_CLOAKED_APP : 0;
    Sleep(60);
    for (int i = 0; i < 30; ++i) {
        if (!IsWindow(hWnd) || g_unloading.load(std::memory_order_relaxed)) break;
        UINT cloaked = 0;
        if (FAILED(DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) ||
            !(cloaked & ~ownCloak)) {
            break;
        }
        Sleep(50);
    }

    if (g_unloading.load(std::memory_order_relaxed) || !IsWindow(hWnd) || IsIconic(hWnd) ||
        !IsWindowVisible(hWnd)) {
        if (IsWindow(hWnd)) MacGenieUndoRisingHide(hWnd, originalExStyle, hiddenByCloak);
        return 0;
    }
    SourceScope src(MW_SRC_LAUNCH);
    StartMacGenieAnim(hWnd, TRUE, originalExStyle, hiddenByCloak);
    return 0;
}

// ---------------------------------------------------------------------------
// Hooks. Each has a pass-through fast path that tail-calls the original (no frame of
// ours stays on the stack, e.g. during modal move/size loops); the slow paths hold a
// HookScope so the DLL never unloads under them.
// ---------------------------------------------------------------------------

__attribute__((noinline)) BOOL WINAPI ShowWindowSlow(HWND hWnd, int nCmdShow) {
    HookScope scope;
    OwnStateChange own;
    SourceScope src(MW_SRC_SHOWWINDOW);
    if (IsMinimizeCmd(nCmdShow)) {
        if (MacGenieShouldAnimate(hWnd)) {
            MacGenieSetDwmTransitions(hWnd, FALSE);
            StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
        }
        return ShowWindow_Original(hWnd, nCmdShow);
    }

    if ((nCmdShow == SW_RESTORE || nCmdShow == SW_SHOWNORMAL) && IsIconic(hWnd)) {
        if (RestoreEnabled() && MacGenieShouldAnimate(hWnd)) {
            MacGenieSetDwmTransitions(hWnd, FALSE);
            MacGenieSetCloak(hWnd, TRUE);
            BOOL res = ShowWindow_Original(hWnd, nCmdShow);
            StartMacGenieAnim(hWnd, TRUE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE), TRUE);
            return res;
        }
        return ShowWindow_Original(hWnd, nCmdShow);
    }

    LONG_PTR launchOrigEx;
    BOOL launchCloaked;
    if (MacGenieLaunchPrepare(hWnd, nCmdShow, &launchOrigEx, &launchCloaked)) {
        BOOL res = ShowWindow_Original(hWnd, nCmdShow);
        MacGenieLaunchCommit(hWnd, launchOrigEx, launchCloaked);
        return res;
    }
    return ShowWindow_Original(hWnd, nCmdShow);
}

BOOL WINAPI ShowWindow_Hook(HWND hWnd, int nCmdShow) {
    if (!EffectsActiveHere()) MW_MUSTTAIL return ShowWindow_Original(hWnd, nCmdShow);
    MW_MUSTTAIL return ShowWindowSlow(hWnd, nCmdShow);
}

template <DefWindowProc_t& Original>
__attribute__((noinline)) LRESULT WINAPI DefWindowProcSlow(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    HookScope scope;
    OwnStateChange own;
    SourceScope src(MW_SRC_DEFWINDOWPROC);
    if (Msg == WM_DESTROY) {
        {
            Locked l;
            g_LaunchSeen.erase(hWnd);
        }
        return Original(hWnd, Msg, wParam, lParam);
    }

    UINT cmd = (UINT)(wParam & 0xFFF0);
    if (cmd == SC_MINIMIZE) {
        // Our own deferred minimize (auto-hide path): pass straight through.
        if (GetPropW(hWnd, kBypassProp)) return Original(hWnd, Msg, wParam, lParam);
        if (EffectsActiveHere() && MacGenieShouldAnimate(hWnd)) {
            // Auto-hide taskbar? Reveal it and defer the real minimize until the
            // animation finishes, so it doesn't slide away mid-genie.
            BOOL requestedUnhide = FALSE;
            HWND hNext = nullptr;
            if (UnhideEnabled()) {
                HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
                HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                if (hTray && MonitorFromWindow(hTray, MONITOR_DEFAULTTOPRIMARY) != hMon) {
                    HWND hSec = nullptr;
                    while ((hSec = FindWindowExW(nullptr, hSec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
                        if (MonitorFromWindow(hSec, MONITOR_DEFAULTTONULL) == hMon) {
                            hTray = hSec;
                            break;
                        }
                    }
                }
                if (hTray) {
                    APPBARDATA abd = {sizeof(APPBARDATA)};
                    abd.hWnd = hTray;
                    UINT uState = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
                    if (uState & ABS_AUTOHIDE) {
                        requestedUnhide = TRUE;
                        HWND hwndIter = GetWindow(hWnd, GW_HWNDNEXT);
                        while (hwndIter) {
                            if (IsWindowVisible(hwndIter) && !IsIconic(hwndIter) &&
                                GetAncestor(hwndIter, GA_ROOT) == hwndIter &&
                                GetWindowTextLengthW(hwndIter) > 0 && hwndIter != hWnd &&
                                hwndIter != hTray) {
                                LONG_PTR exStyle = GetWindowLongPtrW(hwndIter, GWL_EXSTYLE);
                                if (!(exStyle & WS_EX_TOOLWINDOW)) {
                                    hNext = hwndIter;
                                    break;
                                }
                            }
                            hwndIter = GetWindow(hwndIter, GW_HWNDNEXT);
                        }
                        SetForegroundWindow(hTray);
                    }
                }
            }

            MacGenieSetDwmTransitions(hWnd, FALSE);
            if (requestedUnhide) {
                bool ok = StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE),
                                            FALSE, TRUE, TRUE, hNext);
                if (ok) {
                    // First frame is up: hide the real window flash-free; the worker
                    // minimizes it at the end.
                    MacGenieSetCloak(hWnd, TRUE);
                    return 0;
                }
                return Original(hWnd, Msg, wParam, lParam);
            }
            StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
        }
        return Original(hWnd, Msg, wParam, lParam);
    }

    if (cmd == SC_RESTORE && IsIconic(hWnd) && EffectsActiveHere() && RestoreEnabled() &&
        MacGenieShouldAnimate(hWnd)) {
        MacGenieSetDwmTransitions(hWnd, FALSE);
        MacGenieSetCloak(hWnd, TRUE);
        LRESULT res = Original(hWnd, Msg, wParam, lParam);
        StartMacGenieAnim(hWnd, TRUE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE), TRUE);
        return res;
    }
    return Original(hWnd, Msg, wParam, lParam);
}

inline bool DefWindowProcInteresting(UINT Msg, WPARAM wParam) {
    if (Msg == WM_DESTROY) return true;
    if (Msg != WM_SYSCOMMAND) return false;
    UINT cmd = (UINT)(wParam & 0xFFF0);
    return cmd == SC_MINIMIZE || cmd == SC_RESTORE;
}

// Called for every message of every window: the fast path is one compare and a jump.
LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (!DefWindowProcInteresting(Msg, wParam)) MW_MUSTTAIL return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
    MW_MUSTTAIL return DefWindowProcSlow<DefWindowProcW_Original>(hWnd, Msg, wParam, lParam);
}

LRESULT WINAPI DefWindowProcA_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (!DefWindowProcInteresting(Msg, wParam)) MW_MUSTTAIL return DefWindowProcA_Original(hWnd, Msg, wParam, lParam);
    MW_MUSTTAIL return DefWindowProcSlow<DefWindowProcA_Original>(hWnd, Msg, wParam, lParam);
}

// Custom-title-bar apps (Zed/GPUI, some Electron) minimize via ShowWindowAsync,
// SetWindowPlacement or CloseWindow (no auto-hide handling on these paths).
void MacGenieTryMinimizeAnim(HWND hWnd) {
    if (!IsWindowVisible(hWnd) || IsIconic(hWnd)) return;
    if (!MacGenieShouldAnimate(hWnd)) return;
    MacGenieSetDwmTransitions(hWnd, FALSE);
    StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
}

// Only the first-show (launch) case. An async minimize / restore runs later, in the
// window's own thread: the HCBT_MINMAX there animates it (GenieOnMinMax). Doing it here
// instead held the caller up per window - Explorer's "Show desktop" started its genies
// one after another and restored windows without any.
__attribute__((noinline)) BOOL WINAPI ShowWindowAsyncSlow(HWND hWnd, int nCmdShow) {
    HookScope scope;
    SourceScope src(MW_SRC_LAUNCH);
    LONG_PTR launchOrigEx;
    BOOL launchCloaked;
    if (MacGenieLaunchPrepare(hWnd, nCmdShow, &launchOrigEx, &launchCloaked)) {
        BOOL res = ShowWindowAsync_Original(hWnd, nCmdShow);
        MacGenieLaunchCommit(hWnd, launchOrigEx, launchCloaked);
        return res;
    }
    return ShowWindowAsync_Original(hWnd, nCmdShow);
}

BOOL WINAPI ShowWindowAsync_Hook(HWND hWnd, int nCmdShow) {
    if (IsMinimizeCmd(nCmdShow) || !EffectsActiveHere() || !LaunchEnabled()) {
        MW_MUSTTAIL return ShowWindowAsync_Original(hWnd, nCmdShow);
    }
    MW_MUSTTAIL return ShowWindowAsyncSlow(hWnd, nCmdShow);
}

__attribute__((noinline)) BOOL WINAPI SetWindowPlacementSlow(HWND hWnd, const WINDOWPLACEMENT* lpwndpl) {
    HookScope scope;
    OwnStateChange own;
    SourceScope src(MW_SRC_SETWINDOWPLACEMENT);
    MacGenieTryMinimizeAnim(hWnd);
    return SetWindowPlacement_Original(hWnd, lpwndpl);
}

BOOL WINAPI SetWindowPlacement_Hook(HWND hWnd, const WINDOWPLACEMENT* lpwndpl) {
    if (!lpwndpl || !IsMinimizeCmd((int)lpwndpl->showCmd) || !EffectsActiveHere()) {
        MW_MUSTTAIL return SetWindowPlacement_Original(hWnd, lpwndpl);
    }
    MW_MUSTTAIL return SetWindowPlacementSlow(hWnd, lpwndpl);
}

__attribute__((noinline)) BOOL WINAPI CloseWindowSlow(HWND hWnd) {
    HookScope scope;
    OwnStateChange own;
    SourceScope src(MW_SRC_CLOSEWINDOW);
    MacGenieTryMinimizeAnim(hWnd);
    return CloseWindow_Original(hWnd);
}

BOOL WINAPI CloseWindow_Hook(HWND hWnd) {
    if (!EffectsActiveHere()) MW_MUSTTAIL return CloseWindow_Original(hWnd);
    MW_MUSTTAIL return CloseWindowSlow(hWnd);
}

// Store / UWP apps show their first window via SetWindowPos + SWP_SHOWWINDOW.
__attribute__((noinline)) BOOL WINAPI SetWindowPosSlow(HWND hWnd, HWND hWndInsertAfter, int X, int Y,
                                                int cx, int cy, UINT uFlags) {
    HookScope scope;
    SourceScope src(MW_SRC_LAUNCH);
    LONG_PTR launchOrigEx;
    BOOL launchCloaked;
    if (MacGenieLaunchPrepare(hWnd, SW_SHOW, &launchOrigEx, &launchCloaked)) {
        BOOL res = SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
        MacGenieLaunchCommit(hWnd, launchOrigEx, launchCloaked);
        return res;
    }
    return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

// Hot: most SetWindowPos calls are moves / resizes / z-order changes.
BOOL WINAPI SetWindowPos_Hook(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (!(uFlags & SWP_SHOWWINDOW) || !EffectsActiveHere() || !LaunchEnabled()) {
        MW_MUSTTAIL return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
    }
    MW_MUSTTAIL return SetWindowPosSlow(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

// A restore seen in HCBT_MINMAX happens right after the notification returns: wait
// for it (the window is cloaked meanwhile), then start the rising genie.
struct PendingRestore {
    HWND hWnd;
    LONG_PTR exStyle;
};

DWORD WINAPI PendingRestoreThread(LPVOID param) {
    PendingRestore* p = (PendingRestore*)param;
    HWND hWnd = p->hWnd;
    LONG_PTR exStyle = p->exStyle;
    delete p;
    for (int i = 0; i < 60 && IsWindow(hWnd) && IsIconic(hWnd) && !g_unloading.load(); ++i) Sleep(5);
    if (!IsWindow(hWnd)) return 0;
    if (IsIconic(hWnd) || g_unloading.load()) {
        MacGenieUndoRisingHide(hWnd, exStyle, TRUE);   // the restore never came
        return 0;
    }
    SourceScope src(MW_SRC_CBT);
    StartMacGenieAnim(hWnd, TRUE, exStyle, TRUE);
    return 0;
}

}  // namespace

void GenieOnMinMax(HWND hWnd, int cmd) {
    if (t_ownStateChange > 0) return;   // one of our hooked calls on this thread
    if (!EffectsActiveHere()) return;
    HookScope scope;
    SourceScope src(MW_SRC_CBT);
    if (IsMinimizeCmd(cmd) || cmd == SW_FORCEMINIMIZE) {
        if (IsIconic(hWnd) || !IsWindowVisible(hWnd) || !MacGenieShouldAnimate(hWnd)) return;
        // Waits for the genie's first frame, then the minimize goes ahead.
        MacGenieSetDwmTransitions(hWnd, FALSE);
        StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
        return;
    }
    // Any other command on a minimized window restores it (SW_RESTORE, SW_SHOWNOACTIVATE,
    // SW_SHOWNORMAL, ...), maximized windows included.
    if (!IsIconic(hWnd) || !RestoreEnabled() || !MacGenieShouldAnimate(hWnd)) return;
    {
        Locked l;
        auto it = g_AnimActive.find(hWnd);
        if (it != g_AnimActive.end() && it->second.rising) return;   // already coming back
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    MacGenieSetDwmTransitions(hWnd, FALSE);
    MacGenieSetCloak(hWnd, TRUE);
    PendingRestore* p = new PendingRestore{hWnd, exStyle};
    if (!SpawnWorker(PendingRestoreThread, p, false, nullptr)) {
        delete p;
        MacGenieUndoRisingHide(hWnd, exStyle, TRUE);
    }
}

bool GenieInstallHooks() {
    return CreateUser32Hook("DefWindowProcW", (void*)DefWindowProcW_Hook, (void**)&DefWindowProcW_Original) &&
           CreateUser32Hook("DefWindowProcA", (void*)DefWindowProcA_Hook, (void**)&DefWindowProcA_Original) &&
           CreateUser32Hook("ShowWindow", (void*)ShowWindow_Hook, (void**)&ShowWindow_Original) &&
           CreateUser32Hook("ShowWindowAsync", (void*)ShowWindowAsync_Hook, (void**)&ShowWindowAsync_Original) &&
           CreateUser32Hook("SetWindowPlacement", (void*)SetWindowPlacement_Hook,
                            (void**)&SetWindowPlacement_Original) &&
           CreateUser32Hook("CloseWindow", (void*)CloseWindow_Hook, (void**)&CloseWindow_Original) &&
           CreateUser32Hook("SetWindowPos", (void*)SetWindowPos_Hook, (void**)&SetWindowPos_Original);
}

void GenieOnShutdown() {
    Locked l;
    g_LaunchSeen.clear();
    g_AnimActive.clear();
}
