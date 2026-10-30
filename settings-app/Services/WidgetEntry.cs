// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;

namespace MacWin.Settings;

/// <summary>A value of a pop-up button and its text in the UI language.</summary>
public sealed record Choice(string Value, string Name);

/// <summary>
/// One widget on the desktop ("widgets.items" in settings.json). Where it sits is MacWin's
/// business (it places widgets and remembers where they were dragged), so this app never
/// writes the position (see SettingsStore.Save).
/// </summary>
public sealed class WidgetEntry : INotifyPropertyChanged
{
    /// <summary>The kinds, in the order the gallery shows them (the host's names).</summary>
    public static readonly string[] Kinds =
        ["clock", "calendar", "weather", "system", "battery", "network", "notes", "photo", "countdown", "nowPlaying"];
    public static readonly string[] SizeNames = ["small", "medium", "large"];
    public static readonly string[] ThemeNames = ["auto", "light", "dark"];

    public event PropertyChangedEventHandler? PropertyChanged;
    /// <summary>A setting of this widget changed.</summary>
    public event Action? Changed;

    public required string Id { get; init; }
    public required string Kind { get; init; }

    /// <summary>A clock, the system gauges, a countdown, now playing, the battery and the network come in
    /// two sizes only (like the host's Supports).</summary>
    public static bool Supports(string kind, string size) =>
        size != "large" || kind is not ("clock" or "system" or "countdown" or "nowPlaying" or "battery" or "network");

    public static string NewId() => "w" + Guid.NewGuid().ToString("N")[..10];

    private string _size = "small";
    public string Size
    {
        get => _size;
        set => Set(ref _size, Array.IndexOf(SizeNames, value) < 0 ? "small" : Supports(Kind, value) ? value : "medium");
    }

    private string _theme = "auto";
    public string Theme { get => _theme; set => Set(ref _theme, Array.IndexOf(ThemeNames, value) >= 0 ? value : "auto"); }

    private string _text = "";
    /// <summary>Notes: the note (its first line is the title). Countdown: its title.</summary>
    public string Text { get => _text; set => Set(ref _text, value ?? ""); }

    private string _path = "";
    /// <summary>Photo: the picture file.</summary>
    public string Path
    {
        get => _path;
        set
        {
            if (Set(ref _path, value ?? "")) Raise(nameof(PathName));
        }
    }

    private string _date = "";
    /// <summary>Countdown: the day it counts to, yyyy-MM-dd ("" until one is chosen).</summary>
    public string Date
    {
        get => _date;
        set
        {
            if (!Set(ref _date, Parse(value) is { } d ? Format(d) : "")) return;
            Raise(nameof(Day));
            Raise(nameof(Month));
            Raise(nameof(Year));
            Raise(nameof(Days));
            Raise(nameof(Years));
        }
    }

    // The day is picked in three lists like the rest of the window (a date picker would bring
    // Windows' own look): changing one part keeps the others, starting from today.
    public int? Day { get => Parse(_date)?.Day; set => SetDate(value, null, null); }
    public int? Month { get => Parse(_date)?.Month; set => SetDate(null, value, null); }
    public int? Year { get => Parse(_date)?.Year; set => SetDate(null, null, value); }

    public IReadOnlyList<int> Days =>
        Enumerable.Range(1, Parse(_date) is { } d ? DateTime.DaysInMonth(d.Year, d.Month) : 31).ToList();

    public IReadOnlyList<int> Years
    {
        get
        {
            int now = DateTime.Today.Year;
            var years = Enumerable.Range(now - 10, 31).ToList();
            if (Year is { } y && !years.Contains(y)) years.Add(y);
            return years.Order().ToList();
        }
    }

    /// <summary>Month names in the UI language, by number.</summary>
    public static IReadOnlyList<KeyValuePair<int, string>> Months
    {
        get
        {
            var culture = Controls.WidgetPreview.DateCulture();
            return Enumerable.Range(1, 12).Select(m =>
            {
                string name = culture.DateTimeFormat.GetMonthName(m);
                return new KeyValuePair<int, string>(m, name.Length > 0 ? char.ToUpper(name[0], culture) + name[1..] : name);
            }).ToList();
        }
    }

    private void SetDate(int? day, int? month, int? year)
    {
        // A list that empties itself (its items changed) sets null: nothing to change.
        if (day is null && month is null && year is null) return;
        var d = Parse(_date) ?? DateTime.Today;
        int y = Math.Clamp(year ?? d.Year, 1601, 9999), m = Math.Clamp(month ?? d.Month, 1, 12);
        Date = Format(new DateTime(y, m, Math.Clamp(day ?? d.Day, 1, DateTime.DaysInMonth(y, m))));
    }

    private static DateTime? Parse(string? date) =>
        DateTime.TryParseExact(date, "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None, out var d) &&
        d.Year >= 1601 ? d : null;

    private static string Format(DateTime d) => d.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);

    public string Title => Loc.T("settings.widgets.kind." + Kind);
    public string PathName => _path.Length > 0 ? System.IO.Path.GetFileName(_path) : Loc.T("settings.widgets.photo.none");
    /// <summary>The weather has the sky's colors and a photo its own: no light or dark for them.</summary>
    public bool HasTheme => Kind is not ("weather" or "photo");
    public bool IsNotes => Kind == "notes";
    public bool IsPhoto => Kind == "photo";
    public bool IsCountdown => Kind == "countdown";

    public IReadOnlyList<Choice> Sizes => SizeNames.Where(s => Supports(Kind, s))
                                                   .Select(s => new Choice(s, Loc.T("settings.widgets.size." + s)))
                                                   .ToList();
    public static IReadOnlyList<Choice> Themes => ThemeNames.Select(t => new Choice(t, Loc.T("settings.widgets.theme." + t)))
                                                            .ToList();

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        Raise(name);
        Changed?.Invoke();
        return true;
    }

    private void Raise([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
