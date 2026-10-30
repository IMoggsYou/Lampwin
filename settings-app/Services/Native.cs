// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace MacWin.Settings;

internal static class Native
{
    private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    private const int SW_RESTORE = 9;

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern IntPtr FindWindow(string? className, string? windowName);

    // The shell redraws a changed shortcut's icon (App icons).
    internal const int SHCNE_UPDATEITEM = 0x00002000, SHCNE_ASSOCCHANGED = 0x08000000;
    internal const uint SHCNF_IDLIST = 0x0000, SHCNF_PATHW = 0x0005;

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    internal static extern void SHChangeNotify(int eventId, uint flags, string item1, IntPtr item2);

    [DllImport("shell32.dll")]
    internal static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);

    private const int HWND_BROADCAST = 0xFFFF, WM_SETTINGCHANGE = 0x001A, SMTO_ABORTIFHUNG = 0x0002;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SendMessageTimeout(IntPtr hwnd, int msg, IntPtr wParam, string lParam, int flags,
                                                    int timeoutMs, out IntPtr result);

    /// <summary>File Explorer and the taskbar read their settings again (the Tweaks page).
    /// Waits for every window that answers: call it off the UI thread.</summary>
    internal static void NotifyShellSettingsChanged()
    {
        foreach (string area in new[] { "TraySettings", "ShellState" })
            SendMessageTimeout((IntPtr)HWND_BROADCAST, WM_SETTINGCHANGE, IntPtr.Zero, area, SMTO_ABORTIFHUNG, 1000, out _);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, IntPtr.Zero, IntPtr.Zero);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern uint RegisterWindowMessage(string name);

    [DllImport("user32.dll")]
    internal static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int maxCount);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int cmd);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hwnd);

    /// <summary>The window draws its own squircle corners, background and (with
    /// WindowShadow) shadow: DWM must not round it on top of that.</summary>
    public static void ApplyWindowAttributes(Window window, bool dark)
    {
        IntPtr hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero) return;
        int on = dark ? 1 : 0;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref on, sizeof(int));
        int corner = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref corner, sizeof(int));
    }

    /// <summary>True while DWM hides the window (MacWin cloaks it during a restore genie).</summary>
    public static bool IsCloaked(IntPtr hwnd) =>
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, out int cloaked, sizeof(int)) == 0 && cloaked != 0;

    /// <summary>WM_GETMINMAXINFO for a frameless window: maximized = the monitor's work
    /// area, not the whole monitor (the taskbar stays visible).</summary>
    public static void FitMaximizedToWorkArea(IntPtr hwnd, IntPtr lParam)
    {
        IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
        if (monitor == IntPtr.Zero || !GetMonitorInfo(monitor, ref mi)) return;
        var mmi = Marshal.PtrToStructure<MINMAXINFO>(lParam);
        mmi.ptMaxPosition.X = mi.rcWork.Left - mi.rcMonitor.Left;
        mmi.ptMaxPosition.Y = mi.rcWork.Top - mi.rcMonitor.Top;
        mmi.ptMaxSize.X = mi.rcWork.Right - mi.rcWork.Left;
        mmi.ptMaxSize.Y = mi.rcWork.Bottom - mi.rcWork.Top;
        Marshal.StructureToPtr(mmi, lParam, false);
    }

    /// <summary>Puts `hwnd` right under `under` in the z-order, at `rect` (physical px).</summary>
    public static void PlaceUnder(IntPtr hwnd, IntPtr under, RECT rect) =>
        SetWindowPos(hwnd, under, rect.Left, rect.Top, rect.Right - rect.Left, rect.Bottom - rect.Top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    /// <summary>Click-through, never activated, not in the taskbar or Alt+Tab.</summary>
    public static void MakeOverlay(IntPtr hwnd)
    {
        long ex = (long)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
        ex &= ~WS_EX_APPWINDOW;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, (IntPtr)ex);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MINMAXINFO { public POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO { public int cbSize; public RECT rcMonitor, rcWork; public int dwFlags; }

    private const int DWMWA_CLOAKED = 14;
    private const int DWMWCP_DONOTROUND = 1;
    private const uint MONITOR_DEFAULTTONEAREST = 2;
    private const uint SWP_NOACTIVATE = 0x0010, SWP_NOOWNERZORDER = 0x0200;
    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TRANSPARENT = 0x20, WS_EX_TOOLWINDOW = 0x80, WS_EX_APPWINDOW = 0x40000,
                       WS_EX_LAYERED = 0x80000, WS_EX_NOACTIVATE = 0x8000000;

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    private static extern IntPtr SetWindowLongPtr(IntPtr hwnd, int index, IntPtr value);

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(int processId);

    /// <summary>"Show yourself" (wParam: page index + 1, 0 = keep the page), sent by a
    /// second copy of this app to the one already open.</summary>
    public const string ShowMessage = "MacWin_SettingsShow_v1";

    /// <summary>Asks the settings window that is already open (another process of this
    /// app) to come to the front. The window title is translated, so it is found by its
    /// process instead.</summary>
    public static bool ShowOtherInstance(int page)
    {
        using var self = System.Diagnostics.Process.GetCurrentProcess();
        uint message = RegisterWindowMessage(ShowMessage);
        bool posted = false;
        foreach (var other in System.Diagnostics.Process.GetProcessesByName(self.ProcessName))
        {
            using (other)
            {
                if (other.Id == self.Id || other.SessionId != self.SessionId) continue;
                AllowSetForegroundWindow(other.Id);   // we were just started by the user: we may hand it on
                // Its top-level windows (the settings window and its shadow): only the
                // settings window knows the message.
                EnumWindows((hwnd, _) =>
                {
                    GetWindowThreadProcessId(hwnd, out uint pid);
                    if (pid == (uint)other.Id) posted |= PostMessage(hwnd, message, (IntPtr)(page + 1), IntPtr.Zero);
                    return true;
                }, IntPtr.Zero);
            }
        }
        return posted;
    }

    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    public static void Activate(IntPtr hwnd)
    {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
}
