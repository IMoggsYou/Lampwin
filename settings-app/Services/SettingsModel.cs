// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace MacWin.Settings;

/// <summary>A program (lowercase file name) and the library icon it shows.</summary>
public sealed record AppIconChoice(string Program, string IconId);

/// <summary>Everything the pages bind to. Mirrors settings.json (see SettingsStore).</summary>
public sealed class SettingsModel : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    /// <summary>Any user-visible value changed (properties or list contents).</summary>
    public event Action? Changed;

    public SettingsModel()
    {
        foreach (var list in new[] { DevAllowlist, NoInject, Excluded })
            list.CollectionChanged += (_, _) => Changed?.Invoke();
        AppIconChoices.CollectionChanged += (_, _) => Changed?.Invoke();
        Widgets.CollectionChanged += (_, e) =>
        {
            foreach (WidgetEntry w in e.OldItems ?? Array.Empty<WidgetEntry>()) w.Changed -= OnWidgetChanged;
            foreach (WidgetEntry w in e.NewItems ?? Array.Empty<WidgetEntry>()) w.Changed += OnWidgetChanged;
            Raise(nameof(HasWidgets));
            Changed?.Invoke();
        };
    }

    private void OnWidgetChanged() => Changed?.Invoke();

    private bool _appIcons = true;
    /// <summary>Programs show the icons chosen on the App icons page.</summary>
    public bool AppIcons { get => _appIcons; set => Set(ref _appIcons, value); }

    /// <summary>Which library icon each program shows ("appIcons.apps" in settings.json).
    /// The files themselves are in AppIconStore's folders.</summary>
    public ObservableCollection<AppIconChoice> AppIconChoices { get; } = new();

    /// <summary>The widgets on the desktop, in the order they were added.</summary>
    public ObservableCollection<WidgetEntry> Widgets { get; } = new();
    public bool HasWidgets => Widgets.Count > 0;

    private bool _widgetsShow = true;
    public bool WidgetsShow { get => _widgetsShow; set => Set(ref _widgetsShow, value); }

    private double _widgetScale = 100;
    /// <summary>Percent of the normal widget size.</summary>
    public double WidgetScale { get => _widgetScale; set => Set(ref _widgetScale, Math.Round(Math.Clamp(value, 80, 130))); }

    private bool _fahrenheit;
    public bool Fahrenheit
    {
        get => _fahrenheit;
        set
        {
            if (Set(ref _fahrenheit, value)) Raise(nameof(UnitIndex));
        }
    }
    /// <summary>0 = °C, 1 = °F (the segmented control).</summary>
    public int UnitIndex { get => _fahrenheit ? 1 : 0; set => Fahrenheit = value == 1; }

    private bool _clockSeconds = true;
    public bool ClockSeconds { get => _clockSeconds; set => Set(ref _clockSeconds, value); }

    /// <summary>The weather's place: its name and coordinates (from Open-Meteo's search).</summary>
    public string PlaceName { get; private set; } = "";
    public double PlaceLatitude { get; private set; }
    public double PlaceLongitude { get; private set; }
    public bool HasPlace => PlaceName.Length > 0;

    private bool _autoPlace = true;
    /// <summary>The weather where Windows says this PC is; the chosen place when that isn't known.</summary>
    public bool AutoPlace
    {
        get => _autoPlace;
        set
        {
            if (Set(ref _autoPlace, value)) Raise(nameof(PlaceRowDescription));
        }
    }

    /// <summary>The chosen place, or (with "my location") what it is used for.</summary>
    public string PlaceRowDescription => !_autoPlace
        ? (HasPlace ? PlaceName : Loc.T("settings.widgets.place.none"))
        : HasPlace ? Loc.F("settings.widgets.place.fallback", PlaceName) : Loc.T("settings.widgets.place.fallbackNone");

    public void SetPlace(string name, double latitude, double longitude)
    {
        if (name == PlaceName && latitude == PlaceLatitude && longitude == PlaceLongitude) return;
        PlaceName = name;
        PlaceLatitude = latitude;
        PlaceLongitude = longitude;
        Raise(nameof(PlaceName));
        Raise(nameof(HasPlace));
        Raise(nameof(PlaceRowDescription));
        Changed?.Invoke();
    }

    private bool _enabled = true;
    public bool Enabled { get => _enabled; set => Set(ref _enabled, value); }

    private bool _devMode;
    public bool DevMode { get => _devMode; set => Set(ref _devMode, value); }

    public ObservableCollection<string> DevAllowlist { get; } = new();
    public ObservableCollection<string> NoInject { get; } = new();
    public ObservableCollection<string> Excluded { get; } = new();

    private int _style;
    /// <summary>0 = modern (Potassiumuncher), 1 = classic (original), 2 = scale.</summary>
    public int Style
    {
        get => _style;
        set
        {
            if (Set(ref _style, Math.Clamp(value, 0, StyleNames.Length - 1)))
            {
                Raise(nameof(IsModern));
                Raise(nameof(StyleDescription));
            }
        }
    }
    /// <summary>settings.json names of the styles, by index.</summary>
    public static readonly string[] StyleNames = { "modern", "classic", "scale" };
    public bool IsModern => _style == 0;
    public string StyleDescription => Loc.T($"settings.animations.style.{StyleNames[_style]}.description");

    private double _durationMs = 450;
    public double DurationMs { get => _durationMs; set => Set(ref _durationMs, Math.Round(Math.Clamp(value, 50, 2000))); }

    private double _meshQuality = 48;
    public double MeshQuality { get => _meshQuality; set => Set(ref _meshQuality, Math.Round(Math.Clamp(value, 8, 96))); }

    private double _cornerRadius = 15;
    /// <summary>Window corner radius, px at 100 % scaling (genie, every window, this window).</summary>
    public double CornerRadius { get => _cornerRadius; set => Set(ref _cornerRadius, Math.Round(Math.Clamp(value, 0, 40))); }

    private double _cornerSmoothing = 100;
    /// <summary>Figma-style corner smoothing in percent: 0 = Windows 11, 100 = macOS squircle.</summary>
    public double CornerSmoothing
    {
        get => _cornerSmoothing;
        set
        {
            if (Set(ref _cornerSmoothing, Math.Round(Math.Clamp(value, 0, 100)))) Raise(nameof(CornerShape));
        }
    }

    private bool _windowCorners = true;
    /// <summary>Cut every window to these corners, not only the window in the genie.</summary>
    public bool WindowCorners { get => _windowCorners; set => Set(ref _windowCorners, value); }

    /// <summary>0 = Windows 11 (round, 8 px), 1 = macOS (squircle, 15 px).</summary>
    public int CornerShape
    {
        get => _cornerSmoothing > 0 ? 1 : 0;
        set
        {
            if (value == CornerShape) return;
            CornerSmoothing = value == 1 ? 100 : 0;
            CornerRadius = value == 1 ? 15 : 8;
        }
    }

    private bool _restore = true;
    public bool Restore { get => _restore; set => Set(ref _restore, value); }

    /// <summary>settings.json names of the open / close effects, in segment order.</summary>
    public static readonly string[] EffectNames = ["system", "genie"];

    private int _openEffect = 1;
    /// <summary>0 = Windows, 1 = genie.</summary>
    public int OpenEffect
    {
        get => _openEffect;
        set
        {
            if (Set(ref _openEffect, Math.Clamp(value, 0, EffectNames.Length - 1)))
            {
                Raise(nameof(OpenDescription));
                Raise(nameof(HasOpenEffect));
            }
        }
    }
    public bool HasOpenEffect => _openEffect != 0;
    public string OpenDescription => Loc.T(_openEffect == 1
        ? "settings.animations.open.genie.description"
        : "settings.animations.effect.windows.description");

    private int _closeEffect = 1;
    /// <summary>0 = Windows, 1 = genie.</summary>
    public int CloseEffect
    {
        get => _closeEffect;
        set
        {
            if (Set(ref _closeEffect, Math.Clamp(value, 0, EffectNames.Length - 1)))
            {
                Raise(nameof(CloseDescription));
                Raise(nameof(HasCloseEffect));
            }
        }
    }
    public bool HasCloseEffect => _closeEffect != 0;
    public string CloseDescription => Loc.T(_closeEffect == 1
        ? "settings.animations.close.genie.description"
        : "settings.animations.effect.windows.description");

    private double _openDurationMs = 450;
    public double OpenDurationMs { get => _openDurationMs; set => Set(ref _openDurationMs, Math.Round(Math.Clamp(value, 100, 1500))); }

    private double _closeDurationMs = 400;
    public double CloseDurationMs { get => _closeDurationMs; set => Set(ref _closeDurationMs, Math.Round(Math.Clamp(value, 100, 1500))); }

    private bool _multiMonitor;
    public bool MultiMonitor { get => _multiMonitor; set => Set(ref _multiMonitor, value); }

    private bool _unhideTaskbar = true;
    public bool UnhideTaskbar { get => _unhideTaskbar; set => Set(ref _unhideTaskbar, value); }

    private double _unhideDurationMs = 450;
    public double UnhideDurationMs { get => _unhideDurationMs; set => Set(ref _unhideDurationMs, Math.Round(Math.Clamp(value, 0, 5000))); }

    private bool _autostart;
    /// <summary>Not part of settings.json: the scheduled task or HKCU\...\Run (see Autostart).
    /// The switch follows what really happened (e.g. a declined UAC prompt flips it back).</summary>
    public bool Autostart
    {
        get => _autostart;
        set
        {
            if (_autostart == value) return;
            _autostart = value;
            Raise();
            if (!SuppressSideEffects) _ = ApplyAutostartAsync(value);
        }
    }

    public string AutostartDescription => Loc.T(MacWin.Settings.Autostart.IsInstalled
        ? "settings.general.autostart.description.installed"
        : "settings.general.autostart.description");

    private string _language = "auto";
    /// <summary>"auto" (the Windows display language) or one of <see cref="Loc.Languages"/>.</summary>
    public string Language
    {
        get => _language;
        set => Set(ref _language, string.IsNullOrEmpty(value) ? "auto" : value);
    }

    private bool _updateCheck = true;
    /// <summary>MacWin asks GitHub once a day whether a new version is out.</summary>
    public bool UpdateCheck { get => _updateCheck; set => Set(ref _updateCheck, value); }

    private bool _updateAutoInstall = true;
    /// <summary>New versions are installed without asking.</summary>
    public bool UpdateAutoInstall { get => _updateAutoInstall; set => Set(ref _updateAutoInstall, value); }

    private bool _updatePrompt = true;
    /// <summary>Without automatic installs: the settings window offers a new version when it opens.</summary>
    public bool UpdatePrompt { get => _updatePrompt; set => Set(ref _updatePrompt, value); }

    private async Task ApplyAutostartAsync(bool value)
    {
        await MacWin.Settings.Autostart.SetAsync(value);
        await RefreshAutostartAsync();
    }

    /// <summary>Reads the real state off the UI thread (the Task Scheduler is slow to ask).</summary>
    public async Task RefreshAutostartAsync()
    {
        bool actual = await Task.Run(MacWin.Settings.Autostart.IsEnabled);
        if (actual == _autostart) return;
        _autostart = actual;
        Raise(nameof(Autostart));
    }

    /// <summary>Set while the store fills the model from disk.</summary>
    internal bool SuppressSideEffects { get; set; }

    public static void Replace(ObservableCollection<string> target, IEnumerable<string> items)
    {
        target.Clear();
        foreach (var item in items) target.Add(item);
    }

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
