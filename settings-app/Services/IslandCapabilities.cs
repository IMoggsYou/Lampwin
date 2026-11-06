// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Net.NetworkInformation;
using System.Runtime.InteropServices;

namespace MacWin.Settings;

/// <summary>
/// What this PC has, for the island's options that only make sense with it: a battery
/// (charging, low battery), Wi-Fi or a modem (airplane mode), a modem (mobile data). The
/// host checks the same way (src/host/island/island_sources.cpp, DetectCapabilities).
/// </summary>
public static class IslandCapabilities
{
    [StructLayout(LayoutKind.Sequential)]
    private struct SystemPowerStatus
    {
        public byte ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag;
        public int BatteryLifeTime, BatteryFullLifeTime;
    }

    [DllImport("kernel32.dll")]
    private static extern bool GetSystemPowerStatus(out SystemPowerStatus status);

    /// <summary>A laptop or tablet battery (128: no system battery, 255: unknown).</summary>
    public static bool Battery =>
        GetSystemPowerStatus(out var s) && s.BatteryFlag != 128 && s.BatteryFlag != 255;

    private static NetworkInterfaceType[] Types()
    {
        try
        {
            return NetworkInterface.GetAllNetworkInterfaces().Select(n => n.NetworkInterfaceType).ToArray();
        }
        catch (NetworkInformationException)
        {
            return [];
        }
    }

    /// <summary>A mobile broadband modem.</summary>
    public static bool Cellular =>
        Types().Any(t => t is NetworkInterfaceType.Wwanpp or NetworkInterfaceType.Wwanpp2);

    /// <summary>Wi-Fi or a modem: airplane mode turns something off.</summary>
    public static bool Wireless =>
        Cellular || Types().Contains(NetworkInterfaceType.Wireless80211);
}
