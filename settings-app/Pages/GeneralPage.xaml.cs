// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace MacWin.Settings.Pages;

public sealed record LanguageOption(string Code, string Name)
{
    public override string ToString() => Name;
}

public partial class GeneralPage : UserControl
{
    private readonly DispatcherTimer _poll = new() { Interval = TimeSpan.FromSeconds(1) };
    private FileSystemWatcher? _updateWatcher;
    private UpdateState _update = UpdateClient.Read();
    private bool _busy;

    public GeneralPage()
    {
        InitializeComponent();
        // "Automatic", then every language in its own name.
        var options = new List<LanguageOption> { new("auto", Loc.T("settings.general.language.auto")) };
        options.AddRange(Loc.Languages.Select(code => new LanguageOption(code, Loc.NativeName(code))));
        LanguageBox.ItemsSource = options;

        _poll.Tick += (_, _) => UpdateStatus();
        Loaded += (_, _) =>
        {
            UpdateStatus();
            WatchUpdateState();
            _poll.Start();
        };
        Unloaded += (_, _) =>
        {
            _poll.Stop();
            _updateWatcher?.Dispose();
            _updateWatcher = null;
        };
    }

    private void WatchUpdateState()
    {
        try
        {
            string dir = Path.GetDirectoryName(UpdateClient.StatePath)!;
            Directory.CreateDirectory(dir);
            _updateWatcher = new FileSystemWatcher(dir, Path.GetFileName(UpdateClient.StatePath))
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName,
                EnableRaisingEvents = true,
            };
            FileSystemEventHandler changed = (_, _) => Dispatcher.BeginInvoke(() =>
            {
                _update = UpdateClient.Read();
                UpdateStatus();
            });
            _updateWatcher.Changed += changed;
            _updateWatcher.Created += changed;
            _updateWatcher.Renamed += (s, e) => changed(s, e);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            _updateWatcher = null;   // the 1 s poll still shows the host's state
        }
    }

    private void UpdateStatus()
    {
        bool running = HostControl.IsRunning;
        StatusRow.Description = Loc.T(_busy ? "settings.general.status.busy"
            : !running ? "settings.general.status.stopped"
            : HostControl.IsElevated ? "settings.general.status.elevated"
            : "settings.general.status.normal");
        StartButton.Visibility = running ? Visibility.Collapsed : Visibility.Visible;
        RestartButton.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        StopButton.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        StartButton.IsEnabled = RestartButton.IsEnabled = StopButton.IsEnabled = !_busy;
        UpdateUpdateRow(running);
    }

    private void UpdateUpdateRow(bool running)
    {
        var u = _update;
        string latest = u.Latest ?? "";
        bool available = running && u.Status is "available" or "failedInstall" && latest.Length > 0;
        bool working = running && u.Status is "checking" or "downloading" or "installing";
        UpdateRow.Title = !running ? Loc.T("settings.general.updates.status.hostStopped")
            : u.Status switch
            {
                "checking" => Loc.T("settings.general.updates.status.checking"),
                "downloading" => Loc.F("settings.general.updates.status.downloading", latest),
                "installing" => Loc.F("settings.general.updates.status.installing", latest),
                "available" => Loc.F("settings.general.updates.status.available", latest),
                "failed" => Loc.T("settings.general.updates.status.failed"),
                "failedInstall" => Loc.T("settings.general.updates.status.failedInstall"),
                "unusable" => Loc.T("settings.general.updates.status.unusable"),
                "upToDate" => Loc.F("settings.general.updates.status.upToDate", u.Current ?? Project.Version),
                _ => Loc.T("settings.general.updates.status.never"),
            };
        UpdateRow.Description = u.LastCheck is DateTime t
            ? Loc.F("settings.general.updates.lastCheck", t.ToString("g", System.Globalization.CultureInfo.CurrentCulture))
            : null;
        InstallButton.Content = Loc.F("settings.general.updates.install", latest);
        InstallButton.Visibility = WhatsNewButton.Visibility = available ? Visibility.Visible : Visibility.Collapsed;
        CheckButton.IsEnabled = !working && !_busy;   // with MacWin stopped, the check starts it
        InstallButton.IsEnabled = !working;
    }

    private async Task Run(Func<Task> action)
    {
        _busy = true;
        UpdateStatus();
        try
        {
            await action();
            await Task.Delay(600);
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        finally
        {
            _busy = false;
            UpdateStatus();
        }
    }

    private async void Start_Click(object sender, RoutedEventArgs e) => await Run(() =>
    {
        HostControl.Start();
        return Task.CompletedTask;
    });

    private async void Restart_Click(object sender, RoutedEventArgs e) => await Run(HostControl.RestartAsync);

    private async void Stop_Click(object sender, RoutedEventArgs e) => await Run(HostControl.StopAsync);

    // MacWin is the one that asks GitHub: when it isn't running, the button starts it first.
    private async void Check_Click(object sender, RoutedEventArgs e)
    {
        if (!HostControl.IsRunning)
        {
            await Run(async () =>
            {
                HostControl.Start();
                for (int i = 0; i < 50 && !HostControl.IsRunning; i++) await Task.Delay(200);
            });
            if (!HostControl.IsRunning) return;
        }
        // A host that doesn't answer is a failed check, not silence.
        _update = _update with { Status = UpdateClient.CheckNow() ? "checking" : "failed" };
        UpdateStatus();
    }

    private void Install_Click(object sender, RoutedEventArgs e) => UpdateClient.Install();

    // The release notes in the update sheet; the release page when MacWin has none.
    private void WhatsNew_Click(object sender, RoutedEventArgs e)
    {
        if (!string.IsNullOrEmpty(_update.Notes) && Window.GetWindow(this) is MainWindow window) window.ShowUpdate(_update);
        else HostControl.OpenUrl(_update.NotesUrl ?? Project.ReleasesUrl);
    }
}
