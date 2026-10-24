// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;

namespace MacWin.Settings.Pages;

/// <summary>A program in a row's list: none, one on this PC, or "Other program…".</summary>
public sealed record ProgramOption(string? Exe, string Label, bool IsOther = false);

/// <summary>One library icon and the program it is given to.</summary>
public sealed class IconRow : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    internal Action<IconRow, ProgramOption?, ProgramOption?>? SelectionChanged;

    public required LibraryIcon Icon { get; init; }
    public string Name => Icon.Name;
    public required List<ProgramOption> Options { get; set; }

    private ImageSource? _preview;
    /// <summary>Decoded in the background: the list shows at once, however long it is.</summary>
    public ImageSource? Preview
    {
        get => _preview;
        set
        {
            _preview = value;
            Raise();
        }
    }

    private ProgramOption? _selected;
    public ProgramOption? Selected
    {
        get => _selected;
        set
        {
            if (Equals(_selected, value)) return;
            var old = _selected;
            _selected = value;
            Raise();
            Raise(nameof(Status));
            SelectionChanged?.Invoke(this, old, value);
        }
    }

    /// <summary>Sets the selection without acting on it (the page's own updates).</summary>
    internal void Show(ProgramOption? option)
    {
        _selected = option;
        Raise(nameof(Selected));
        Raise(nameof(Status));
    }

    public string Status => _selected?.Exe is string exe ? exe : Loc.T("settings.icons.status.free");

    private void Raise([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

/// <summary>
/// App icons: the icons the user added (and those that come with MacWin), each given to a
/// program - or to none, which keeps the icon in the library. The work happens in
/// AppIconActions, off the UI thread.
/// </summary>
public partial class AppIconsPage : UserControl
{
    private readonly ObservableCollection<IconRow> _rows = new();
    private SettingsModel? _model;
    private List<KnownProgram> _programs = [];
    private bool _busy;

    public AppIconsPage()
    {
        InitializeComponent();
        Rows.ItemsSource = _rows;
        Loaded += async (_, _) => await OnLoadedAsync();
        Unloaded += (_, _) =>
        {
            if (_model is not null) _model.PropertyChanged -= Model_PropertyChanged;
            AppIconActions.Changed -= OnActionsChanged;
            _model = null;
        };
    }

    private async Task OnLoadedAsync()
    {
        _model = DataContext as SettingsModel;
        if (_model is null) return;
        _model.PropertyChanged += Model_PropertyChanged;
        AppIconActions.Changed += OnActionsChanged;
        Rebuild();   // the library right away, the programs' names when they are found
        try
        {
            _programs = await AppIconActions.ProgramsAsync();
            Rebuild();
            // An update of a program may have put back its shortcuts with the old icon.
            var choices = _model?.AppIcons == true && !App.Snapshot ? _model.AppIconChoices.ToList() : [];
            if (choices.Count > 0 && await AppIconActions.Sta(() => ApplyShortcuts(choices)) > 0)
                TaskbarBar.Visibility = Visibility.Visible;
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
    }

    private void OnActionsChanged() => Dispatcher.BeginInvoke(Rebuild);

    private static int ApplyShortcuts(List<AppIconChoice> choices) =>
        choices.Where(c => File.Exists(AppIconStore.AppIconPath(c.Program)))
               .Sum(c => AppIconStore.SetShortcuts(c.Program, AppIconStore.AppIconPath(c.Program)));

    private async void Model_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        // Turned off: the shortcuts get their own icons back (the windows theirs from MacWin).
        if (e.PropertyName != nameof(SettingsModel.AppIcons) || _model is null || _model.SuppressSideEffects) return;
        bool on = _model.AppIcons;
        var choices = _model.AppIconChoices.ToList();
        try
        {
            await AppIconActions.Sta(() =>
            {
                if (on) return ApplyShortcuts(choices);
                foreach (var c in choices) AppIconStore.RestoreShortcuts(c.Program);
                return 0;
            });
            TaskbarBar.Visibility = Visibility.Visible;
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
    }

    // ---- rows --------------------------------------------------------------------------

    private void Rebuild()
    {
        if (_model is null) return;
        var library = AppIconStore.Library();
        var options = new List<ProgramOption> { new(null, Loc.T("settings.icons.program.none")) };
        options.AddRange(_programs.Select(p => new ProgramOption(p.Exe, Label(p))));
        // A program given an icon earlier but not found now (uninstalled, never opened).
        foreach (var c in _model.AppIconChoices)
            if (!options.Any(o => o.Exe == c.Program)) options.Add(new ProgramOption(c.Program, c.Program));
        options.Add(new ProgramOption(null, Loc.T("settings.icons.program.other"), IsOther: true));

        var previews = _rows.ToDictionary(r => r.Icon.Id, r => r.Preview, StringComparer.OrdinalIgnoreCase);
        _rows.Clear();
        foreach (var icon in library)
        {
            var row = new IconRow { Icon = icon, Options = options };
            if (previews.TryGetValue(icon.Id, out var preview)) row.Preview = preview;
            var choice = _model.AppIconChoices.FirstOrDefault(c => c.IconId == icon.Id);
            row.Show(choice is null ? options[0] : options.First(o => o.Exe == choice.Program));
            row.SelectionChanged = OnSelectionChanged;
            _rows.Add(row);
        }
        EmptyText.Visibility = _rows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        _ = LoadPreviewsAsync(_rows.Where(r => r.Preview is null).ToList());
    }

    private static async Task LoadPreviewsAsync(List<IconRow> rows)
    {
        if (rows.Count == 0) return;
        var images = await AppIconActions.Sta(() => rows.Select(r => LoadPreview(r.Icon.PreviewPath)).ToList());
        for (int i = 0; i < rows.Count; i++) rows[i].Preview = images[i];
    }

    private static string Label(KnownProgram p) =>
        string.Equals(Path.GetFileNameWithoutExtension(p.Exe), p.Name, StringComparison.OrdinalIgnoreCase)
            ? p.Exe
            : $"{p.Name} ({p.Exe})";

    private static ImageSource? LoadPreview(string path)
    {
        try
        {
            var image = new BitmapImage();
            image.BeginInit();
            image.UriSource = new Uri(path);
            image.CacheOption = BitmapCacheOption.OnLoad;                 // the file stays free
            image.CreateOptions = BitmapCreateOptions.IgnoreImageCache;   // a re-imported icon shows
            image.DecodePixelWidth = 80;
            image.EndInit();
            image.Freeze();
            return image;
        }
        catch (Exception ex) when (ex is IOException or NotSupportedException or UriFormatException or FileFormatException)
        {
            return null;
        }
    }

    private async void OnSelectionChanged(IconRow row, ProgramOption? old, ProgramOption? chosen)
    {
        if (_model is null) return;
        if (chosen?.IsOther == true)
        {
            var dialog = new OpenFileDialog { Filter = $"{Loc.T("settings.icons.exeFilter")}|*.exe" };
            if (dialog.ShowDialog(Window.GetWindow(this)) != true)
            {
                row.Show(old);
                return;
            }
            string exe = Path.GetFileName(dialog.FileName).ToLowerInvariant();
            chosen = row.Options.FirstOrDefault(o => o.Exe == exe);
            if (chosen is null)
            {
                chosen = new ProgramOption(exe, exe);
                row.Options.Insert(row.Options.Count - 1, chosen);
            }
            row.Show(chosen);
        }
        try
        {
            await AppIconActions.GiveAsync(_model, row.Icon, chosen?.Exe);
        }
        catch (Exception ex)
        {
            App.Report(ex);
            Rebuild();
            return;
        }
        // The program had another icon before: that row is free now.
        foreach (var other in _rows.Where(r => r != row && chosen?.Exe is not null && r.Selected?.Exe == chosen.Exe))
            other.Show(other.Options[0]);
        if (_model.AppIcons) TaskbarBar.Visibility = Visibility.Visible;
    }

    private async void Remove_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is not IconRow row || _model is null) return;
        try
        {
            await AppIconActions.GiveAsync(_model, row.Icon, null);
            AppIconStore.Delete(row.Icon);
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        _rows.Remove(row);
        EmptyText.Visibility = _rows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    // ---- import ------------------------------------------------------------------------

    private async void ImportFiles_Click(object sender, RoutedEventArgs e)
    {
        string patterns = string.Join(";", IconImport.Extensions.Select(x => "*" + x));
        var dialog = new OpenFileDialog { Multiselect = true, Filter = $"{Loc.T("settings.icons.iconFilter")}|{patterns}" };
        if (dialog.ShowDialog(Window.GetWindow(this)) == true) await ImportAsync(dialog.FileNames);
    }

    private async void ImportFolder_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog();
        if (dialog.ShowDialog(Window.GetWindow(this)) != true) return;
        string folder = dialog.FolderName;
        var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true };
        var files = await AppIconActions.Sta(() =>
            Directory.EnumerateFiles(folder, "*", options).Where(IconImport.IsSupported).ToList());
        await ImportAsync(files);
    }

    private Task ImportAsync(IReadOnlyList<string> files) => files.Count == 0 ? Task.CompletedTask : Work(async model =>
    {
        var progress = new Progress<(int Done, int Total)>(p =>
            ImportRow.Description = Loc.F("settings.icons.import.busy", p.Done, p.Total));
        var (added, failed) = await AppIconActions.ImportAsync(files, progress);
        int given = await AppIconActions.AutoGiveAsync(model, added);
        string done = Loc.F("settings.icons.import.done", added.Count, given);
        if (failed.Count > 0) done += " " + Loc.F("settings.icons.import.failed", string.Join(", ", failed.Take(10)));
        ImportRow.Description = done;
        return given > 0;
    });

    /// <summary>One change at a time: the buttons wait, the list is rebuilt afterwards, and
    /// the taskbar bar shows when `work` changed what programs show.</summary>
    private async Task Work(Func<SettingsModel, Task<bool>> work)
    {
        if (_busy || _model is null) return;
        var model = _model;
        _busy = true;
        Button[] buttons = [ImportFilesButton, ImportFolderButton, SaveSetButton, LoadSetButton, MatchButton, WindowsButton, RemoveAllButton];
        foreach (var b in buttons) b.IsEnabled = false;
        try
        {
            if (await AppIconActions.Exclusive(() => work(model)) && model.AppIcons) TaskbarBar.Visibility = Visibility.Visible;
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        finally
        {
            _busy = false;
            foreach (var b in buttons) b.IsEnabled = true;
            Rebuild();
        }
    }

    // ---- sets and reset ----------------------------------------------------------------

    private string SetFilter => $"{Loc.T("settings.icons.sets.filter")}|*{AppIconStore.SetExtension}";

    private async void SaveSet_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog
        {
            Filter = SetFilter,
            FileName = "MacWin" + AppIconStore.SetExtension,
            DefaultExt = AppIconStore.SetExtension,
        };
        if (dialog.ShowDialog(Window.GetWindow(this)) != true) return;
        await Work(async model =>
        {
            await AppIconActions.SaveSetAsync(model, dialog.FileName);
            SetsRow.Description = Loc.F("settings.icons.sets.saved", Path.GetFileName(dialog.FileName));
            return false;
        });
    }

    private async void LoadSet_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = SetFilter };
        if (dialog.ShowDialog(Window.GetWindow(this)) != true) return;
        await Work(async model =>
        {
            var progress = new Progress<(int Done, int Total)>(p =>
                SetsRow.Description = Loc.F("settings.icons.import.busy", p.Done, p.Total));
            try
            {
                int given = await AppIconActions.LoadSetAsync(model, dialog.FileName, progress);
                SetsRow.Description = Loc.F("settings.icons.sets.loaded", given);
            }
            catch (Exception ex) when (ex is InvalidDataException or System.Text.Json.JsonException or IOException
                                           or UnauthorizedAccessException)
            {
                SetsRow.Description = Loc.T("settings.icons.sets.invalid");
                return false;
            }
            return true;
        });
    }

    private async void Match_Click(object sender, RoutedEventArgs e) => await Work(async model =>
    {
        int given = await AppIconActions.MatchAllAsync(model);
        _programs = await AppIconActions.ProgramsAsync();
        MatchRow.Description = Loc.F("settings.icons.match.done", given);
        return given > 0;
    });

    private async void WindowsIcons_Click(object sender, RoutedEventArgs e) => await Work(async model =>
    {
        await AppIconActions.UseWindowsIconsAsync(model);
        WindowsRow.Description = Loc.T("settings.icons.windows.done");
        return true;
    });

    private async void RemoveAll_Click(object sender, RoutedEventArgs e)
    {
        int count = _rows.Count;
        if (count == 0) return;
        var answer = MessageBox.Show(Window.GetWindow(this), Loc.F("settings.icons.removeAll.confirm", count), "MacWin",
                                     MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No,
                                     Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign
                                                       : MessageBoxOptions.None);
        if (answer != MessageBoxResult.Yes) return;
        await Work(async model =>
        {
            await AppIconActions.RemoveAllAsync(model);
            _rows.Clear();   // the old previews go with them
            return true;
        });
    }

    private void Refresh_Click(object sender, RoutedEventArgs e)
    {
        AppIconStore.RefreshTaskbar();
        TaskbarBar.Visibility = Visibility.Collapsed;
    }
}
