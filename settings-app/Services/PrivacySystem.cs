// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

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

    // Every app package registered for the user has a key here, named after its full name
    // ("Microsoft.Copilot_1.25.0.0_x64__8wekyb3d8bbwe"): its name, then the publisher id last.
    private const string Packages =
        @"Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages";

    public bool? IsPackageInstalled(string family)
    {
        using var k = _user.OpenSubKey(Packages);
        if (k is null) return null;
        int cut = family.LastIndexOf('_');
        if (cut <= 0) return null;
        string name = family[..cut] + "_", publisher = "__" + family[(cut + 1)..];
        return k.GetSubKeyNames().Any(n => n.StartsWith(name, StringComparison.OrdinalIgnoreCase) &&
                                           n.EndsWith(publisher, StringComparison.OrdinalIgnoreCase));
    }

    public void RemovePackage(string family)
    {
        // What Settings → Apps → Uninstall does, for this user only. The name is checked
        // first: it goes into a command line.
        int cut = family.LastIndexOf('_');
        string name = cut > 0 ? family[..cut] : "";
        if (name.Length == 0 || !name.All(c => char.IsAsciiLetterOrDigit(c) || c == '.'))
            throw new InvalidDataException("Bad package name " + family);
        var (exit, output) = Tools.Run(@"WindowsPowerShell\v1.0\powershell.exe",
                                       $"-NoProfile -NonInteractive -Command \"Get-AppxPackage -Name '{name}' | Remove-AppxPackage\"",
                                       TimeSpan.FromMinutes(2));
        if (exit != 0) throw new InvalidOperationException($"Remove-AppxPackage failed ({exit}): {output.Trim()}");
    }

    public void OpenStore(string productId)
    {
        if (!productId.All(char.IsAsciiLetterOrDigit)) throw new InvalidDataException("Bad Store id " + productId);
        try
        {
            Process.Start(new ProcessStartInfo("ms-windows-store://pdp/?ProductId=" + productId) { UseShellExecute = true });
        }
        catch (Win32Exception ex)
        {
            throw new InvalidOperationException("The Microsoft Store didn't open: " + ex.Message, ex);
        }
    }

    public bool Has(PrivacyNeed need) => need switch
    {
        // Recall and Click to Do come with the AI components of a Copilot+ PC.
        PrivacyNeed.CopilotPlus => HasSystemApp("MicrosoftWindows.Client.AIX_"),
        // Windows' tips on the lock screen only show over the user's own picture or slideshow.
        PrivacyNeed.OwnLockScreenPicture =>
            ReadValue(PrivacyHive.User, @"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager",
                      "RotatingLockScreenEnabled") != RegData.Dword(1),
        _ => true,
    };

    private static bool HasSystemApp(string prefix)
    {
        try
        {
            string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "SystemApps");
            return Directory.Exists(folder) && Directory.EnumerateDirectories(folder, prefix + "*").Any();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    /// <summary>Restarts File Explorer (the taskbar and Start too) so it shows what changed:
    /// Windows starts it again by itself, as the signed-in user.</summary>
    public static void RestartExplorer() => Tools.Run("taskkill.exe", "/f /im explorer.exe", TimeSpan.FromSeconds(20));
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
        try
        {
            copy.Wait(timeout);
            return (p.ExitCode, Decode(output.ToArray()) + error.Result);
        }
        catch (AggregateException ex) when (ex.InnerException is not null)
        {
            // The output pipe broke (the program ended oddly): one error the callers know.
            throw new InvalidOperationException(exe + ": " + ex.InnerException.Message, ex.InnerException);
        }
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

    private static string BackupOf(SwitchSet set) => Path.Combine(Folder, set.BackupName);

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
    public static Dictionary<string, TweakState> ReadStates(SwitchSet set)
    {
        using var system = new WindowsPrivacySystem();
        var engine = new PrivacyEngine(system, new PrivacyBackupFile(BackupOf(set)));
        var states = new Dictionary<string, TweakState>();
        foreach (var tweak in set.All)
        {
            try
            {
                states[tweak.Id] = engine.GetState(tweak);
            }
            catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException)
            {
                App.Log($"Privacy: not allowed to read {tweak.Id}: {ex.Message}");
                states[tweak.Id] = TweakState.Denied;
            }
            catch (Exception ex) when (ex is IOException or InvalidOperationException or InvalidDataException or
                                       Win32Exception or TimeoutException)
            {
                App.Log($"Privacy: can't read {tweak.Id}: {ex.Message}");
                states[tweak.Id] = TweakState.Unavailable;
            }
        }
        return states;
    }

    public static bool HasBackup(SwitchSet set) => new PrivacyBackupFile(BackupOf(set)).HasEntries();

    // Only the Privacy page's switches ever need administrator rights (tests/settings checks
    // that the Tweaks page's don't); the helper takes nothing but those.
    // The switches that need them go to the helper; the user's own (apps above all, which must
    // be removed for this user and not for the administrator who answered the prompt) are
    // changed here.
    public static Task<IReadOnlyList<PrivacyResult>> ApplyAsync(SwitchSet set, IReadOnlyList<PrivacyChange> changes) =>
        Task.Run(() =>
        {
            var elevated = set == SwitchSet.Privacy && !IsElevated ? changes.Where(c => c.Tweak.NeedsAdmin).ToList() : [];
            var here = changes.Except(elevated).ToList();
            var results = new List<PrivacyResult>();
            if (here.Count > 0) results.AddRange(Here(set, engine => engine.Apply(here)));
            if (elevated.Count > 0)
                results.AddRange(RunElevated("apply", PrivacyCatalog.FormatChanges(elevated), elevated.Select(c => c.Tweak.Id)));
            return AfterChange(results);
        });

    public static Task<IReadOnlyList<PrivacyResult>> RestoreAsync(SwitchSet set) =>
        Task.Run(() =>
        {
            bool elevated = set == SwitchSet.Privacy && NeedsAdminToRestore() && !IsElevated;
            var results = new List<PrivacyResult>(Here(set, engine => engine.RestoreAll(
                elevated ? set.All.Where(t => !t.NeedsAdmin).ToList() : set.All)));
            if (elevated) results.AddRange(RunElevated("restore", null, [""]));
            return AfterChange(results);
        });

    // Explorer, Start and the taskbar read their settings and policies again when told so.
    private static IReadOnlyList<PrivacyResult> AfterChange(IReadOnlyList<PrivacyResult> results)
    {
        if (results.Any(r => r.Ok)) Native.NotifyShellSettingsChanged();
        return results;
    }

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

    private static IReadOnlyList<PrivacyResult> Here(SwitchSet set, Func<PrivacyEngine, IReadOnlyList<PrivacyResult>> work)
    {
        using var system = new WindowsPrivacySystem();
        return Log(work(new PrivacyEngine(system, new PrivacyBackupFile(BackupOf(set)))));
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
            // Only switches that need administrator rights: the user's own are changed by the
            // window that asked, as that user.
            results = changes is not null ? engine.Apply(changes.Where(c => c.Tweak.NeedsAdmin).ToList())
                                          : engine.RestoreAll(PrivacyCatalog.All.Where(t => t.NeedsAdmin).ToList());
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
