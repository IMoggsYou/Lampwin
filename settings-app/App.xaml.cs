// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

using System.IO;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;

namespace MacWin.Settings;

public partial class App : Application
{
    private Mutex? _single;
    private static DateTime _lastReport = DateTime.MinValue;

    public static string LogPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "settings.log");

    protected override void OnStartup(StartupEventArgs e)
    {
        // A bug in this window must never take it down with unsaved changes: log it,
        // tell the user once, keep going.
        DispatcherUnhandledException += (_, args) =>
        {
            Report(args.Exception);
            args.Handled = true;
        };
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            if (args.ExceptionObject is Exception ex) Log(ex);
        };
        TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            Log(args.Exception);
            args.SetObserved();
        };

        if (ArgValue(e.Args, "--export-icon") is string iconPath)
        {
            Theme.Apply(false);
            IconExport.Save(iconPath);
            Shutdown();
            return;
        }

        // --lang <code>: this run only (screenshots, checking a translation).
        Loc.Apply(ArgValue(e.Args, "--lang") ?? SettingsStore.ReadLanguage());

        // --record-preview <dir>: the live preview's genie as PNG frames (README animation).
        if (ArgValue(e.Args, "--record-preview") is string recordDir)
        {
            Theme.Apply(ArgValue(e.Args, "--theme") == "dark");
            PreviewRecorder.Run(recordDir);
            Shutdown();
            return;
        }

        string? snapshotDir = ArgValue(e.Args, "--snapshot");
        string? page = ArgValue(e.Args, "--page");
        // Snapshots run beside an open settings window; everything else is one window.
        if (snapshotDir is null)
        {
            _single = new Mutex(true, @"Local\MacWin_SettingsApp_v1", out bool created);
            if (!created)
            {
                Native.ShowOtherInstance(PageIndexOf(page));
                Shutdown();
                return;
            }
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
        var window = new MainWindow(snapshotDir, page);
        MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _single?.Dispose();
        base.OnExit(e);
    }

    /// <summary>Logs an unexpected error and tells the user (at most every 10 s).</summary>
    public static void Report(Exception ex)
    {
        Log(ex);
        if ((DateTime.UtcNow - _lastReport).TotalSeconds < 10) return;
        _lastReport = DateTime.UtcNow;
        try
        {
            MessageBox.Show(Current?.MainWindow ?? new Window(), Loc.F("settings.error.unexpected", LogPath), "MacWin",
                            MessageBoxButton.OK, MessageBoxImage.Warning, MessageBoxResult.OK,
                            Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign : MessageBoxOptions.None);
        }
        catch (InvalidOperationException)
        {
            // Shutting down: the log has it.
        }
    }

    private static void Log(Exception ex)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            var info = new FileInfo(LogPath);
            if (info.Exists && info.Length > 512 * 1024) info.Delete();   // keep it small
            File.AppendAllText(LogPath, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} MacWin.Settings {Project.Version}\n{ex}\n\n");
        }
        catch (Exception logError) when (logError is IOException or UnauthorizedAccessException)
        {
        }
    }

    // --page for the window that is already open: the same ids as MainWindow.PageIndex.
    private static int PageIndexOf(string? page) =>
        page is null ? -1 : Array.IndexOf(new[] { "animations", "corners", "general", "exclusions", "about" }, page.ToLowerInvariant());

    private static string? ArgValue(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }
}
