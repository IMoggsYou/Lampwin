// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#include "host/widgets/system_stats.h"
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <string.h>

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
        s.pluggedIn = power.ACLineStatus == 1;
        if (!s.pluggedIn && power.BatteryLifeTime != (DWORD)-1) s.batteryMinutes = (int)(power.BatteryLifeTime / 60);
    }
    SampleNetwork(&s);
    return s;
}

// Every adapter that is up and isn't the loopback or a tunnel: the sum is what the PC
// receives and sends.
void SystemSampler::SampleNetwork(SystemStats* s) {
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return;
    ULONGLONG received = 0, sent = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL ||
            !row.InterfaceAndOperStatusFlags.HardwareInterface || row.InterfaceAndOperStatusFlags.FilterInterface) {
            continue;
        }
        received += row.InOctets;
        sent += row.OutOctets;
    }
    FreeMibTable(table);
    const ULONGLONG now = GetTickCount64();
    if (networkAt_ && now > networkAt_ && received >= received_ && sent >= sent_) {
        const double seconds = (now - networkAt_) / 1000.0;
        s->down = (received - received_) / seconds;
        s->up = (sent - sent_) / seconds;
    }
    received_ = received;
    sent_ = sent;
    networkAt_ = now;
    memmove(downHistory_, downHistory_ + 1, sizeof(float) * (kNetworkHistory - 1));
    memmove(upHistory_, upHistory_ + 1, sizeof(float) * (kNetworkHistory - 1));
    downHistory_[kNetworkHistory - 1] = (float)s->down;
    upHistory_[kNetworkHistory - 1] = (float)s->up;
    memcpy(s->downHistory, downHistory_, sizeof(downHistory_));
    memcpy(s->upHistory, upHistory_, sizeof(upHistory_));
}

}  // namespace widgets
