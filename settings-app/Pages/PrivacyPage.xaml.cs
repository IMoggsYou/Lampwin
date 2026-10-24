// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Shapes;
using MacWin.Settings.Controls;

namespace MacWin.Settings.Pages;

/// <summary>
/// Windows' privacy settings (PrivacyCatalog). A switch shows what the PC does now, read
/// every time the page is shown; turning switches only marks them, and Apply changes them
/// all at once (one administrator prompt), backed up first (PrivacyEngine).
/// </summary>
public partial class PrivacyPage : UserControl
{
    private sealed class Row(PrivacyTweak tweak, SettingRow setting, CheckBox toggle, TextBlock pending)
    {
        public PrivacyTweak Tweak { get; } = tweak;
        public SettingRow Setting { get; } = setting;
        public CheckBox Toggle { get; } = toggle;
        public TextBlock Pending { get; } = pending;
        public TweakState State { get; set; } = TweakState.Unavailable;

        /// <summary>What the user turned the switch to, while that isn't applied yet (true: on).</summary>
        public bool? Desired { get; set; }
    }

    private readonly List<Row> _rows = new();
    private bool _read;          // the states have been read once
    private string? _work;       // the status while reading or changing; null when idle
    private string? _message;    // what the last change did, until a switch is turned
    private bool _hasBackup;

    public PrivacyPage()
    {
        InitializeComponent();
        BuildRows();
        UpdateUi();
        Loaded += async (_, _) =>
        {
            if (_work is null) await RefreshAsync();
        };
    }

    private void BuildRows()
    {
        foreach (string group in PrivacyCatalog.Groups)
        {
            var header = new TextBlock { Text = Loc.T($"settings.privacy.group.{group}") };
            header.SetResourceReference(StyleProperty, "Text.GroupHeader");
            var list = new StackPanel();
            foreach (var tweak in PrivacyCatalog.All.Where(t => t.Group == group))
            {
                if (list.Children.Count > 0)
                {
                    var separator = new Rectangle();
                    separator.SetResourceReference(StyleProperty, "RowSeparator");
                    list.Children.Add(separator);
                }
                var pending = new TextBlock
                {
                    Text = Loc.T("settings.privacy.pending"),
                    Margin = new Thickness(0, 0, 10, 0),
                    VerticalAlignment = VerticalAlignment.Center,
                    FontSize = 11.5,
                    Visibility = Visibility.Collapsed,
                };
                pending.SetResourceReference(TextBlock.ForegroundProperty, "Brush.Accent");
                var toggle = new CheckBox();
                toggle.SetResourceReference(StyleProperty, "MacSwitch");
                var content = new StackPanel { Orientation = Orientation.Horizontal };
                content.Children.Add(pending);
                content.Children.Add(toggle);
                var setting = new SettingRow { Title = Loc.T(tweak.TitleKey), Content = content };
                AutomationProperties.SetName(toggle, setting.Title);
                var row = new Row(tweak, setting, toggle, pending);
                toggle.Click += (_, _) => Toggle_Click(row);
                _rows.Add(row);
                list.Children.Add(setting);
            }
            var box = new Border { Child = list };
            box.SetResourceReference(StyleProperty, "Group");
            Groups.Children.Add(header);
            Groups.Children.Add(box);
        }
    }

    private void Toggle_Click(Row row)
    {
        bool on = row.Toggle.IsChecked == true;
        row.Desired = on == (row.State == TweakState.On) ? null : on;
        _message = null;
        UpdateUi();
    }

