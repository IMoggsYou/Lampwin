#pragma once
#include <stdint.h>

// DLL -> host animation request, sent with WM_COPYDATA (COPYDATASTRUCT::dwData =
// MW_IPC_ANIM) and SendMessageTimeout, so the reply is the LRESULT. Handles are values
// in the sending process; the host duplicates what it needs before replying.

#define MW_IPC_ANIM    0x4D570001u
#define MW_IPC_VERSION 1u

enum : uint32_t {
    MW_ANIM_MINIMIZE = 0,   // falling: window -> taskbar button
    MW_ANIM_RESTORE  = 1,   // rising: taskbar button -> window
    MW_ANIM_LAUNCH   = 2,   // rising, first show of a new window (experimental)
};

enum : uint32_t {
    MW_REQ_HAS_SNAPSHOT = 1u,
    MW_REQ_SOURCE_SHIFT = 8,        // bits 8..11: MW_SRC_*
    MW_REQ_SOURCE_MASK = 0xF00u,
};

enum : uint32_t {
    MW_SRC_UNKNOWN = 0,
    MW_SRC_SHOWWINDOW,
    MW_SRC_DEFWINDOWPROC,           // SC_MINIMIZE / SC_RESTORE
    MW_SRC_SETWINDOWPLACEMENT,
    MW_SRC_CLOSEWINDOW,
    MW_SRC_CBT,                     // HCBT_MINMAX in the window's own thread
    MW_SRC_LAUNCH,
};

enum : uint32_t {
    MW_REPLY_FAIL          = 0,   // not animating: undo the hide, let Windows animate
    MW_REPLY_OK            = 1,   // animating; hDone will be signaled
    MW_REPLY_NEED_SNAPSHOT = 2,   // restore without a cached snapshot: capture and resend
};
// A request for a window whose genie is still in flight is never refused: the host
// merges it (same direction) or turns the running genie around (opposite direction).

struct AnimRequest {
    uint32_t version;
    uint32_t kind;
    uint64_t hwnd;
    uint32_t pid;
    uint32_t flags;
    // Pagefile section holding a rawW x rawH top-down BGRA (premultiplied) snapshot of
    // the full window rect, straight from PrintWindow.
    uint64_t hSection;
    int32_t rawW;
    int32_t rawH;
    // Part of the snapshot covered by the DWM extended frame, as fractions of raw size.
    float cropU0, cropV0, cropU1, cropV1;
    uint64_t hFirstFrame;   // minimize: set once the first genie frame is on screen
    uint64_t hDone;         // set after the last frame; the DLL then tears down
    uint64_t hRevealed;     // rising: set by the DLL once the real window is visible
};
