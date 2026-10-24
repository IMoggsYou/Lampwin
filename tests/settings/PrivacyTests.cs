// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Text.Json;
using MacWin.Settings;
using Microsoft.Win32;

namespace MacWin.Settings.Tests;

/// <summary>The Privacy page's logic against a fake PC: nothing here touches the real
/// registry, services or tasks. Usage: MacWin.Settings.Tests &lt;repository root&gt;.</summary>
internal static class Program
{
    private static int _failures;
    private static string _temp = "";

    private static int Main(string[] args)
    {
        string repo = args.Length > 0 ? args[0] : Directory.GetCurrentDirectory();
        _temp = Path.Combine(Path.GetTempPath(), "MacWinTests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_temp);
        try
        {
            Run(nameof(CatalogIsSound), CatalogIsSound);
            Run(nameof(CatalogNeverTouchesSecurity), CatalogNeverTouchesSecurity);
            Run(nameof(EveryTweakHasTexts), () => EveryTweakHasTexts(repo));
            Run(nameof(DefaultPcShowsOn), DefaultPcShowsOn);
            Run(nameof(TurnOffBacksUpThenWrites), TurnOffBacksUpThenWrites);
            Run(nameof(TurnBackOnRestoresExactly), TurnBackOnRestoresExactly);
            Run(nameof(FirstOriginalWins), FirstOriginalWins);
            Run(nameof(RestoreAllPutsEverythingBack), RestoreAllPutsEverythingBack);
            Run(nameof(OffBeforeMacWinStaysOffAfterRestore), OffBeforeMacWinStaysOffAfterRestore);
            Run(nameof(PartialStateShowsOn), PartialStateShowsOn);
            Run(nameof(MissingServiceIsUnavailableAndNotCreated), MissingServiceIsUnavailableAndNotCreated);
            Run(nameof(MissingTasksAreSkipped), MissingTasksAreSkipped);
            Run(nameof(FailedWriteRollsBackTheSwitch), FailedWriteRollsBackTheSwitch);
            Run(nameof(ValueWindowsDoesNotKeepIsReported), ValueWindowsDoesNotKeepIsReported);
            Run(nameof(DamagedBackupChangesNothing), DamagedBackupChangesNothing);
            Run(nameof(RestoreIgnoresForeignEntries), RestoreIgnoresForeignEntries);
            Run(nameof(BackupKeepsOtherValueKinds), BackupKeepsOtherValueKinds);
            Run(nameof(ServiceStopsAndStarts), ServiceStopsAndStarts);
            Run(nameof(TaskXmlIsRead), TaskXmlIsRead);
            Run(nameof(HelperArgumentsAreStrict), HelperArgumentsAreStrict);
            Run(nameof(AdminIsNeededWhereWindowsRequiresIt), AdminIsNeededWhereWindowsRequiresIt);
        }
        finally
        {
            Directory.Delete(_temp, recursive: true);
        }
        Console.WriteLine(_failures == 0 ? "All privacy tests passed." : $"{_failures} privacy check(s) failed.");
        return _failures == 0 ? 0 : 1;
    }

    private static void Run(string name, Action test)
    {
        try
        {
            test();
        }
        catch (Exception ex)
        {
            Fail($"{name}: threw {ex.GetType().Name}: {ex.Message}");
        }
    }

    private static void Check(bool condition, string what)
    {
        if (!condition) Fail(what);
    }

    private static void Fail(string what)
    {
        _failures++;
        Console.Error.WriteLine("FAIL " + what);
    }

    private static (FakePc Pc, PrivacyEngine Engine, PrivacyBackupFile Backup) NewPc()
    {
        var backup = new PrivacyBackupFile(Path.Combine(_temp, Guid.NewGuid().ToString("N"), "backup.json"));
        var pc = new FakePc { BackupExists = () => File.Exists(backup.Path) };
        return (pc, new PrivacyEngine(pc, backup), backup);
    }

    private static PrivacyTweak Tweak(string id) => PrivacyCatalog.Find(id) ?? throw new InvalidOperationException(id);

    // --- the catalogue ---

    private static void CatalogIsSound()
    {
        var all = PrivacyCatalog.All;
        Check(all.Select(t => t.Id).Distinct().Count() == all.Count, "tweak ids are unique");
        var keys = all.SelectMany(t => t.Actions).Select(a => a.BackupKey).ToList();
        Check(keys.Distinct().Count() == keys.Count, "no two switches change the same value");
        Check(all.All(t => PrivacyCatalog.Groups.Contains(t.Group)), "every switch is in a known group");
        Check(PrivacyCatalog.Groups.All(g => all.Any(t => t.Group == g)), "no empty group");
        foreach (var reg in all.SelectMany(t => t.Actions).OfType<RegistryAction>())
        {
            Check(reg.On != reg.Off, $"{reg.BackupKey}: on and off differ");
            Check(!reg.Key.StartsWith('\\') && !reg.Key.EndsWith('\\'), $"{reg.BackupKey}: key without stray backslashes");
            Check(reg.Hive == PrivacyHive.User || reg.Key.StartsWith("SOFTWARE\\Policies\\", StringComparison.Ordinal) ||
                  reg.OnlyIfKeyExists, $"{reg.BackupKey}: machine values are policies or existing services");
        }
    }

    private static void CatalogNeverTouchesSecurity()
    {
        string[] forbidden =
        [
            "Defender", "WinDefend", "SmartScreen", "WindowsUpdate", "Windows Update", "wuauserv", "UsoSvc", "Firewall",
            "MpsSvc", "SecurityHealth", "EnableLUA", "DeviceGuard", "HypervisorEnforced", "LSA", "BitLocker", "Spynet",
        ];
        foreach (var action in PrivacyCatalog.All.SelectMany(t => t.Actions))
        {
            foreach (string word in forbidden)
                Check(!action.BackupKey.Contains(word, StringComparison.OrdinalIgnoreCase), $"{action.BackupKey} touches {word}");
        }
    }

    private static void EveryTweakHasTexts(string repo)
    {
        string path = Path.Combine(repo, "i18n", "en.json");
        var en = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(path))!;
        foreach (var tweak in PrivacyCatalog.All)
        {
            Check(en.ContainsKey(tweak.TitleKey), "missing " + tweak.TitleKey);
            Check(en.ContainsKey(tweak.DescriptionKey), "missing " + tweak.DescriptionKey);
        }
        foreach (string group in PrivacyCatalog.Groups)
            Check(en.ContainsKey($"settings.privacy.group.{group}"), "missing group text " + group);
    }

