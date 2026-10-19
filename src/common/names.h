#pragma once

// Names shared by the host (MacWin.exe) and every injected MacWinHook DLL.
// Bump the _vN suffix whenever the layout of SharedSettings or AnimRequest changes,
// so mismatched builds never talk to each other.

#define MACWIN_SETTINGS_SECTION   L"Local\\MacWin_Settings_v1"
#define MACWIN_SINGLE_INSTANCE    L"Local\\MacWin_SingleInstance_v1"

// Per host instance (the suffix is SharedSettings::hostInstance), so a restarted host
// never inherits a signaled event / abandoned mutex from the previous one.
#define MACWIN_SHUTDOWN_EVENT_FMT L"Local\\MacWin_Shutdown_v1_%08X"
#define MACWIN_ALIVE_MUTEX_FMT    L"Local\\MacWin_Alive_v1_%08X"

#define MACWIN_HOST_WNDCLASS      L"MacWinHost_v1"
#define MACWIN_GHOST_WNDCLASS     L"MacWinGhost_v1"
#define MACWIN_RELOAD_MESSAGE     L"MacWin_ReloadSettings_v1"   // RegisterWindowMessage
#define MACWIN_QUIT_MESSAGE       L"MacWin_Quit_v1"             // RegisterWindowMessage
#define MACWIN_TASK_NAME          L"MacWin"                     // scheduled task (elevated start)

// Lowercase image names the DLL must never activate in.
#define MACWIN_HOST_EXE           L"macwin.exe"
#define MACWIN_HELPER32_EXE       L"macwinhelper32.exe"
#define MACWIN_SETTINGS_EXE       L"macwin.settings.exe"

#define MACWIN_CBT_PROC           "MacWinCbtProc"
#define MACWIN_GETMSG_PROC        "MacWinGetMsgProc"
