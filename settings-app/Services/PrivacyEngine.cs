// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace MacWin.Settings;

// The Privacy page's logic, without WPF and without the real registry, so the tests
// (tests/settings) can run it against a fake PC. PrivacySystem.cs has the real one.

public enum PrivacyHive { Machine, User }

/// <summary>A registry value: its kind and its data as text (numbers in decimal, binary in
/// base64, a multi-string joined with '\0').</summary>
public sealed record RegData(RegistryValueKind Kind, string Text)
{
    public static RegData Dword(int value) => new(RegistryValueKind.DWord, value.ToString(CultureInfo.InvariantCulture));

    public static RegData Sz(string value) => new(RegistryValueKind.String, value);
}

/// <summary>One thing a privacy switch changes.</summary>
public abstract record PrivacyAction
{
    /// <summary>The name of what it changes in the backup; unique in the catalogue.</summary>
    public abstract string BackupKey { get; }

    public abstract bool NeedsAdmin { get; }
}

/// <summary>A registry value: <see cref="Off"/> blocks the feature, <see cref="On"/> is
/// Windows' own behaviour (null: the value is deleted). With <see cref="OnlyIfKeyExists"/>
/// a missing key means "not on this PC" and is never created (services).</summary>
public sealed record RegistryAction(PrivacyHive Hive, string Key, string Name, RegData Off, RegData? On,
                                    bool OnlyIfKeyExists = false) : PrivacyAction
{
    public override string BackupKey => $"reg|{(Hive == PrivacyHive.Machine ? "HKLM" : "HKCU")}\\{Key}|{Name}";

    // HKCU\Software\Policies only takes changes from administrators.
    public override bool NeedsAdmin =>
        Hive == PrivacyHive.Machine || Key.StartsWith(@"Software\Policies\", StringComparison.OrdinalIgnoreCase);
}

/// <summary>A scheduled task that is disabled to block the feature.</summary>
public sealed record TaskAction(string Path) : PrivacyAction
{
    public override string BackupKey => "task|" + Path;

    public override bool NeedsAdmin => true;
}

/// <summary>One switch of the Privacy page. Its texts are settings.privacy.&lt;id&gt;.title and
/// .description. <see cref="Service"/>: a service that is stopped when the switch is turned
/// off and started again when it is turned back on.</summary>
public sealed record PrivacyTweak(string Id, string Group, PrivacyAction[] Actions, string? Service = null)
{
    public string TitleKey => $"settings.privacy.{Id}.title";

    public string DescriptionKey => $"settings.privacy.{Id}.description";

    public bool NeedsAdmin => Actions.Any(a => a.NeedsAdmin);
}

/// <summary>On: Windows does it (its default). Off: blocked. Unavailable: none of what the
/// switch changes exists on this PC.</summary>
public enum TweakState { On, Off, Unavailable }

public sealed record PrivacyChange(PrivacyTweak Tweak, bool TurnOff);

/// <summary>What happened to one switch. Error is one of the codes in <see cref="PrivacyErrors"/>.</summary>
public sealed record PrivacyResult(string Id, bool Ok, string? Error = null, string? Detail = null);

public static class PrivacyErrors
{
    public const string Access = "access";       // Windows refused the change
    public const string NotKept = "notKept";     // written, but Windows reads something else back
    public const string Backup = "backup";       // no usable backup: nothing was changed
    public const string Cancelled = "cancelled"; // the administrator prompt was declined
    public const string Other = "other";
}

/// <summary>What the page reads and changes on the PC.</summary>
public interface IPrivacySystem
{
    /// <summary>Null when the value does not exist.</summary>
    RegData? ReadValue(PrivacyHive hive, string key, string name);

    bool KeyExists(PrivacyHive hive, string key);

    void WriteValue(PrivacyHive hive, string key, string name, RegData data);

    void DeleteValue(PrivacyHive hive, string key, string name);

    /// <summary>Null when there is no such task.</summary>
    bool? IsTaskEnabled(string path);

    void SetTaskEnabled(string path, bool enabled);

    /// <summary>Best effort: a service that doesn't stop or start is not an error (the
    /// start type decides after the next restart).</summary>
    void SetServiceRunning(string name, bool running);
}

/// <summary>The value or task state before MacWin first changed it.</summary>
public sealed class BackupEntry
{
    /// <summary>The registry value did not exist.</summary>
    [JsonPropertyName("absent")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public bool Absent { get; set; }

    [JsonPropertyName("kind")]
    public string? Kind { get; set; }

    [JsonPropertyName("data")]
    public string? Data { get; set; }

    /// <summary>Scheduled tasks: whether it was enabled.</summary>
    [JsonPropertyName("enabled")]
    public bool? Enabled { get; set; }

    /// <summary>Throws for a kind MacWin could not write back: such a value is never changed.</summary>
    public static BackupEntry FromValue(RegData? value)
    {
        if (value is null) return new BackupEntry { Absent = true };
        if (!PrivacyBackup.IsSupported(value.Kind)) throw new InvalidDataException($"Unsupported registry value kind {value.Kind}");
        return new BackupEntry { Kind = value.Kind.ToString(), Data = value.Text };
    }

    public static BackupEntry FromTask(bool enabled) => new() { Enabled = enabled };

    /// <summary>The value to write back; null: delete it. Throws on an entry that makes no sense.</summary>
    public RegData? ToValue()
    {
        if (Absent) return null;
        if (!Enum.TryParse(Kind, out RegistryValueKind kind) || Data is null || !PrivacyBackup.IsSupported(kind))
            throw new InvalidDataException("Bad registry entry in the privacy backup");
        return new RegData(kind, Data);
    }
}

/// <summary>The originals of everything MacWin changed, by <see cref="PrivacyAction.BackupKey"/>.
/// The first original wins: a value changed twice keeps the state from before MacWin.</summary>
public sealed class PrivacyBackup
{
    [JsonPropertyName("version")]
    public int Version { get; set; } = 1;

    [JsonPropertyName("entries")]
    public Dictionary<string, BackupEntry> Entries { get; set; } = new(StringComparer.Ordinal);

    public static bool IsSupported(RegistryValueKind kind) => kind is RegistryValueKind.DWord or RegistryValueKind.QWord or
        RegistryValueKind.String or RegistryValueKind.ExpandString or RegistryValueKind.MultiString or RegistryValueKind.Binary;
}

public static class TaskXml
{
    private static readonly System.Xml.Linq.XNamespace Ns = "http://schemas.microsoft.com/windows/2004/02/mit/task";

    /// <summary>Whether a task is enabled, from "schtasks /Query /XML": Task/Settings/Enabled,
    /// true when missing (triggers have Enabled elements of their own).</summary>
    public static bool IsEnabled(string xml)
    {
        int start = xml.IndexOf('<');
        var doc = System.Xml.Linq.XDocument.Parse(start > 0 ? xml[start..] : xml);
        string? enabled = doc.Root?.Element(Ns + "Settings")?.Element(Ns + "Enabled")?.Value;
        return enabled is null || !enabled.Trim().Equals("false", StringComparison.OrdinalIgnoreCase);
    }
}

/// <summary>The backup on disk (%LOCALAPPDATA%\MacWin\privacy\backup.json). It is replaced in
/// one step (write a copy, then rename), so a crash leaves the old or the new one, never half.</summary>
public sealed class PrivacyBackupFile(string path)
{
    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    public string Path { get; } = path;

    /// <summary>An empty backup when there is no file. Throws when the file can't be read or
    /// is damaged: then nothing may be changed, or its originals would be lost.</summary>
    public PrivacyBackup Load()
    {
        if (!File.Exists(Path)) return new PrivacyBackup();
        var backup = JsonSerializer.Deserialize<PrivacyBackup>(File.ReadAllText(Path), Options)
                     ?? throw new InvalidDataException("Empty privacy backup");
        backup.Entries = new Dictionary<string, BackupEntry>(backup.Entries ?? new(), StringComparer.Ordinal);
        return backup;
    }

    /// <summary>Whether MacWin has anything to put back.</summary>
    public bool HasEntries()
    {
        try
        {
            return Load().Entries.Count > 0;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            return true;   // a damaged backup: restoring it reports the problem
        }
    }

    public void Save(PrivacyBackup backup)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(Path)!);
        string temp = Path + ".tmp";
        using (var stream = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            JsonSerializer.Serialize(stream, backup, Options);
            stream.Flush(flushToDisk: true);
        }
        File.Move(temp, Path, overwrite: true);
    }
}