    // --- reading and changing ---

    private static void DefaultPcShowsOn()
    {
        var (pc, engine, _) = NewPc();
        pc.AddDefaults();
        foreach (var tweak in PrivacyCatalog.All)
            Check(engine.GetState(tweak) == TweakState.On, $"{tweak.Id} is on on a fresh PC");
    }

    private static void TurnOffBacksUpThenWrites()
    {
        var (pc, engine, backup) = NewPc();
        pc.AddDefaults();
        var adId = Tweak("adId");
        var results = engine.Apply([new PrivacyChange(adId, TurnOff: true)]);
        Check(results.Single().Ok, "adId turned off");
        Check(engine.GetState(adId) == TweakState.Off, "adId reads off");
        var entry = backup.Load().Entries[adId.Actions[0].BackupKey];
        Check(!entry.Absent && entry.Kind == "DWord" && entry.Data == "1", "the original 1 is backed up");
        Check(pc.WritesBeforeBackup == 0, "nothing written before the backup was saved");
    }

    private static void TurnBackOnRestoresExactly()
    {
        var (pc, engine, backup) = NewPc();
        var telemetry = Tweak("telemetry");   // a policy that doesn't exist on a fresh PC
        engine.Apply([new PrivacyChange(telemetry, TurnOff: true)]);
        Check(pc.Values.Count == 1, "one policy written");
        engine.Apply([new PrivacyChange(telemetry, TurnOff: false)]);
        Check(pc.Values.Count == 0, "the policy is gone again, not set to another value");
        Check(backup.Load().Entries.Count == 0, "the backup has done its job");
        Check(engine.GetState(telemetry) == TweakState.On, "telemetry reads on");
    }

