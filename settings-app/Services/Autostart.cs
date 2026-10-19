using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace MacWin.Settings;

/// <summary>
/// "Start with Windows".
///  - Installed copy (Program Files): the "MacWin" scheduled task, which also runs MacWin
///    with administrator rights without a UAC prompt; its logon trigger is the autostart.
///    Changing it re-registers the task, which needs one UAC confirmation.
///  - Anywhere else (a development build): HKCU\...\Run, no administrator rights.
/// </summary>
internal static class Autostart
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "MacWin";
    private const string TaskName = "MacWin";

    public static bool IsInstalled { get; } = IsUnderProgramFiles(HostControl.HostExe);

    /// <summary>Slow-ish (asks the Task Scheduler): call off the UI thread.</summary>
    public static bool IsEnabled()
    {
        if (IsInstalled && QueryTaskXml() is string xml) return xml.Contains("LogonTrigger", StringComparison.Ordinal);
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return key?.GetValue(ValueName) is string;
    }

    public static async Task<bool> SetAsync(bool enabled)
    {
        if (IsInstalled)
        {
            bool ok = await Task.Run(() => RunElevated(enabled ? "--install-task" : "--install-task --no-logon"));
            if (ok) DeleteRunValue();
            return ok;
        }
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (enabled) key.SetValue(ValueName, $"\"{HostControl.HostExe}\"");
        else key.DeleteValue(ValueName, throwOnMissingValue: false);
        return true;
    }

    private static void DeleteRunValue()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true);
        key?.DeleteValue(ValueName, throwOnMissingValue: false);
    }

    private static bool RunElevated(string args)
    {
        try
        {
            using var p = Process.Start(new ProcessStartInfo(HostControl.HostExe, args)
            {
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden,
            });
            if (p is null) return false;
            p.WaitForExit(30000);
            return p.HasExited && p.ExitCode == 0;
        }
        catch (Win32Exception)
        {
            return false;   // UAC prompt declined
        }
    }

    private static string? QueryTaskXml()
    {
        try
        {
            using var p = Process.Start(new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "schtasks.exe"),
                                                             $"/Query /TN \"{TaskName}\" /XML")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
            });
            if (p is null) return null;
            string output = p.StandardOutput.ReadToEnd();
            p.WaitForExit(10000);
            return p.ExitCode == 0 ? output.Replace("\0", "") : null;
        }
        catch (Win32Exception)
        {
            return null;
        }
    }

    private static bool IsUnderProgramFiles(string path)
    {
        foreach (var folder in new[] { Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86 })
        {
            string root = Environment.GetFolderPath(folder);
            if (root.Length > 0 && path.StartsWith(root.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) return true;
        }
        return false;
    }
}