/// <summary>Reads the switches' states and changes them. Every change is backed up before it is
/// made; a switch whose change fails half way is put back as it was.</summary>
public sealed class PrivacyEngine(IPrivacySystem system, PrivacyBackupFile backupFile)
{
    private enum ActionState { Applied, NotApplied, NotApplicable }

    public TweakState GetState(PrivacyTweak tweak)
    {
        bool any = false, allApplied = true;
        foreach (var action in tweak.Actions)
        {
            var state = GetState(action);
            if (state == ActionState.NotApplicable) continue;
            any = true;
            allApplied &= state == ActionState.Applied;
        }
        return !any ? TweakState.Unavailable : allApplied ? TweakState.Off : TweakState.On;
    }

    private ActionState GetState(PrivacyAction action)
    {
        switch (action)
        {
            case RegistryAction reg:
                if (reg.OnlyIfKeyExists && !system.KeyExists(reg.Hive, reg.Key)) return ActionState.NotApplicable;
                return system.ReadValue(reg.Hive, reg.Key, reg.Name) == reg.Off ? ActionState.Applied : ActionState.NotApplied;
            case TaskAction task:
                return system.IsTaskEnabled(task.Path) switch
                {
                    null => ActionState.NotApplicable,
                    false => ActionState.Applied,
                    true => ActionState.NotApplied,
                };
            default:
                return ActionState.NotApplicable;
        }
    }