    private static void FirstOriginalWins()
    {
        var (pc, engine, backup) = NewPc();
        pc.AddDefaults();
        var tips = Tweak("tips");
        engine.Apply([new PrivacyChange(tips, TurnOff: true)]);
        // Something else (Windows Settings) turns one value back on; MacWin turns it off again.
        var first = (RegistryAction)tips.Actions[0];
        pc.WriteValue(first.Hive, first.Key, first.Name, RegData.Dword(1));
        engine.Apply([new PrivacyChange(tips, TurnOff: true)]);
        Check(backup.Load().Entries[first.BackupKey].Data == "1", "the backup still has the state from before MacWin");
        engine.RestoreAll(PrivacyCatalog.All);
        Check(pc.ReadValue(first.Hive, first.Key, first.Name) == RegData.Dword(1), "restored to the first original");
    }

    private static void RestoreAllPutsEverythingBack()
    {
        var (pc, engine, backup) = NewPc();
        pc.AddDefaults();
        var before = pc.Snapshot();
        var changes = PrivacyCatalog.All.Select(t => new PrivacyChange(t, TurnOff: true)).ToList();
        var results = engine.Apply(changes);
        Check(results.All(r => r.Ok), "every switch turned off: " + string.Join(", ", results.Where(r => !r.Ok).Select(r => r.Id)));
        Check(PrivacyCatalog.All.All(t => engine.GetState(t) == TweakState.Off), "every switch reads off");
        var restored = engine.RestoreAll(PrivacyCatalog.All);
        Check(restored.All(r => r.Ok), "restore worked");
        Check(pc.Snapshot().SequenceEqual(before), "the PC is exactly as it was");
        Check(backup.Load().Entries.Count == 0, "the backup is empty after a full restore");
    }

    private static void OffBeforeMacWinStaysOffAfterRestore()
    {
        var (pc, engine, _) = NewPc();
        var welcome = Tweak("welcome");
        var a = (RegistryAction)welcome.Actions[0];
        pc.WriteValue(a.Hive, a.Key, a.Name, RegData.Dword(0));   // the user had this one off already
        engine.Apply([new PrivacyChange(welcome, TurnOff: false)]);
        Check(pc.ReadValue(a.Hive, a.Key, a.Name) == RegData.Dword(1), "turning on writes Windows' value");
        engine.RestoreAll(PrivacyCatalog.All);
        Check(pc.ReadValue(a.Hive, a.Key, a.Name) == RegData.Dword(0), "restore brings back the user's own off");
    }

    private static void PartialStateShowsOn()
    {
        var (pc, engine, _) = NewPc();
        var cloud = Tweak("cloudSearch");
        var a = (RegistryAction)cloud.Actions[0];
        pc.WriteValue(a.Hive, a.Key, a.Name, a.Off);
        Check(engine.GetState(cloud) == TweakState.On, "half off is not off");
        engine.Apply([new PrivacyChange(cloud, TurnOff: true)]);
        Check(engine.GetState(cloud) == TweakState.Off, "off once all values are set");
    }

    private static void MissingServiceIsUnavailableAndNotCreated()
    {
        var (pc, engine, _) = NewPc();
        var diag = Tweak("diagTrack");
        Check(engine.GetState(diag) == TweakState.Unavailable, "no DiagTrack service: not on this PC");
        engine.Apply([new PrivacyChange(diag, TurnOff: true)]);
        Check(pc.Values.Count == 0 && pc.Keys.Count == 0, "no service key created");
    }

    private static void MissingTasksAreSkipped()
    {
        var (pc, engine, _) = NewPc();
        var ceip = Tweak("ceip");
        var tasks = ceip.Actions.OfType<TaskAction>().ToList();
        pc.Tasks[tasks[0].Path] = true;   // only one of the tasks exists
        var results = engine.Apply([new PrivacyChange(ceip, TurnOff: true)]);
        Check(results.Single().Ok, "ceip turned off");
        Check(pc.Tasks[tasks[0].Path] == false, "the existing task is disabled");
        Check(pc.Tasks.Count == 1, "no other task appears");
        Check(engine.GetState(ceip) == TweakState.Off, "ceip reads off");
    }

