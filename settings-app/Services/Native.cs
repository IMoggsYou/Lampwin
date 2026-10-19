using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace MacWin.Settings;

internal static class Native
{
    private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    private const int DWMWA_SYSTEMBACKDROP_TYPE = 38;
    private const int DWMSBT_MAINWINDOW = 2;   // Mica
    private const int DWMWCP_ROUND = 2;
    private const int SW_RESTORE = 9;

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern IntPtr FindWindow(string? className, string? windowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    internal static extern uint RegisterWindowMessage(string name);

    [DllImport("user32.dll")]
    internal static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int cmd);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hwnd);

    /// <summary>Mica behind the whole window (the content pane paints over it), dark title
    /// handling and Windows 11 rounded corners.</summary>
    public static void ApplyBackdrop(Window window, bool dark)
    {
        IntPtr hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero) return;
        if (HwndSource.FromHwnd(hwnd) is HwndSource source && source.CompositionTarget is not null)
            source.CompositionTarget.BackgroundColor = Colors.Transparent;
        int on = dark ? 1 : 0;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref on, sizeof(int));
        int corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref corner, sizeof(int));
        int backdrop = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref backdrop, sizeof(int));
    }

    public static void ActivateWindow(string title)
    {
        IntPtr hwnd = FindWindow(null, title);
        if (hwnd == IntPtr.Zero) return;
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
}
