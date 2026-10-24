// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace MacWin.Settings;

/// <summary>The real PC for <see cref="PrivacyEngine"/>: the registry, schtasks and sc.
/// <paramref name="userSid"/>: the user whose HKCU is changed when this runs with another
/// account's administrator rights (HKEY_USERS\&lt;sid&gt;); null for this process' own HKCU.</summary>
internal sealed class WindowsPrivacySystem : IPrivacySystem, IDisposable
{
    private readonly RegistryKey _machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
    private readonly RegistryKey _user;

    public WindowsPrivacySystem(string? userSid = null)
    {
        _user = userSid is null
            ? RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64)
            : Registry.Users.OpenSubKey(userSid, writable: true)
              ?? throw new InvalidOperationException("The user's registry is not loaded");
    }

    public void Dispose()
    {
        _machine.Dispose();
        _user.Dispose();
    }

    private RegistryKey Root(PrivacyHive hive) => hive == PrivacyHive.Machine ? _machine : _user;

    public bool KeyExists(PrivacyHive hive, string key)
    {
        using var k = Root(hive).OpenSubKey(key);
        return k is not null;
    }

    public RegData? ReadValue(PrivacyHive hive, string key, string name)
    {
        using var k = Root(hive).OpenSubKey(key);
        object? value = k?.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
        if (k is null || value is null) return null;
        var kind = k.GetValueKind(name);
        return new RegData(kind, value switch
        {
            int i => i.ToString(CultureInfo.InvariantCulture),
            long l => l.ToString(CultureInfo.InvariantCulture),
            string s => s,
            string[] lines => string.Join('\0', lines),
            byte[] bytes => Convert.ToBase64String(bytes),
            _ => "",
        });
    }

    public void WriteValue(PrivacyHive hive, string key, string name, RegData data)
    {
        object value = data.Kind switch
        {
            RegistryValueKind.DWord => int.Parse(data.Text, CultureInfo.InvariantCulture),
            RegistryValueKind.QWord => long.Parse(data.Text, CultureInfo.InvariantCulture),
            RegistryValueKind.String or RegistryValueKind.ExpandString => data.Text,
            RegistryValueKind.MultiString => data.Text.Length == 0 ? Array.Empty<string>() : data.Text.Split('\0'),
            RegistryValueKind.Binary => Convert.FromBase64String(data.Text),
            _ => throw new InvalidDataException($"Unsupported registry value kind {data.Kind}"),
        };
        using var k = Root(hive).CreateSubKey(key, writable: true);
        k.SetValue(name, value, data.Kind);
    }

    public void DeleteValue(PrivacyHive hive, string key, string name)
    {
        // An empty key that MacWin created is left: it changes nothing.
        using var k = Root(hive).OpenSubKey(key, writable: true);
        k?.DeleteValue(name, throwOnMissingValue: false);
    }

    public bool? IsTaskEnabled(string path)
    {
        // schtasks only says why it failed in a translated message: a task it can't show is
        // treated as missing (the Task Scheduler's own index is closed to standard users).
        var (exit, output) = Tools.Run("schtasks.exe", $"/Query /TN \"{path}\" /XML", TimeSpan.FromSeconds(15));
        if (exit != 0) return null;
        try
        {
            return TaskXml.IsEnabled(output);
        }
        catch (System.Xml.XmlException ex)
        {
            throw new InvalidDataException("Unreadable task XML: " + ex.Message, ex);
        }
    }

    public void SetTaskEnabled(string path, bool enabled)
    {
        var (exit, output) = Tools.Run("schtasks.exe", $"/Change /TN \"{path}\" {(enabled ? "/ENABLE" : "/DISABLE")}",
                                       TimeSpan.FromSeconds(30));
        if (exit != 0) throw new InvalidOperationException($"schtasks /Change failed ({exit}): {output.Trim()}");
    }

    public void SetServiceRunning(string name, bool running)
    {
        try
        {
            Tools.Run("sc.exe", $"{(running ? "start" : "stop")} {name}", TimeSpan.FromSeconds(20));
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or TimeoutException)
        {
            // Best effort: the start type decides after the next restart.
        }
    }
}