    public IReadOnlyList<PrivacyResult> Apply(IReadOnlyList<PrivacyChange> changes)
    {
        // 1. The originals of everything that will change, saved before anything changes.
        PrivacyBackup backup;
        try
        {
            backup = backupFile.Load();
            bool added = false;
            foreach (var change in changes)
            {
                foreach (var action in ActionsToChange(change))
                {
                    if (backup.Entries.ContainsKey(action.BackupKey)) continue;
                    backup.Entries[action.BackupKey] = Capture(action);
                    added = true;
                }
            }
            if (added) backupFile.Save(backup);
        }
        catch (Exception ex) when (IsExpected(ex) || ex is JsonException)
        {
            return changes.Select(c => new PrivacyResult(c.Tweak.Id, false, PrivacyErrors.Backup, ex.Message)).ToList();
        }

        // 2. The changes, one switch at a time.
        var results = new List<PrivacyResult>();
        bool backupChanged = false;
        foreach (var change in changes)
        {
            var done = new List<(PrivacyAction Action, BackupEntry Before)>();
            try
            {
                foreach (var action in ActionsToChange(change))
                {
                    var before = Capture(action);
                    done.Add((action, before));
                    if (change.TurnOff) WriteOff(action);
                    else if (backup.Entries.TryGetValue(action.BackupKey, out var original) && !IsOff(action, original))
                    {
                        // Back on: exactly as it was before MacWin, then the backup has done its job.
                        Restore(action, original);
                        backup.Entries.Remove(action.BackupKey);
                        backupChanged = true;
                    }
                    else WriteOn(action);
                }
                if (change.Tweak.Service is string service && done.Count > 0) system.SetServiceRunning(service, !change.TurnOff);
                var expected = change.TurnOff ? TweakState.Off : TweakState.On;
                var now = GetState(change.Tweak);
                results.Add(now == expected || now == TweakState.Unavailable
                    ? new PrivacyResult(change.Tweak.Id, true)
                    : new PrivacyResult(change.Tweak.Id, false, PrivacyErrors.NotKept));
            }
            catch (Exception ex) when (IsExpected(ex))
            {
                // Half a switch is worse than none: what this switch already changed goes back.
                for (int i = done.Count - 1; i >= 0; i--)
                {
                    try
                    {
                        Restore(done[i].Action, done[i].Before);
                    }
                    catch (Exception undo) when (IsExpected(undo))
                    {
                        // The backup still has the original; "Restore" can try again.
                    }
                }
                results.Add(new PrivacyResult(change.Tweak.Id, false, ErrorCode(ex), ex.Message));
            }
        }

        if (backupChanged)
        {
            try
            {
                backupFile.Save(backup);
            }
            catch (Exception ex) when (IsExpected(ex))
            {
                // The entries that stay behind hold the originals that are back now: restoring
                // them later writes the same values again.
            }
        }
        return results;
    }

