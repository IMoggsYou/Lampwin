// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once

// Names shared by the host (MacWin.exe) and every injected MacWinHook DLL.
// Bump the _vN suffix whenever the layout of SharedSettings or AnimRequest changes,
// so mismatched builds never talk to each other.

// A test build (preset x64-dev) names all of these differently, so it can run while an
// installed MacWin is stopped but older processes still hold that one's objects.
#ifdef MACWIN_DEV_NAMES
#define MACWIN_NS L"Dev_"
#else
#define MACWIN_NS L""
#endif

#define MACWIN_SETTINGS_SECTION   L"Local\\MacWin_" MACWIN_NS L"Settings_v1"
#define MACWIN_SINGLE_INSTANCE    L"Local\\MacWin_" MACWIN_NS L"SingleInstance_v1"

// Per host instance (the suffix is SharedSettings::hostInstance), so a restarted host
// never inherits a signaled event / abandoned mutex from the previous one.
#define MACWIN_SHUTDOWN_EVENT_FMT L"Local\\MacWin_" MACWIN_NS L"Shutdown_v1_%08X"
#define MACWIN_ALIVE_MUTEX_FMT    L"Local\\MacWin_" MACWIN_NS L"Alive_v1_%08X"
// Exists while the 64-bit DLL animates the windows of process %lu itself (host instance,
// pid): other processes then hand a minimize of its windows over to it.
#define MACWIN_ACTIVE_FMT         L"Local\\MacWin_" MACWIN_NS L"Active_v1_%08X_%lu"

#define MACWIN_HOST_WNDCLASS      L"MacWinHost_" MACWIN_NS L"v1"
#define MACWIN_GHOST_WNDCLASS     L"MacWinGhost_v1"
#define MACWIN_RELOAD_MESSAGE     L"MacWin_" MACWIN_NS L"ReloadSettings_v1"   // RegisterWindowMessage
#define MACWIN_QUIT_MESSAGE       L"MacWin_" MACWIN_NS L"Quit_v1"             // RegisterWindowMessage
// Posted to top-level windows: wParam 0 = re-cut the corners to the current settings,
// 1 = remove MacWin's corners. The hook DLL answers it in DefWindowProc.
#define MACWIN_CORNERS_MESSAGE    L"MacWin_" MACWIN_NS L"Corners_v1"          // RegisterWindowMessage
#define MACWIN_TASK_NAME          L"MacWin"                     // scheduled task (elevated start)

// Lowercase image names the DLL must never activate in.
#define MACWIN_HOST_EXE           L"macwin.exe"
#define MACWIN_HELPER32_EXE       L"macwinhelper32.exe"
#define MACWIN_SETTINGS_EXE       L"macwin.settings.exe"

#define MACWIN_CBT_PROC           "MacWinCbtProc"
#define MACWIN_GETMSG_PROC        "MacWinGetMsgProc"
