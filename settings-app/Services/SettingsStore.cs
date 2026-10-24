// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

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
            m.DevMode = Bool(_root, "devMode", false);
            SettingsModel.Replace(m.DevAllowlist, Strings(_root, "devAllowlist", DefaultDevAllowlist));
            SettingsModel.Replace(m.NoInject, Strings(_root, "noInject", DefaultNoInject));
            m.Language = Str(_root, "language", "auto");
            var updates = _root["updates"] as JsonObject ?? new JsonObject();
            m.UpdateCheck = Bool(updates, "check", true);
            m.UpdateAutoInstall = Bool(updates, "autoInstall", true);
            m.UpdatePrompt = Bool(updates, "prompt", true);
            var genie = _root["genie"] as JsonObject ?? new JsonObject();
            m.Style = Math.Max(0, System.Array.IndexOf(SettingsModel.StyleNames, Str(genie, "style", "modern")));
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
            var icons = _root["appIcons"] as JsonObject ?? new JsonObject();
            m.AppIcons = Bool(icons, "enabled", true);
            var apps = icons["apps"] as JsonObject ?? new JsonObject();
            var choices = apps.Where(kv => kv.Value is JsonValue v && v.TryGetValue(out string? _))
                .Select(kv => new AppIconChoice(kv.Key.ToLowerInvariant(), (string)kv.Value!)).ToList();
            if (!choices.SequenceEqual(m.AppIconChoices))
            {
                m.AppIconChoices.Clear();
                foreach (var c in choices) m.AppIconChoices.Add(c);
            }
            var widgets = _root["widgets"] as JsonObject ?? new JsonObject();
            m.WidgetsShow = Bool(widgets, "show", true);
            m.WidgetScale = Num(widgets, "scale", 100);
            m.Fahrenheit = Str(widgets, "units", "c") == "f";
            m.ClockSeconds = Bool(widgets, "clockSeconds", true);
            m.AutoPlace = Bool(widgets, "autoPlace", true);
            var place = widgets["place"] as JsonObject ?? new JsonObject();
            double lat = Num(place, "latitude", double.NaN), lon = Num(place, "longitude", double.NaN);
            bool hasPlace = Str(place, "name", "").Length > 0 && Math.Abs(lat) <= 90 && Math.Abs(lon) <= 180;
            m.SetPlace(hasPlace ? Str(place, "name", "") : "", hasPlace ? lat : 0, hasPlace ? lon : 0);
            SyncWidgets(m.Widgets, widgets["items"] as JsonArray);
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
        _root["language"] = m.Language;
        var updates = _root["updates"] as JsonObject ?? new JsonObject();
        updates["check"] = m.UpdateCheck;
        updates["autoInstall"] = m.UpdateAutoInstall;
        updates["prompt"] = m.UpdatePrompt;
        _root["updates"] = updates;
        var genie = _root["genie"] as JsonObject ?? new JsonObject();
        genie["style"] = SettingsModel.StyleNames[m.Style];
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
        var icons = _root["appIcons"] as JsonObject ?? new JsonObject();
        icons["enabled"] = m.AppIcons;
        var apps = new JsonObject();
        foreach (var c in m.AppIconChoices) apps[c.Program] = c.IconId;
        icons["apps"] = apps;
        _root["appIcons"] = icons;
        _root["widgets"] = SaveWidgets(m, _root["widgets"] as JsonObject ?? new JsonObject());

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

    /// <summary>The "language" setting, read before any window exists.</summary>
    public static string ReadLanguage() => Str(ReadRoot(), "language", "auto");

    // The same defaults as the host (src/host/settings_store.h).
    private static readonly string[] DefaultDevAllowlist = ["notepad.exe", "macwintestwindow.exe"];
    public static readonly string[] DefaultNoInject =
    [
        "robloxplayerbeta.exe", "valorant-win64-shipping.exe", "fortniteclient-win64-shipping.exe",
        "r5apex.exe", "r5apex_dx12.exe", "cs2.exe", "overwatch.exe", "cod.exe", "wow.exe",
        @"\steamapps\common\", @"\epic games\", @"\riot games\", @"\xboxgames\", @"\ea games\",
        @"\ubisoft game launcher\games\", @"\gog galaxy\games\",
    ];

    // Entries are kept (and updated) rather than replaced, so a note being typed keeps its
    // text box when MacWin writes the file.
    private static void SyncWidgets(System.Collections.ObjectModel.ObservableCollection<WidgetEntry> target, JsonArray? items)
    {
        var wanted = new List<(string Id, string Kind, JsonObject Item)>();
        foreach (var node in items ?? new JsonArray())
        {
            if (node is not JsonObject o) continue;
            string id = Str(o, "id", ""), kind = Str(o, "type", "");
            if (id.Length > 0 && System.Array.IndexOf(WidgetEntry.Kinds, kind) >= 0 && wanted.All(w => w.Id != id))
                wanted.Add((id, kind, o));
        }
        for (int i = target.Count - 1; i >= 0; i--)
        {
            if (!wanted.Any(w => w.Id == target[i].Id && w.Kind == target[i].Kind)) target.RemoveAt(i);
        }
        for (int i = 0; i < wanted.Count; i++)
        {
            var (id, kind, o) = wanted[i];
            var entry = target.FirstOrDefault(e => e.Id == id);
            if (entry is null)
            {
                entry = new WidgetEntry { Id = id, Kind = kind };
                target.Insert(i, entry);
            }
            else if (target.IndexOf(entry) != i)
            {
                target.Move(target.IndexOf(entry), i);
            }
            entry.Size = Str(o, "size", "small");
            entry.Theme = Str(o, "theme", "auto");
            entry.Text = Str(o, "text", "");
            entry.Path = Str(o, "path", "");
        }
    }

    private static JsonObject SaveWidgets(SettingsModel m, JsonObject widgets)
    {
        widgets["show"] = m.WidgetsShow;
        widgets["scale"] = (int)m.WidgetScale;
        widgets["units"] = m.Fahrenheit ? "f" : "c";
        widgets["clockSeconds"] = m.ClockSeconds;
        widgets["autoPlace"] = m.AutoPlace;
        if (m.HasPlace)
            widgets["place"] = new JsonObject { ["name"] = m.PlaceName, ["latitude"] = m.PlaceLatitude, ["longitude"] = m.PlaceLongitude };
        else
            widgets.Remove("place");
        // Positions are MacWin's (it places widgets and saves where they are dragged): each
        // widget starts from the file as it is now, so they are never overwritten.
        var onDisk = (ReadRoot()["widgets"] as JsonObject)?["items"] as JsonArray;
        var known = widgets["items"] as JsonArray;
        var items = new JsonArray();
        foreach (var w in m.Widgets)
        {
            JsonObject? Find(JsonArray? list) =>
                list?.OfType<JsonObject>().FirstOrDefault(o => Str(o, "id", "") == w.Id)?.DeepClone() as JsonObject;
            var item = Find(onDisk) ?? Find(known) ?? new JsonObject();
            item["id"] = w.Id;
            item["type"] = w.Kind;
            item["size"] = w.Size;
            item["theme"] = w.Theme;
            if (w.IsNotes) item["text"] = w.Text;
            if (w.IsPhoto) item["path"] = w.Path;
            items.Add(item);
        }
        widgets["items"] = items;
        return widgets;
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
