// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#pragma once
#include <string>

// Host log: %LOCALAPPDATA%\MacWin\host.log. Each start moves the previous one to
// host.old.log; past 4 MB it starts over the same way.
void LogInit();
void LogClose();
void Log(const wchar_t* fmt, ...);
std::wstring LogPath();

// %APPDATA%\MacWin (settings) and %LOCALAPPDATA%\MacWin (log), created on demand.
std::wstring RoamingDataDir();
std::wstring LocalDataDir();
