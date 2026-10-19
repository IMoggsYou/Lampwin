#pragma once
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define MACWIN_SETTINGS_MAGIC 0x3153574Du   // 'MWS1'
#define MACWIN_LIST_CHARS 2048

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
    int32_t genieLaunch;           // experimental: animate first show of new windows
    int32_t genieMultiMonitor;     // target the window's own monitor's taskbar
    int32_t genieUnhideTaskbar;    // reveal an auto-hidden taskbar during minimize
    int32_t genieUnhideDurationMs; // 0..5000
    int32_t genieMeshQuality;      // tiles per side, 8..96
    int32_t reserved[15];

    // Lowercase entries separated by L'\0' and ended by an empty entry. An entry
    // matches a full image path, a file name or a file name without ".exe".
    wchar_t genieExcluded[MACWIN_LIST_CHARS];  // never animate windows of these
    wchar_t noInject[MACWIN_LIST_CHARS];       // the DLL refuses to load into these
    wchar_t devAllowlist[MACWIN_LIST_CHARS];
};

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
