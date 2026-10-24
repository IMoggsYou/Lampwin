// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.ComponentModel;
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
    public static readonly string[] Kinds = ["clock", "calendar", "weather", "system", "notes", "photo"];
    public static readonly string[] SizeNames = ["small", "medium", "large"];
    public static readonly string[] ThemeNames = ["auto", "light", "dark"];

    public event PropertyChangedEventHandler? PropertyChanged;
    /// <summary>A setting of this widget changed.</summary>
    public event Action? Changed;

    public required string Id { get; init; }
    public required string Kind { get; init; }

    /// <summary>A clock or the system gauges come in two sizes only (like the host's Supports).</summary>
    public static bool Supports(string kind, string size) => size != "large" || kind is not ("clock" or "system");

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
    /// <summary>Notes: the note (its first line is the title).</summary>
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

    public string Title => Loc.T("settings.widgets.kind." + Kind);
    public string PathName => _path.Length > 0 ? System.IO.Path.GetFileName(_path) : Loc.T("settings.widgets.photo.none");
    /// <summary>The weather has the sky's colors and a photo its own: no light or dark for them.</summary>
    public bool HasTheme => Kind is not ("weather" or "photo");
    public bool IsNotes => Kind == "notes";
    public bool IsPhoto => Kind == "photo";

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