    // Reads what the PC has now; switches the user has turned but not applied keep their
    // position unless the PC already matches it.
    private async Task RefreshAsync()
    {
        _work ??= Loc.T("settings.privacy.status.reading");
        UpdateUi();
        try
        {
            var (states, hasBackup) = await Task.Run(() => (PrivacyRunner.ReadStates(), PrivacyRunner.HasBackup()));
            foreach (var row in _rows)
            {
                row.State = states.GetValueOrDefault(row.Tweak.Id, TweakState.Unavailable);
                if (row.State == TweakState.Unavailable || row.Desired == (row.State == TweakState.On)) row.Desired = null;
            }
            _hasBackup = hasBackup;
            _read = true;
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        finally
        {
            _work = null;
            UpdateUi();
        }
    }

    private void UpdateUi()
    {
        bool busy = _work is not null;
        foreach (var row in _rows)
        {
            bool available = row.State != TweakState.Unavailable;
            row.Setting.IsEnabled = !busy && available;
            row.Setting.Description = _read && !available ? Loc.T("settings.privacy.unavailable") : Loc.T(row.Tweak.DescriptionKey);
            row.Toggle.IsChecked = row.Desired ?? row.State == TweakState.On;
            row.Pending.Visibility = row.Desired is null ? Visibility.Collapsed : Visibility.Visible;
        }

        int pending = _rows.Count(r => r.Desired is not null);
        SummaryRow.Description = _work ?? (pending > 0 ? Loc.F("settings.privacy.status.pending", pending)
                                                       : _message ?? Loc.T("settings.privacy.status.ready"));
        var waiting = pending > 0 ? Visibility.Visible : Visibility.Collapsed;
        ApplyButton.Visibility = DiscardButton.Visibility = BottomBar.Visibility = waiting;
        BottomRow.Description = SummaryRow.Description;
        ApplyButton.IsEnabled = DiscardButton.IsEnabled = BottomApplyButton.IsEnabled = BottomDiscardButton.IsEnabled = !busy;

        RestoreRow.Description = Loc.T(_hasBackup ? "settings.privacy.restore.description" : "settings.privacy.restore.none");
        RestoreButton.IsEnabled = _hasBackup && !busy;
    }

    private void Discard_Click(object sender, RoutedEventArgs e)
    {
        foreach (var row in _rows) row.Desired = null;
        UpdateUi();
    }

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        var changes = _rows.Where(r => r.Desired is not null)
                           .Select(r => new PrivacyChange(r.Tweak, TurnOff: r.Desired == false)).ToList();
        if (changes.Count == 0 || _work is not null) return;
        _work = Loc.T("settings.privacy.status.applying");
        UpdateUi();
        IReadOnlyList<PrivacyResult> results = [];
        try
        {
            results = await PrivacyRunner.ApplyAsync(changes);
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        // A declined prompt changed nothing: the switches stay turned, to try again.
        if (!results.Any(r => r.Error == PrivacyErrors.Cancelled))
        {
            foreach (var row in _rows) row.Desired = null;
        }
        _message = Summary(results, "settings.privacy.status.applied");
        await RefreshAsync();
    }

    private async void Restore_Click(object sender, RoutedEventArgs e)
    {
        if (_work is not null || !Ask(Loc.T("settings.privacy.restore.confirm"))) return;
        foreach (var row in _rows) row.Desired = null;
        _work = Loc.T("settings.privacy.status.restoring");
        UpdateUi();
        IReadOnlyList<PrivacyResult> results = [];
        try
        {
            results = await PrivacyRunner.RestoreAsync();
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        _message = Summary(results, "settings.privacy.status.restored");
        await RefreshAsync();
    }

    // One sentence for the summary row: done, or what went wrong (the details are logged).
    private string? Summary(IReadOnlyList<PrivacyResult> results, string doneKey)
    {
        if (results.Count == 0) return null;
        if (results.All(r => r.Ok)) return Loc.T(doneKey);
        if (results.Any(r => r.Error == PrivacyErrors.Cancelled)) return Loc.T("settings.privacy.status.cancelled");
        if (results.Any(r => r.Error == PrivacyErrors.Backup)) return Loc.T("settings.privacy.status.backupFailed");
        var names = results.Where(r => !r.Ok && PrivacyCatalog.Find(r.Id) is not null)
                           .Select(r => Loc.T(PrivacyCatalog.Find(r.Id)!.TitleKey));
        return Loc.F("settings.privacy.status.failed", string.Join(Loc.T("settings.privacy.listSeparator"), names));
    }

    private bool Ask(string question) =>
        MessageBox.Show(Window.GetWindow(this) ?? Application.Current.MainWindow!, question, "MacWin",
                        MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No,
                        Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign : MessageBoxOptions.None)
        == MessageBoxResult.Yes;
}