    /// <summary>Puts back every original in the backup that belongs to a switch of
    /// <paramref name="catalog"/> (nothing else: the file is the user's, a restore with
    /// administrator rights must not write whatever it says).</summary>
    public IReadOnlyList<PrivacyResult> RestoreAll(IReadOnlyList<PrivacyTweak> catalog)
    {
        PrivacyBackup backup;
        try
        {
            backup = backupFile.Load();
        }
        catch (Exception ex) when (IsExpected(ex) || ex is JsonException)
        {
            return [new PrivacyResult("", false, PrivacyErrors.Backup, ex.Message)];
        }

        var results = new List<PrivacyResult>();
        foreach (var tweak in catalog)
        {
            var entries = tweak.Actions.Where(a => backup.Entries.ContainsKey(a.BackupKey)).ToList();
            if (entries.Count == 0) continue;
            string? error = null, detail = null;
            foreach (var action in entries)
            {
                try
                {
                    Restore(action, backup.Entries[action.BackupKey]);
                    backup.Entries.Remove(action.BackupKey);
                }
                catch (Exception ex) when (IsExpected(ex))
                {
                    error ??= ex is InvalidDataException ? PrivacyErrors.Backup : ErrorCode(ex);
                    detail ??= ex.Message;
                }
            }
            if (tweak.Service is string service && GetState(tweak) == TweakState.On) system.SetServiceRunning(service, true);
            results.Add(new PrivacyResult(tweak.Id, error is null, error, detail));
        }
        if (results.Count == 0) return results;

        try
        {
            backupFile.Save(backup);
        }
        catch (Exception ex) when (IsExpected(ex))
        {
            results.Add(new PrivacyResult("", false, PrivacyErrors.Backup, ex.Message));
        }
        return results;
    }

    // What has to change for the switch to reach its new state: the actions that exist on
    // this PC and aren't there yet.
    private IEnumerable<PrivacyAction> ActionsToChange(PrivacyChange change) =>
        change.Tweak.Actions.Where(a => GetState(a) == (change.TurnOff ? ActionState.NotApplied : ActionState.Applied));

    private BackupEntry Capture(PrivacyAction action) => action switch
    {
        RegistryAction reg => BackupEntry.FromValue(system.ReadValue(reg.Hive, reg.Key, reg.Name)),
        TaskAction task => BackupEntry.FromTask(system.IsTaskEnabled(task.Path) ?? true),
        _ => throw new NotSupportedException(action.GetType().Name),
    };

    private static bool IsOff(PrivacyAction action, BackupEntry original) => action switch
    {
        RegistryAction reg => !original.Absent && original.Kind == reg.Off.Kind.ToString() && original.Data == reg.Off.Text,
        TaskAction => original.Enabled == false,
        _ => false,
    };

    private void WriteOff(PrivacyAction action)
    {
        if (action is RegistryAction reg) system.WriteValue(reg.Hive, reg.Key, reg.Name, reg.Off);
        else if (action is TaskAction task) system.SetTaskEnabled(task.Path, false);
    }

    private void WriteOn(PrivacyAction action)
    {
        if (action is RegistryAction reg)
        {
            if (reg.On is null) system.DeleteValue(reg.Hive, reg.Key, reg.Name);
            else system.WriteValue(reg.Hive, reg.Key, reg.Name, reg.On);
        }
        else if (action is TaskAction task) system.SetTaskEnabled(task.Path, true);
    }

    private void Restore(PrivacyAction action, BackupEntry entry)
    {
        if (action is RegistryAction reg)
        {
            // A service whose key is gone (uninstalled) is not created again.
            if (reg.OnlyIfKeyExists && !system.KeyExists(reg.Hive, reg.Key)) return;
            if (entry.ToValue() is RegData value) system.WriteValue(reg.Hive, reg.Key, reg.Name, value);
            else system.DeleteValue(reg.Hive, reg.Key, reg.Name);
        }
        else if (action is TaskAction task && system.IsTaskEnabled(task.Path) is not null)
        {
            system.SetTaskEnabled(task.Path, entry.Enabled ?? true);
        }
    }

    // What the registry, the files and schtasks throw; anything else is a bug and is reported as one.
    private static bool IsExpected(Exception ex) => ex is IOException or UnauthorizedAccessException or
        System.Security.SecurityException or InvalidOperationException or ArgumentException or FormatException or
        InvalidDataException or TimeoutException or System.ComponentModel.Win32Exception;

    private static string ErrorCode(Exception ex) =>
        ex is UnauthorizedAccessException or System.Security.SecurityException ? PrivacyErrors.Access : PrivacyErrors.Other;
}
