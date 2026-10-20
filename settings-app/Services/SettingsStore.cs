using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows.Threading;

namespace MacWin.Settings;

/// <summary>
/// settings.json (%APPDATA%\MacWin) &lt;-&gt; SettingsModel. Changes are saved a moment after
/// the last edit (like macOS: no "Apply" button), then MacWin is told to reload them. Keys
/// this app does not know are preserved. Edits made elsewhere (the tray menu) are picked
/// up through a file watcher.
/// </summary>
public sealed class SettingsStore
{
    public static string Folder { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MacWin");
    public static string FilePath { get; } = Path.Combine(Folder, "settings.json");

    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private readonly DispatcherTimer _saveTimer;
    private readonly FileSystemWatcher? _watcher;
    private JsonObject _root = new();
    private bool _loading;
    private DateTime _lastWrite = DateTime.MinValue;

    public SettingsModel Model { get; } = new();

    public SettingsStore()
    {
        _saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _saveTimer.Tick += (_, _) =>
        {
            _saveTimer.Stop();
            Save();
        };
        Model.Changed += () =>
        {
            if (_loading) return;
            _saveTimer.Stop();
            _saveTimer.Start();
        };

        Directory.CreateDirectory(Folder);
        try
        {
            _watcher = new FileSystemWatcher(Folder, "settings.json")
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName,
                EnableRaisingEvents = true,
            };
            _watcher.Changed += OnFileChanged;
            _watcher.Renamed += OnFileChanged;
            _watcher.Created += OnFileChanged;
        }
        catch (IOException)
        {
            _watcher = null;
        }
    }

    private void OnFileChanged(object sender, FileSystemEventArgs e)
    {
        System.Windows.Application.Current?.Dispatcher.BeginInvoke(() =>
        {
            if ((DateTime.UtcNow - _lastWrite).TotalMilliseconds > 1500 && !_saveTimer.IsEnabled) Load();
        });
    }

    public void Load()
    {
        _loading = true;
        Model.SuppressSideEffects = true;
        try
        {
            _root = ReadRoot();
            var m = Model;
            m.Enabled = Bool(_root, "enabled", true);
            m.DevMode = Bool(_root, "devMode", true);
            SettingsModel.Replace(m.DevAllowlist, Strings(_root, "devAllowlist", ["notepad.exe", "macwintestwindow.exe"]));
            SettingsModel.Replace(m.NoInject, Strings(_root, "noInject", []));
            var genie = _root["genie"] as JsonObject ?? new JsonObject();
            m.Style = Str(genie, "style", "modern") == "classic" ? 1 : 0;
            m.DurationMs = Num(genie, "durationMs", 450);
            m.Restore = Bool(genie, "restoreAnimation", true);
            m.OpenEffect = Effect(genie, "openAnimation", 1);
            m.CloseEffect = Effect(genie, "closeAnimation", 1);
            m.OpenDurationMs = Num(genie, "openDurationMs", 450);
            m.CloseDurationMs = Num(genie, "closeDurationMs", 400);
            m.MultiMonitor = Bool(genie, "multiMonitor", false);
            m.UnhideTaskbar = Bool(genie, "unhideTaskbar", true);
            m.UnhideDurationMs = Num(genie, "unhideDurationMs", 450);
            m.MeshQuality = Num(genie, "meshQuality", 48);
            m.CornerRadius = Num(genie, "cornerRadius", 15);
            m.CornerSmoothing = Num(genie, "cornerSmoothing", 100);
            m.WindowCorners = Bool(genie, "windowCorners", true);
            SettingsModel.Replace(m.Excluded, Strings(genie, "excludedPrograms", []));
        }
        finally
        {
            Model.SuppressSideEffects = false;
            _loading = false;
        }
    }

    /// <summary>Writes a pending change right away (window closing).</summary>
    public void Flush()
    {
        if (!_saveTimer.IsEnabled) return;
        _saveTimer.Stop();
        Save();
    }

    public void Save()
    {
        var m = Model;
        _root["enabled"] = m.Enabled;
        _root["devMode"] = m.DevMode;
        _root["devAllowlist"] = Array(m.DevAllowlist);
        _root["noInject"] = Array(m.NoInject);
        var genie = _root["genie"] as JsonObject ?? new JsonObject();
        genie["style"] = m.Style == 1 ? "classic" : "modern";
        genie["durationMs"] = (int)m.DurationMs;
        genie["restoreAnimation"] = m.Restore;
        genie.Remove("launchAnimation");
        genie["openAnimation"] = SettingsModel.EffectNames[m.OpenEffect];
        genie["closeAnimation"] = SettingsModel.EffectNames[m.CloseEffect];
        genie["openDurationMs"] = (int)m.OpenDurationMs;
        genie["closeDurationMs"] = (int)m.CloseDurationMs;
        genie["multiMonitor"] = m.MultiMonitor;
        genie["unhideTaskbar"] = m.UnhideTaskbar;
        genie["unhideDurationMs"] = (int)m.UnhideDurationMs;
        genie["meshQuality"] = (int)m.MeshQuality;
        genie["cornerRadius"] = (int)m.CornerRadius;
        genie["cornerSmoothing"] = (int)m.CornerSmoothing;
        genie["windowCorners"] = m.WindowCorners;
        genie["excludedPrograms"] = Array(m.Excluded);
        _root["genie"] = genie;

        try
        {
            string tmp = FilePath + ".tmp";
            File.WriteAllText(tmp, _root.ToJsonString(WriteOptions) + "\n");
            _lastWrite = DateTime.UtcNow;
            File.Move(tmp, FilePath, overwrite: true);
            _lastWrite = DateTime.UtcNow;
            HostControl.NotifyReload();
        }
        catch (IOException)
        {
            // The file is briefly locked (MacWin writing it): try again shortly.
            _saveTimer.Start();
        }
    }

    private static JsonObject ReadRoot()
    {
        try
        {
            if (File.Exists(FilePath) && JsonNode.Parse(File.ReadAllText(FilePath)) is JsonObject obj) return obj;
        }
        catch (Exception ex) when (ex is IOException or JsonException) { }
        return new JsonObject();
    }

    private static bool Bool(JsonObject o, string key, bool fallback) =>
        o[key] is JsonValue v && v.TryGetValue(out bool b) ? b : fallback;

    private static double Num(JsonObject o, string key, double fallback) =>
        o[key] is JsonValue v && v.TryGetValue(out double d) ? d : fallback;

    private static int Effect(JsonObject o, string key, int fallback)
    {
        int i = System.Array.IndexOf(SettingsModel.EffectNames, Str(o, key, ""));
        return i >= 0 ? i : fallback;
    }

    private static string Str(JsonObject o, string key, string fallback) =>
        o[key] is JsonValue v && v.TryGetValue(out string? s) && s is not null ? s : fallback;

    private static IEnumerable<string> Strings(JsonObject o, string key, string[] fallback) =>
        o[key] is JsonArray a
            ? a.Select(n => n is JsonValue v && v.TryGetValue(out string? s) ? s : null).OfType<string>().ToList()
            : fallback;

    private static JsonArray Array(IEnumerable<string> items) => new(items.Select(s => (JsonNode)s).ToArray());
}
