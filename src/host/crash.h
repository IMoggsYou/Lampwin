// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

#pragma once

// If MacWin.exe ever crashes: a minidump in %LOCALAPPDATA%\MacWin\crashes (the newest
// five are kept) and a line in the log, so a bug report can say what happened. The
// DLLs notice the host is gone (its "alive" mutex is abandoned) and step aside, and
// the scheduled task starts MacWin again.
void InstallCrashHandler();
