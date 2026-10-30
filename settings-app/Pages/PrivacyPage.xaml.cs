// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Shapes;
using MacWin.Settings.Controls;

namespace MacWin.Settings.Pages;

/// <summary>
/// Windows' privacy settings (PrivacyCatalog), or the Tweaks page's (TweaksCatalog): the same
/// page for both. A switch shows what the PC does now, read every time the page is shown;
/// turning switches only marks them, and Apply changes them all at once (one administrator
/// prompt), backed up first (PrivacyEngine).
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

        /// <summary>The switch is on: Windows does it (Privacy), or the change is made (Tweaks).</summary>
        public bool IsOn(SwitchSet set) => set.OnMeansChanged ? State == TweakState.Off : State == TweakState.On;
    }

    private readonly List<Row> _rows = new();
    private bool _read;          // the states have been read once
    private string? _work;       // the status while reading or changing; null when idle
    private string? _message;    // what the last change did, until a switch is turned
    private bool _hasBackup;
    private readonly SwitchSet _set;

    public PrivacyPage() : this(SwitchSet.Privacy)
    {
    }

    public PrivacyPage(SwitchSet set)
    {
        _set = set;
        InitializeComponent();
        Footnote.Text = Text("footnote");
        BuildRows();
        UpdateUi();
        Loaded += async (_, _) =>
        {
            if (_work is null) await RefreshAsync();
        };
    }

    private void BuildRows()
    {
        foreach (string group in _set.Groups)
        {
            var header = new TextBlock { Text = Text($"group.{group}") };
            header.SetResourceReference(StyleProperty, "Text.GroupHeader");
            var list = new StackPanel();
            foreach (var tweak in _set.All.Where(t => t.Group == group))
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
        row.Desired = on == row.IsOn(_set) ? null : on;
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
            var (states, hasBackup) = await Task.Run(() => (PrivacyRunner.ReadStates(_set), PrivacyRunner.HasBackup(_set)));
            foreach (var row in _rows)
            {
                row.State = states.GetValueOrDefault(row.Tweak.Id, TweakState.Unavailable);
                if (row.State == TweakState.Unavailable || row.Desired == row.IsOn(_set)) row.Desired = null;
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
            row.Toggle.IsChecked = row.Desired ?? row.IsOn(_set);
            row.Pending.Visibility = row.Desired is null ? Visibility.Collapsed : Visibility.Visible;
        }

        int pending = _rows.Count(r => r.Desired is not null);
        SummaryRow.Description = _work ?? (pending > 0 ? Loc.F("settings.privacy.status.pending", pending)
                                                       : _message ?? Text("status.ready"));
        var waiting = pending > 0 ? Visibility.Visible : Visibility.Collapsed;
        ApplyButton.Visibility = DiscardButton.Visibility = BottomBar.Visibility = waiting;
        BottomRow.Description = SummaryRow.Description;
        ApplyButton.IsEnabled = DiscardButton.IsEnabled = BottomApplyButton.IsEnabled = BottomDiscardButton.IsEnabled = !busy;

        RestoreRow.Description = Text(_hasBackup ? "restore.description" : "restore.none");
        RestoreButton.IsEnabled = _hasBackup && !busy;
    }

    private void Discard_Click(object sender, RoutedEventArgs e)
    {
        foreach (var row in _rows) row.Desired = null;
        UpdateUi();
    }

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        // "Off" in the catalogues is the change: on the Tweaks page that is a switch turned on.
        var changes = _rows.Where(r => r.Desired is not null)
                           .Select(r => new PrivacyChange(r.Tweak, TurnOff: r.Desired == _set.OnMeansChanged)).ToList();
        if (changes.Count == 0 || _work is not null) return;
        _work = Loc.T("settings.privacy.status.applying");
        UpdateUi();
        IReadOnlyList<PrivacyResult> results = [];
        try
        {
            results = await PrivacyRunner.ApplyAsync(_set, changes);
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
        _message = Summary(results, "status.applied");
        await RefreshAsync();
    }

    private async void Restore_Click(object sender, RoutedEventArgs e)
    {
        if (_work is not null || !Ask(Text("restore.confirm"))) return;
        foreach (var row in _rows) row.Desired = null;
        _work = Loc.T("settings.privacy.status.restoring");
        UpdateUi();
        IReadOnlyList<PrivacyResult> results = [];
        try
        {
            results = await PrivacyRunner.RestoreAsync(_set);
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        _message = Summary(results, "status.restored");
        await RefreshAsync();
    }

    // One sentence for the summary row: done, or what went wrong (the details are logged).
    private string? Summary(IReadOnlyList<PrivacyResult> results, string doneKey)
    {
        if (results.Count == 0) return null;
        if (results.All(r => r.Ok)) return Text(doneKey);
        if (results.Any(r => r.Error == PrivacyErrors.Cancelled)) return Loc.T("settings.privacy.status.cancelled");
        if (results.Any(r => r.Error == PrivacyErrors.Backup)) return Loc.T("settings.privacy.status.backupFailed");
        var names = results.Where(r => !r.Ok && _set.Find(r.Id) is not null)
                           .Select(r => Loc.T(_set.Find(r.Id)!.TitleKey));
        return Loc.F("settings.privacy.status.failed", string.Join(Loc.T("settings.privacy.listSeparator"), names));
    }

    // The texts that say what this page's switches mean are its own (settings.tweaks.*); the
    // rest (Apply, the progress) are the Privacy page's.
    private static readonly string[] OwnTexts =
        ["footnote", "status.ready", "status.applied", "status.restored", "restore.description", "restore.none",
         "restore.confirm"];

    private string Text(string key) =>
        Loc.T(key.StartsWith("group.", StringComparison.Ordinal) || OwnTexts.Contains(key)
                  ? $"settings.{_set.Texts}.{key}"
                  : $"settings.privacy.{key}");

    private bool Ask(string question) =>
        MessageBox.Show(Window.GetWindow(this) ?? Application.Current.MainWindow!, question, Project.Name,
                        MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No,
                        Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign : MessageBoxOptions.None)
        == MessageBoxResult.Yes;
}
