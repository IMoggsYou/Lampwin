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

// The adapter Windows sends internet traffic through (its routing table, asked for a public
// address: nothing is sent): Wi-Fi or Ethernet, or the VPN while one is on. Adding up every
// adapter would count a VPN's traffic twice, and virtual switches' too.
void SystemSampler::SampleNetwork(SystemStats* s) {
    sockaddr_in internet = {};
    internet.sin_family = AF_INET;
    internet.sin_addr.s_addr = 0x01010101;   // 1.1.1.1 (the same in any byte order), only looked up
    DWORD index = 0;
    MIB_IF_ROW2 row = {};
    if (GetBestInterfaceEx((sockaddr*)&internet, &index) != NO_ERROR) return;
    row.InterfaceIndex = index;
    if (GetIfEntry2(&row) != NO_ERROR) return;
    if (index != interface_) {
        // Another adapter (Wi-Fi to Ethernet, a VPN): start counting afresh.
        interface_ = index;
        networkAt_ = 0;
    }
    s->link = row.ReceiveLinkSpeed != (ULONG64)-1 ? row.ReceiveLinkSpeed : 0;
    const ULONGLONG received = row.InOctets, sent = row.OutOctets;
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