    private static void FailedWriteRollsBackTheSwitch()
    {
        var (pc, engine, backup) = NewPc();
        pc.AddDefaults();
        var apps = Tweak("suggestedApps");
        var before = pc.Snapshot();
        pc.FailOn = ((RegistryAction)apps.Actions[2]).Name;   // the third of four values is refused
        var results = engine.Apply([new PrivacyChange(apps, TurnOff: true), new PrivacyChange(Tweak("adId"), TurnOff: true)]);
        Check(!results[0].Ok && results[0].Error == PrivacyErrors.Access, "the refused switch is reported as access denied");
        Check(results[1].Ok, "the next switch is still applied");
        pc.FailOn = null;
        var adIdKey = Tweak("adId").Actions[0].BackupKey;
        Check(pc.Snapshot().Where(kv => !kv.Key.Contains("AdvertisingInfo")).SequenceEqual(before.Where(kv => !kv.Key.Contains("AdvertisingInfo"))),
              "the half-done switch was put back");
        Check(engine.GetState(apps) == TweakState.On, "the refused switch still reads on");
        Check(backup.Load().Entries.ContainsKey(adIdKey), "the applied switch is backed up");
    }

    private static void ValueWindowsDoesNotKeepIsReported()
    {
        var (pc, engine, _) = NewPc();
        pc.AddDefaults();
        var adId = Tweak("adId");
        pc.IgnoreWritesTo = ((RegistryAction)adId.Actions[0]).Name;   // like a driver that silently blocks it
        var results = engine.Apply([new PrivacyChange(adId, TurnOff: true)]);
        Check(!results.Single().Ok && results.Single().Error == PrivacyErrors.NotKept, "a change Windows didn't keep is reported");
    }

    private static void DamagedBackupChangesNothing()
    {
        var (pc, engine, backup) = NewPc();
        pc.AddDefaults();
        Directory.CreateDirectory(Path.GetDirectoryName(backup.Path)!);
        File.WriteAllText(backup.Path, "{ not json");
        var before = pc.Snapshot();
        var results = engine.Apply([new PrivacyChange(Tweak("adId"), TurnOff: true)]);
        Check(results.Single().Error == PrivacyErrors.Backup, "a damaged backup is reported");
        Check(pc.Snapshot().SequenceEqual(before), "and nothing changed");
        Check(File.ReadAllText(backup.Path) == "{ not json", "the damaged file is left for the user");
        Check(engine.RestoreAll(PrivacyCatalog.All).Single().Error == PrivacyErrors.Backup, "restore reports it too");
        Check(backup.HasEntries(), "a damaged backup still offers Restore (which explains)");
    }

    private static void RestoreIgnoresForeignEntries()
    {
        var (pc, engine, backup) = NewPc();
        var data = new PrivacyBackup();
        data.Entries[@"reg|HKLM\SOFTWARE\Microsoft\Windows Defender|DisableAntiSpyware"] = new BackupEntry { Kind = "DWord", Data = "1" };
        backup.Save(data);
        var results = engine.RestoreAll(PrivacyCatalog.All);
        Check(results.All(r => r.Ok) && pc.Values.Count == 0, "a value that isn't in the catalogue is never written");
        Check(backup.Load().Entries.Count == 1, "and stays in the file");
    }

