// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#include "host/widgets/system_stats.h"

namespace widgets {

namespace {

ULONGLONG Ticks(const FILETIME& ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

}  // namespace

SystemStats SystemSampler::Sample() {
    SystemStats s;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        // Kernel time includes the idle time.
        ULONGLONG i = Ticks(idle), t = Ticks(kernel) + Ticks(user);
        if (total_ && t > total_) {
            ULONGLONG di = i - idle_, dt = t - total_;
            cpu_ = (int)(100 - (di * 100 + dt / 2) / dt);
            if (cpu_ < 0) cpu_ = 0;
        }
        idle_ = i;
        total_ = t;
    }
    s.cpu = cpu_;

    MEMORYSTATUSEX mem = {sizeof(mem)};
    if (GlobalMemoryStatusEx(&mem)) s.memory = (int)mem.dwMemoryLoad;

    wchar_t windows[MAX_PATH];
    UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
    if (n >= 3) {
        windows[3] = 0;   // "C:\"
        ULARGE_INTEGER total, free;
        if (GetDiskFreeSpaceExW(windows, nullptr, &total, &free) && total.QuadPart) {
            s.disk = (int)(100 - (free.QuadPart * 100 + total.QuadPart / 2) / total.QuadPart);
        }
    }

    SYSTEM_POWER_STATUS power;
    if (GetSystemPowerStatus(&power) && !(power.BatteryFlag & 128) && power.BatteryFlag != 255 &&
        power.BatteryLifePercent <= 100) {
        s.battery = power.BatteryLifePercent;
        s.charging = (power.BatteryFlag & 8) != 0;
    }
    return s;
}

}  // namespace widgets
