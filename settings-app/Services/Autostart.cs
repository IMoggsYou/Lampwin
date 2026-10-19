using Microsoft.Win32;

namespace MacWin.Settings;

/// <summary>"Start with Windows" for the current user (HKCU\...\Run).</summary>
internal static class Autostart
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "MacWin";

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return key?.GetValue(ValueName) is string;
    }

    public static void Set(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (enabled) key.SetValue(ValueName, $"\"{HostControl.HostExe}\"");
        else key.DeleteValue(ValueName, throwOnMissingValue: false);
    }
}