internal static class Tools
{
    /// <summary>Runs a program from System32 without a window; its exit code and output.</summary>
    public static (int Exit, string Output) Run(string exe, string args, TimeSpan timeout)
    {
        var psi = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, exe), args)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        using var p = Process.Start(psi) ?? throw new InvalidOperationException(exe + " did not start");
        using var output = new MemoryStream();
        var copy = p.StandardOutput.BaseStream.CopyToAsync(output);
        var error = p.StandardError.ReadToEndAsync();
        if (!p.WaitForExit(timeout))
        {
            try
            {
                p.Kill();
            }
            catch (InvalidOperationException)
            {
            }
            throw new TimeoutException(exe + " did not finish");
        }
        copy.Wait(timeout);
        return (p.ExitCode, Decode(output.ToArray()) + error.Result);
    }

    // schtasks writes its XML in UTF-16; its messages (only logged) in the console's code page,
    // which is close enough to UTF-8 for a log line.
    private static string Decode(byte[] bytes)
    {
        bool utf16 = bytes.Length >= 2 && (bytes[0] == 0xFF && bytes[1] == 0xFE || bytes.Count(b => b == 0) > bytes.Length / 3);
        return utf16 ? Encoding.Unicode.GetString(bytes).TrimStart('﻿') : Encoding.UTF8.GetString(bytes);
    }
}

