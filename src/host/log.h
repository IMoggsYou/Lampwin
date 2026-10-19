#pragma once
#include <string>

// Host log: %LOCALAPPDATA%\MacWin\host.log, truncated at every start.
void LogInit();
void LogClose();
void Log(const wchar_t* fmt, ...);
std::wstring LogPath();

// %APPDATA%\MacWin (settings) and %LOCALAPPDATA%\MacWin (log), created on demand.
std::wstring RoamingDataDir();
std::wstring LocalDataDir();
