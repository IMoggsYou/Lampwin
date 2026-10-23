// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net.Http;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using Microsoft.Win32;

namespace MacWin.Settings.Pages;

public partial class WidgetsPage : UserControl
{
    private static readonly HttpClient Http = CreateHttp();
    private int _search;   // the search whose results are wanted (a newer one wins)

    public sealed record Tile(string Kind, string Name);

    /// <summary>A place Open-Meteo found: its name on the widget, the longer one in the list.</summary>
    public sealed record Place(string Name, string Label, double Latitude, double Longitude);

    // MacWin writes what "my location" found here (src/host/widgets/widget_host.cpp).
    private static readonly string LocationFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "location.json");
    private FileSystemWatcher? _locationWatcher;
    private SettingsModel? _watched;

    public WidgetsPage()
    {
        InitializeComponent();
        Gallery.ItemsSource = WidgetEntry.Kinds.Select(k => new Tile(k, Loc.T("settings.widgets.kind." + k))).ToList();
        Loaded += (_, _) =>
        {
            WatchModel();
            WatchLocation();
            ShowLocation();
        };
        Unloaded += (_, _) =>
        {
            _locationWatcher?.Dispose();
            _locationWatcher = null;
            if (_watched is not null) _watched.PropertyChanged -= Model_PropertyChanged;
            _watched = null;
        };
        DataContextChanged += (_, _) =>
        {
            WatchModel();
            ShowLocation();
        };
    }

    private void WatchModel()
    {
        if (_watched is not null) _watched.PropertyChanged -= Model_PropertyChanged;
        _watched = Model;
        if (_watched is not null) _watched.PropertyChanged += Model_PropertyChanged;
    }

    private void Model_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(SettingsModel.AutoPlace)) ShowLocation();
    }

    private void WatchLocation()
    {
        if (_locationWatcher is not null) return;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LocationFile)!);
            _locationWatcher = new FileSystemWatcher(Path.GetDirectoryName(LocationFile)!, Path.GetFileName(LocationFile))
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName,
                EnableRaisingEvents = true,
            };
            FileSystemEventHandler changed = (_, _) => Dispatcher.BeginInvoke(ShowLocation);
            _locationWatcher.Changed += changed;
            _locationWatcher.Created += changed;
            _locationWatcher.Renamed += (_, _) => Dispatcher.BeginInvoke(ShowLocation);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            _locationWatcher = null;
        }
    }

    // Under the switch: the town Windows' location gave, or why there is none.
    private void ShowLocation()
    {
        string status = "", place = "";
        try
        {
            if (Model is { AutoPlace: true } && File.Exists(LocationFile) &&
                JsonNode.Parse(File.ReadAllText(LocationFile)) is JsonObject o)
            {
                status = Text(o, "status");
                place = Text(o, "place");
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
        {
            // Being rewritten right now: the watcher brings the new one.
        }
        string text = status switch
        {
            "found" when place.Length > 0 => Loc.F("settings.widgets.location.found", place),
            "denied" => Loc.T("settings.widgets.location.denied"),
            "rough" => Loc.T("settings.widgets.location.rough"),
            "unavailable" => Loc.T("settings.widgets.location.unavailable"),
            _ => "",
        };
        LocationText.Text = text;
        LocationIcon.Visibility = status == "found" ? Visibility.Visible : Visibility.Collapsed;
        LocationSettings.Visibility = status is "denied" or "rough" ? Visibility.Visible : Visibility.Collapsed;
        LocationRow.Visibility = text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private void LocationSettings_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("ms-settings:privacy-location") { UseShellExecute = true })?.Dispose();
    }

    private SettingsModel? Model => DataContext as SettingsModel;

    private void Add_Click(object sender, RoutedEventArgs e)
    {
        if (Model is not { } model || sender is not Button { CommandParameter: string kind }) return;
        model.Widgets.Add(new WidgetEntry { Id = WidgetEntry.NewId(), Kind = kind });
        // Adding one means wanting to see it.
        model.WidgetsShow = true;
        // Without "my location" the weather needs a place first: straight to where it is chosen.
        if (kind == "weather" && !model.AutoPlace && !model.HasPlace)
        {
            PlaceBox.BringIntoView();
            PlaceBox.Focus();
        }
    }

    private void Remove_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: WidgetEntry entry }) Model?.Widgets.Remove(entry);
    }

    private void ChoosePhoto_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { DataContext: WidgetEntry entry }) return;
        var dialog = new OpenFileDialog
        {
            Title = Loc.T("settings.widgets.photo.dialog.title"),
            Filter = Loc.T("settings.widgets.photo.dialog.filter") +
                     "|*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.heic;*.webp",
            InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures),
        };
        if (dialog.ShowDialog(Window.GetWindow(this)) == true) entry.Path = dialog.FileName;
    }

    private void PlaceBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        e.Handled = true;
        _ = SearchAsync();
    }

    private void Search_Click(object sender, RoutedEventArgs e) => _ = SearchAsync();

    // Open-Meteo's geocoding (free, no key): names in the UI language where it has them.
    private async Task SearchAsync()
    {
        string query = PlaceBox.Text.Trim();
        if (query.Length < 2) return;
        int search = ++_search;
        ShowStatus(Loc.T("settings.widgets.place.searching"));
        Results.Visibility = Visibility.Collapsed;
        List<Place> found;
        try
        {
            string language = Loc.Code switch
            {
                "zh-Hans" => "zh",
                "pt-BR" => "pt",
                _ => Loc.Code,
            };
            string url = "https://geocoding-api.open-meteo.com/v1/search?count=8&format=json&language=" + language +
                         "&name=" + Uri.EscapeDataString(query);
            var root = JsonNode.Parse(await Http.GetStringAsync(url)) as JsonObject;
            found = Parse(root);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or System.Text.Json.JsonException)
        {
            if (search == _search) ShowStatus(Loc.T("settings.widgets.place.error"));
            return;
        }
        if (search != _search) return;
        if (found.Count == 0)
        {
            ShowStatus(Loc.T("settings.widgets.place.notFound"));
            return;
        }
        SearchStatus.Visibility = Visibility.Collapsed;
        Results.ItemsSource = found;
        Results.SelectedIndex = -1;
        Results.Visibility = Visibility.Visible;
    }

    private static List<Place> Parse(JsonObject? root)
    {
        var places = new List<Place>();
        foreach (var node in root?["results"] as JsonArray ?? new JsonArray())
        {
            if (node is not JsonObject r) continue;
            string name = Text(r, "name");
            if (name.Length == 0 || r["latitude"] is not JsonValue lat || r["longitude"] is not JsonValue lon ||
                !lat.TryGetValue(out double latitude) || !lon.TryGetValue(out double longitude))
            {
                continue;
            }
            string label = string.Join(", ", new[] { name, Text(r, "admin1"), Text(r, "country") }
                                                 .Where(s => s.Length > 0).Distinct());
            places.Add(new Place(name, label, latitude, longitude));
        }
        return places;
    }

    private static string Text(JsonObject o, string key) =>
        o[key] is JsonValue v && v.TryGetValue(out string? s) && s is not null ? s.Trim() : "";

    private void Results_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Results.SelectedItem is not Place place || Model is not { } model) return;
        model.SetPlace(place.Name, Math.Round(place.Latitude, 4), Math.Round(place.Longitude, 4));
        Results.Visibility = Visibility.Collapsed;
        Results.ItemsSource = null;
        PlaceBox.Clear();
    }

    private void ShowStatus(string text)
    {
        SearchStatus.Text = text;
        SearchStatus.Visibility = Visibility.Visible;
    }

    private static HttpClient CreateHttp()
    {
        var http = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd($"MacWin.Settings/{Project.Version}");
        return http;
    }
}

/// <summary>No separator above the first row of a list (its PreviousData is null).</summary>
public sealed class FirstItemHidden : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is null ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}

/// <summary>Shown while a flag is off (an empty list's hint).</summary>
public sealed class VisibleWhenFalse : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