    private static void BackupKeepsOtherValueKinds()
    {
        var (pc, engine, backup) = NewPc();
        var lang = Tweak("languageList");
        var a = (RegistryAction)lang.Actions[0];
        pc.WriteValue(a.Hive, a.Key, a.Name, RegData.Sz("odd"));   // a string where Windows has a number
        engine.Apply([new PrivacyChange(lang, TurnOff: true)]);
        Check(pc.ReadValue(a.Hive, a.Key, a.Name) == a.Off, "off writes the number");
        Check(backup.Load().Entries[a.BackupKey].Kind == "String", "the string is backed up as a string");
        engine.RestoreAll(PrivacyCatalog.All);
        Check(pc.ReadValue(a.Hive, a.Key, a.Name) == RegData.Sz("odd"), "and comes back as it was");

        var round = new PrivacyBackupFile(Path.Combine(_temp, "round", "backup.json"));
        var data = new PrivacyBackup();
        data.Entries["a"] = BackupEntry.FromValue(null);
        data.Entries["b"] = BackupEntry.FromValue(new RegData(RegistryValueKind.Binary, "AAE="));
        data.Entries["c"] = BackupEntry.FromTask(true);
        round.Save(data);
        var loaded = round.Load();
        Check(loaded.Entries["a"].Absent && loaded.Entries["a"].ToValue() is null, "absent survives the file");
        Check(loaded.Entries["b"].ToValue() == new RegData(RegistryValueKind.Binary, "AAE="), "binary survives the file");
        Check(loaded.Entries["c"].Enabled == true, "a task's state survives the file");
        Check(!File.Exists(round.Path + ".tmp"), "no temporary file is left");
        bool threw = false;
        try
        {
            BackupEntry.FromValue(new RegData(RegistryValueKind.None, ""));
        }
        catch (InvalidDataException)
        {
            threw = true;
        }
        Check(threw, "a value MacWin couldn't write back is refused before anything changes");
    }

    private static void ServiceStopsAndStarts()
    {
        var (pc, engine, _) = NewPc();
        pc.AddDefaults();
        var diag = Tweak("diagTrack");
        engine.Apply([new PrivacyChange(diag, TurnOff: true)]);
        Check(pc.ServiceCalls.SequenceEqual(["DiagTrack:stop"]), "the service is stopped");
        engine.RestoreAll(PrivacyCatalog.All);
        Check(pc.ServiceCalls.SequenceEqual(["DiagTrack:stop", "DiagTrack:start"]), "and started again by restore");
        Check(pc.ReadValue(PrivacyHive.Machine, @"SYSTEM\CurrentControlSet\Services\DiagTrack", "Start") == RegData.Dword(2),
              "its start type is back");
    }

    private static void TaskXmlIsRead()
    {
        const string ns = "xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"";
        string disabled = $"﻿<?xml version=\"1.0\" encoding=\"UTF-16\"?><Task {ns}><Settings><Enabled>false</Enabled></Settings>" +
                          "<Triggers><TimeTrigger><Enabled>true</Enabled></TimeTrigger></Triggers></Task>";
        string enabled = $"<Task {ns}><Triggers><TimeTrigger><Enabled>false</Enabled></TimeTrigger></Triggers><Settings/></Task>";
        Check(!TaskXml.IsEnabled(disabled), "Settings/Enabled=false is disabled");
        Check(TaskXml.IsEnabled(enabled), "a disabled trigger doesn't make the task disabled");
    }

    private static void HelperArgumentsAreStrict()
    {
        var parsed = PrivacyCatalog.ParseChanges("telemetry=off,adId=on");
        Check(parsed is { Count: 2 } && parsed[0].TurnOff && !parsed[1].TurnOff, "a valid list parses");
        Check(PrivacyCatalog.ParseChanges(PrivacyCatalog.FormatChanges(parsed!))!.Count == 2, "format and parse agree");
        Check(PrivacyCatalog.ParseChanges("telemetry=off,unknown=off") is null, "unknown switches are refused");
        Check(PrivacyCatalog.ParseChanges("telemetry=off,telemetry=on") is null, "a switch twice is refused");
        Check(PrivacyCatalog.ParseChanges("telemetry=maybe") is null, "only on and off");
        Check(PrivacyCatalog.ParseChanges("") is null, "nothing to do is refused");
        Check(PrivacyCatalog.IsUserSid("S-1-5-21-1004336348-1177238915-682003330-1001"), "a local account's SID");
        Check(PrivacyCatalog.IsUserSid("S-1-12-1-1234567890-1234567890-1234567890-1234567890"), "an Entra ID account's SID");
        Check(!PrivacyCatalog.IsUserSid("S-1-5-18"), "not LocalSystem");
        Check(!PrivacyCatalog.IsUserSid(@"S-1-5-21-1-2-3-1001\..\.DEFAULT"), "no path tricks");
    }

