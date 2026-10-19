using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace MacWin.Settings;

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
    }

    private bool _enabled = true;
    public bool Enabled { get => _enabled; set => Set(ref _enabled, value); }

    private bool _devMode = true;
    public bool DevMode { get => _devMode; set => Set(ref _devMode, value); }

    public ObservableCollection<string> DevAllowlist { get; } = new();
    public ObservableCollection<string> NoInject { get; } = new();
    public ObservableCollection<string> Excluded { get; } = new();

    private int _style;
    /// <summary>0 = modern (Potassiumuncher), 1 = classic (original).</summary>
    public int Style
    {
        get => _style;
        set
        {
            if (Set(ref _style, Math.Clamp(value, 0, 1)))
            {
                Raise(nameof(IsModern));
                Raise(nameof(StyleDescription));
            }
        }
    }
    public bool IsModern => _style == 0;
    public string StyleDescription => _style == 0
        ? "Płynna krzywa „lampy” z silnika Potassiumunchera, liczona na karcie graficznej."
        : "Oryginalny efekt lejka z pierwszej wersji moda, z wygaszaniem na końcu.";

    private double _durationMs = 450;
    public double DurationMs { get => _durationMs; set => Set(ref _durationMs, Math.Round(Math.Clamp(value, 50, 2000))); }

    private double _meshQuality = 48;
    public double MeshQuality { get => _meshQuality; set => Set(ref _meshQuality, Math.Round(Math.Clamp(value, 8, 96))); }

    private bool _restore = true;
    public bool Restore { get => _restore; set => Set(ref _restore, value); }

    private bool _launch;
    public bool Launch { get => _launch; set => Set(ref _launch, value); }

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

    public string AutostartDescription => MacWin.Settings.Autostart.IsInstalled
        ? "MacWin startuje po zalogowaniu z uprawnieniami administratora, bez pytania UAC. Zmiana wymaga jednego potwierdzenia."
        : "MacWin startuje po zalogowaniu i siedzi w zasobniku.";

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