/// <summary>Runs the Privacy page's reads and changes: here, or with administrator rights in a
/// second copy of this app (one UAC prompt per Apply), which only takes switches of the
/// catalogue and writes its results to a file next to the backup.</summary>
internal static class PrivacyRunner
{
    public static string Folder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "privacy");

    public static string BackupPath { get; } = Path.Combine(Folder, "backup.json");

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private const int ErrorCancelled = 1223;

    public static bool IsElevated
    {
        get
        {
            using var me = WindowsIdentity.GetCurrent();
            return new WindowsPrincipal(me).IsInRole(WindowsBuiltInRole.Administrator);
        }
    }

    /// <summary>Every switch's state; a switch that can't be read counts as not on this PC.
    /// Slow-ish (schtasks): call off the UI thread.</summary>
    public static Dictionary<string, TweakState> ReadStates()
    {
        using var system = new WindowsPrivacySystem();
        var engine = new PrivacyEngine(system, new PrivacyBackupFile(BackupPath));
        var states = new Dictionary<string, TweakState>();
        foreach (var tweak in PrivacyCatalog.All)
        {
            try
            {
                states[tweak.Id] = engine.GetState(tweak);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.Security.SecurityException or
                                       InvalidOperationException or InvalidDataException or Win32Exception or TimeoutException)
            {
                App.Log($"Privacy: can't read {tweak.Id}: {ex.Message}");
                states[tweak.Id] = TweakState.Unavailable;
            }
        }
        return states;
    }

    public static bool HasBackup() => new PrivacyBackupFile(BackupPath).HasEntries();

    public static Task<IReadOnlyList<PrivacyResult>> ApplyAsync(IReadOnlyList<PrivacyChange> changes) => Task.Run(() =>
        changes.Any(c => c.Tweak.NeedsAdmin) && !IsElevated
            ? RunElevated("apply", PrivacyCatalog.FormatChanges(changes), changes.Select(c => c.Tweak.Id))
            : Here(engine => engine.Apply(changes)));

    public static Task<IReadOnlyList<PrivacyResult>> RestoreAsync() => Task.Run(() =>
        NeedsAdminToRestore() && !IsElevated
            ? RunElevated("restore", null, [""])
            : Here(engine => engine.RestoreAll(PrivacyCatalog.All)));

    // Machine-wide originals in the backup (or a backup that can't be read: the helper reports it).
    private static bool NeedsAdminToRestore()
    {
        try
        {
            var keys = new PrivacyBackupFile(BackupPath).Load().Entries.Keys;
            return PrivacyCatalog.All.SelectMany(t => t.Actions).Any(a => a.NeedsAdmin && keys.Contains(a.BackupKey));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            return false;
        }
    }

    private static IReadOnlyList<PrivacyResult> Here(Func<PrivacyEngine, IReadOnlyList<PrivacyResult>> work)
    {
        using var system = new WindowsPrivacySystem();
        return Log(work(new PrivacyEngine(system, new PrivacyBackupFile(BackupPath))));
    }

    private static IReadOnlyList<PrivacyResult> RunElevated(string mode, string? changes, IEnumerable<string> ids)
    {
        string resultPath = Path.Combine(Folder, $"result-{Guid.NewGuid():N}.json");
        string? sid;
        using (var me = WindowsIdentity.GetCurrent()) sid = me.User?.Value;
        var args = new StringBuilder($"--privacy {mode} --user {sid} --backup \"{BackupPath}\" --result \"{resultPath}\"");
        if (changes is not null) args.Append($" --changes {changes}");
        try
        {
            Directory.CreateDirectory(Folder);
            using var p = Process.Start(new ProcessStartInfo(Environment.ProcessPath!, args.ToString())
            {
                UseShellExecute = true,
                Verb = "runas",
            });
            if (p is null || !p.WaitForExit(TimeSpan.FromMinutes(3)))
                return Fail(ids, PrivacyErrors.Other, "The administrator helper did not finish");
            if (!File.Exists(resultPath))
                return Fail(ids, PrivacyErrors.Other, $"The administrator helper exited with {p.ExitCode} without results");
            var results = JsonSerializer.Deserialize<List<PrivacyResult>>(File.ReadAllText(resultPath), JsonOptions);
            return Log(results ?? Fail(ids, PrivacyErrors.Other, "Empty results"));
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == ErrorCancelled)
        {
            return Fail(ids, PrivacyErrors.Cancelled, null);
        }
        catch (Exception ex) when (ex is Win32Exception or IOException or UnauthorizedAccessException or JsonException or
                                   InvalidOperationException)
        {
            return Log(Fail(ids, PrivacyErrors.Other, ex.Message));
        }
        finally
        {
            try
            {
                File.Delete(resultPath);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
            }
        }
    }

    private static List<PrivacyResult> Fail(IEnumerable<string> ids, string error, string? detail) =>
        ids.Select(id => new PrivacyResult(id, false, error, detail)).ToList();

    private static IReadOnlyList<PrivacyResult> Log(IReadOnlyList<PrivacyResult> results)
    {
        foreach (var r in results.Where(r => !r.Ok))
            App.Log($"Privacy: {(r.Id.Length > 0 ? r.Id : "backup")} not changed ({r.Error}): {r.Detail}");
        return results;
    }

    /// <summary>"--privacy apply|restore ...": the copy with administrator rights. No window;
    /// exit code 0 when everything worked, 1 when something didn't, 2 for bad arguments.</summary>
    public static int RunHelper(string[] args)
    {
        string? mode = Arg(args, "--privacy"), sid = Arg(args, "--user");
        string? backup = Arg(args, "--backup"), result = Arg(args, "--result");
        // Only the files this app names: backup.json and result-*.json in one "privacy" folder.
        if (mode is not ("apply" or "restore") || !PrivacyCatalog.IsUserSid(sid) || backup is null || result is null ||
            !Path.IsPathFullyQualified(backup) || !Path.IsPathFullyQualified(result) ||
            Path.GetFileName(backup) != "backup.json" || !Path.GetFileName(result).StartsWith("result-", StringComparison.Ordinal) ||
            Path.GetExtension(result) != ".json" || Path.GetDirectoryName(backup) != Path.GetDirectoryName(result) ||
            Path.GetFileName(Path.GetDirectoryName(backup)) != "privacy")
            return 2;
        var changes = mode == "apply" ? PrivacyCatalog.ParseChanges(Arg(args, "--changes")) : null;
        if (mode == "apply" && changes is null) return 2;

        IReadOnlyList<PrivacyResult> results;
        try
        {
            using var system = new WindowsPrivacySystem(sid);
            var engine = new PrivacyEngine(system, new PrivacyBackupFile(backup));
            results = changes is not null ? engine.Apply(changes) : engine.RestoreAll(PrivacyCatalog.All);
        }
        catch (Exception ex)
        {
            // Whatever it is, the window that asked must learn about it.
            results = Fail(changes?.Select(c => c.Tweak.Id) ?? [""], PrivacyErrors.Other, ex.Message);
        }
        try
        {
            File.WriteAllText(result, JsonSerializer.Serialize(results, JsonOptions));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return 1;
        }
        return results.All(r => r.Ok) ? 0 : 1;
    }

    private static string? Arg(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }
}
