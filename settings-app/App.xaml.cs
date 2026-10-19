using System.Windows;
using Microsoft.Win32;

namespace MacWin.Settings;

public partial class App : Application
{
    private Mutex? _single;

    protected override void OnStartup(StartupEventArgs e)
    {
        _single = new Mutex(true, @"Local\MacWin_SettingsApp_v1", out bool created);
        if (!created)
        {
            // Already open: bring that window to the front instead.
            Native.ActivateWindow(Settings.MainWindow.WindowTitle);
            Shutdown();
            return;
        }

        string? themeArg = ArgValue(e.Args, "--theme");
        bool? forced = themeArg == "dark" ? true : themeArg == "light" ? false : null;
        Theme.Apply(forced ?? Theme.SystemIsDark());
        if (forced is null)
        {
            SystemEvents.UserPreferenceChanged += (_, args) =>
            {
                if (args.Category == UserPreferenceCategory.General)
                    Dispatcher.BeginInvoke(() => Theme.Apply(Theme.SystemIsDark()));
            };
        }

        base.OnStartup(e);
        var window = new MainWindow(ArgValue(e.Args, "--snapshot"), ArgValue(e.Args, "--page"));
        MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _single?.Dispose();
        base.OnExit(e);
    }

    private static string? ArgValue(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }
}