    private static void AdminIsNeededWhereWindowsRequiresIt()
    {
        Check(Tweak("telemetry").NeedsAdmin, "machine policies need administrator rights");
        Check(Tweak("webSearch").NeedsAdmin, "HKCU policies need administrator rights");
        Check(!Tweak("adId").NeedsAdmin, "the user's own settings don't");
        Check(Tweak("ceip").NeedsAdmin, "tasks need administrator rights");
    }
}

/// <summary>A PC in memory. Keys are "HKLM\path" / "HKCU\path", values "key|name".</summary>
internal sealed class FakePc : IPrivacySystem
{
    public SortedDictionary<string, RegData> Values { get; } = new(StringComparer.OrdinalIgnoreCase);
    public HashSet<string> Keys { get; } = new(StringComparer.OrdinalIgnoreCase);
    public Dictionary<string, bool> Tasks { get; } = new(StringComparer.OrdinalIgnoreCase);
    public List<string> ServiceCalls { get; } = new();

    /// <summary>A value name whose writes throw UnauthorizedAccessException.</summary>
    public string? FailOn { get; set; }

    /// <summary>A value name whose writes "succeed" but change nothing.</summary>
    public string? IgnoreWritesTo { get; set; }

    /// <summary>Changes made while there was no backup file yet.</summary>
    public int WritesBeforeBackup { get; private set; }
    public Func<bool>? BackupExists { get; set; }

    private void Changing()
    {
        if (BackupExists?.Invoke() == false) WritesBeforeBackup++;
    }

    private static string KeyName(PrivacyHive hive, string key) => (hive == PrivacyHive.Machine ? "HKLM\\" : "HKCU\\") + key;

    /// <summary>What a fresh Windows 11 has: the values its Settings app writes, the service
    /// and the tasks; no policies.</summary>
    public void AddDefaults()
    {
        foreach (var action in PrivacyCatalog.All.SelectMany(t => t.Actions))
        {
            if (action is TaskAction task) Tasks[task.Path] = true;
            else if (action is RegistryAction reg && reg.On is not null)
            {
                Keys.Add(KeyName(reg.Hive, reg.Key));
                Values[KeyName(reg.Hive, reg.Key) + "|" + reg.Name] = reg.On;
            }
        }
    }

    public SortedDictionary<string, string> Snapshot()
    {
        var all = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var (k, v) in Values) all[k] = v.Kind + ":" + v.Text;
        foreach (var (k, v) in Tasks) all["task|" + k] = v.ToString();
        return all;
    }

    public RegData? ReadValue(PrivacyHive hive, string key, string name) =>
        Values.TryGetValue(KeyName(hive, key) + "|" + name, out var v) ? v : null;

    public bool KeyExists(PrivacyHive hive, string key) => Keys.Contains(KeyName(hive, key));

    public void WriteValue(PrivacyHive hive, string key, string name, RegData data)
    {
        if (name == FailOn) throw new UnauthorizedAccessException("Access denied (test)");
        if (name == IgnoreWritesTo) return;
        Changing();
        Keys.Add(KeyName(hive, key));
        Values[KeyName(hive, key) + "|" + name] = data;
    }

    public void DeleteValue(PrivacyHive hive, string key, string name)
    {
        if (name == FailOn) throw new UnauthorizedAccessException("Access denied (test)");
        Changing();
        Values.Remove(KeyName(hive, key) + "|" + name);
    }

    public bool? IsTaskEnabled(string path) => Tasks.TryGetValue(path, out bool enabled) ? enabled : null;

    public void SetTaskEnabled(string path, bool enabled)
    {
        if (!Tasks.ContainsKey(path)) throw new InvalidOperationException("no such task");
        Changing();
        Tasks[path] = enabled;
    }

    public void SetServiceRunning(string name, bool running) => ServiceCalls.Add($"{name}:{(running ? "start" : "stop")}");
}
