// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#pragma once
#include <string>

namespace widgets {

// MacWin.exe --widget-snapshot <dir> [--lang <code>] [--theme light|dark]: every widget in
// every size with sample data, on a wallpaper, as PNG (widgets.png). Checks the look and
// the translations without touching the desktop. Returns the process exit code.
int WidgetSnapshot(const std::wstring& dir, const std::wstring& language, bool dark);

}  // namespace widgets
