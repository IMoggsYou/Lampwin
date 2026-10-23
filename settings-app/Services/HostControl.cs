// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

using System.Diagnostics;
using System.IO;

namespace MacWin.Settings;

/// <summary>Talks to the running MacWin.exe (see src/common/names.h).</summary>
internal static class HostControl
{
    private const string HostWindowClass = "MacWinHost_v1";
    private const string ReloadMessage = "MacWin_ReloadSettings_v1";

    public static string HostExe { get; } = Path.Combine(AppContext.BaseDirectory, "MacWin.exe");
    public static string TestWindowExe { get; } = Path.Combine(AppContext.BaseDirectory, "MacWinTestWindow.exe");
    public static string LogPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "host.log");

    public static bool IsRunning => Native.FindWindow(HostWindowClass, null) != IntPtr.Zero;

    /// <summary>MacWin titles its (hidden) window "MacWin (administrator)" when elevated.</summary>
    public static bool IsElevated
    {
        get
        {
            IntPtr host = Native.FindWindow(HostWindowClass, null);
            if (host == IntPtr.Zero) return false;
            var title = new System.Text.StringBuilder(64);
            Native.GetWindowText(host, title, title.Capacity);
            return title.ToString().Contains("administrator", StringComparison.OrdinalIgnoreCase);
        }
    }

    public static void NotifyReload() => Post(ReloadMessage, 0);

    /// <summary>Posts one of MacWin's registered messages (src/common/names.h) to the host.</summary>
    public static bool Post(string message, int wParam)
    {
        IntPtr host = Native.FindWindow(HostWindowClass, null);
        return host != IntPtr.Zero &&
               Native.PostMessage(host, Native.RegisterWindowMessage(message), (IntPtr)wParam, IntPtr.Zero);
    }

    /// <summary>Starting MacWin.exe is enough: an installed copy hands over to its scheduled
    /// task by itself and comes back with administrator rights.</summary>
    public static bool Start() => Launch(HostExe, null);

    /// <summary>Clean shutdown via "MacWin.exe --quit" (waits for the host to exit).</summary>
    public static Task StopAsync() => Task.Run(() =>
    {
        if (!File.Exists(HostExe)) return;
        using var p = Process.Start(new ProcessStartInfo(HostExe, "--quit") { UseShellExecute = false });
        p?.WaitForExit(20000);
    });

    public static async Task RestartAsync()
    {
        await StopAsync();
        Start();
    }

    public static bool TryOnRealWindow() => Launch(TestWindowExe, "--cycle 1");

    /// <summary>The test window opens and closes twice: the open and close effects.</summary>
    public static bool TryOpenClose() => Launch(TestWindowExe, "--open-close 2");

    public static bool Open(string path) => Launch(path, null);

    /// <summary>Opens a web page in the default browser (only https links of this project).</summary>
    public static bool OpenUrl(string url)
    {
        if (!url.StartsWith("https://", StringComparison.OrdinalIgnoreCase)) return false;
        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            return true;
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            return false;
        }
    }

    private static bool Launch(string path, string? args)
    {
        try
        {
            if (!File.Exists(path) && !Directory.Exists(path)) return false;
            var psi = new ProcessStartInfo(path) { UseShellExecute = true };
            if (args is not null) psi.Arguments = args;
            Process.Start(psi);
            return true;
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            return false;
        }
    }
}
