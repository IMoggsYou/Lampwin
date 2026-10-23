#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MACWIN_SETTINGS_MAGIC 0x3153574Du   // 'MWS1'
#define MACWIN_LIST_CHARS 2048

// Window open / close effects (SharedSettings::openEffect / closeEffect).
enum : int32_t {
    MW_EFFECT_SYSTEM = 0,   // Windows' own animation (MacWin does nothing)
    MW_EFFECT_GENIE = 1,    // out of / into its taskbar icon
    MW_EFFECT_COUNT = 2,
};

// Published by the host in a shared section (MACWIN_SETTINGS_SECTION) and read by every
// DLL, 32-bit and 64-bit alike, so only fixed-width fields - no pointers, no size_t.
// The host writes under a seqlock (`seq` is odd while a write is in progress); plain
// int fields can be read directly, the lists are copied with SettingsCopyList.
struct SharedSettings {
    uint32_t magic;
    uint32_t size;
    volatile LONG seq;
    uint32_t generation;       // bumped on every publish
    uint32_t hostPid;
    uint32_t hostInstance;     // suffix of the per-instance named objects
    uint64_t hostIpcHwnd;      // window that receives WM_COPYDATA AnimRequests

    int32_t enabled;
    int32_t devMode;           // load only into processes listed in devAllowlist

    // Genie minimize / restore (settings of the original mod)
    int32_t genieDurationMs;       // 50..2000
    int32_t genieRestore;          // animate restores (the mod's "open_animation")
    int32_t genieLaunch;           // openEffect != 0: the DLL animates first shows
    int32_t genieMultiMonitor;     // target the window's own monitor's taskbar
    int32_t genieUnhideTaskbar;    // reveal an auto-hidden taskbar during minimize
    int32_t genieUnhideDurationMs; // 0..5000
    int32_t genieMeshQuality;      // tiles per side, 8..96

    // Window corners: radius in px at 100 % scaling and Figma-style smoothing in percent
    // (0 = Windows 11's circular corner, 100 = macOS squircle). The genie always uses
    // them; windowCorners also cuts every real window to that shape (window region).
    int32_t cornerRadius;          // 0..40
    int32_t cornerSmoothing;       // 0..100
    int32_t windowCorners;

    // Opening (first show) and closing (hide / destroy) of windows: MW_EFFECT_*.
    int32_t openEffect;
    int32_t closeEffect;
    int32_t openDurationMs;        // 100..1500
    int32_t closeDurationMs;       // 100..1500
    int32_t reserved[8];

    // Lowercase entries separated by L'\0' and ended by an empty entry. An entry
    // matches a full image path, a file name or a file name without ".exe".
    wchar_t genieExcluded[MACWIN_LIST_CHARS];  // never animate windows of these
    wchar_t noInject[MACWIN_LIST_CHARS];       // the DLL refuses to load into these
    wchar_t devAllowlist[MACWIN_LIST_CHARS];
};

// The 64-bit host and the 32-bit and 64-bit DLLs must see one layout. A deliberate
// change updates these numbers AND bumps the _vN object names (common/names.h).
static_assert(sizeof(SharedSettings) == 12416, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, seq) == 8, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, generation) == 12, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, hostInstance) == 20, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, hostIpcHwnd) == 24, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, enabled) == 32, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, genieMeshQuality) == 64, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, windowCorners) == 76, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, closeDurationMs) == 92, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, genieExcluded) == 128, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, noInject) == 128 + 2 * MACWIN_LIST_CHARS, "SharedSettings layout changed");
static_assert(offsetof(SharedSettings, devAllowlist) == 128 + 4 * MACWIN_LIST_CHARS, "SharedSettings layout changed");

// Consistent copy of one list field (seqlock reader).
inline void SettingsCopyList(const SharedSettings* s, const wchar_t* field,
                             wchar_t* dst, size_t cap) {
    for (int tries = 0; tries < 10000; ++tries) {
        LONG before = s->seq;
        if (before & 1) {
            YieldProcessor();
            continue;
        }
        MemoryBarrier();
        memcpy(dst, field, cap * sizeof(wchar_t));
        MemoryBarrier();
        if (s->seq == before) break;
    }
    dst[cap - 2] = 0;
    dst[cap - 1] = 0;
}
