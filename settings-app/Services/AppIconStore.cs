// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MacWin.Settings;

/// <summary>One icon in the library: library\&lt;Id&gt;.ico and its preview .png.</summary>
public sealed record LibraryIcon(string Id, string Name, string IcoPath, string PreviewPath);

/// <summary>A program on this PC that can get an icon (from a Start menu or desktop
/// shortcut, or running with a window).</summary>
public sealed record KnownProgram(string Exe, string Name);

/// <summary>
/// App icons on disk, in %LOCALAPPDATA%\MacWin\icons:
///   library\   the icons the user imported (.ico + .png preview);
///   apps\      &lt;program&gt;.ico for each program that has one - what MacWin's DLL shows on
///              that program's windows (see src/hook/effects/icons.cpp);
///   shortcuts.json   the original icon of every shortcut MacWin changed, to give it back.
/// Shortcuts (Start menu, desktop, taskbar pins) get the same icon, since the taskbar shows
/// a pinned program's shortcut icon rather than its window's.
/// </summary>
public static class AppIconStore
{
    public static string Root { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "icons");
    public static string LibraryDir => Path.Combine(Root, "library");
    public static string AppsDir => Path.Combine(Root, "apps");
    private static string ShortcutsFile => Path.Combine(Root, "shortcuts.json");

    // Icons that come with MacWin (installed next to the settings app), and the ones of them
    // already offered once: each is imported a single time, so one the user deleted stays gone.
    public static string BundledDir => Path.Combine(AppContext.BaseDirectory, "app-icons");
    private static string SeenFile => Path.Combine(Root, "bundled.json");

    /// <summary>The bundled icons not imported yet.</summary>
    public static List<string> NewBundledFiles()
    {
        if (!Directory.Exists(BundledDir)) return [];
        var seen = Seen();
        return Directory.EnumerateFiles(BundledDir)
            .Where(f => IconImport.IsSupported(f) && !seen.Contains(Path.GetFileName(f)))
            .ToList();
    }

    public static void MarkSeen(IEnumerable<string> files)
    {
        var seen = Seen();
        foreach (string f in files) seen.Add(Path.GetFileName(f));
        Directory.CreateDirectory(Root);
        File.WriteAllText(SeenFile + ".tmp", JsonSerializer.Serialize(seen.Order(StringComparer.OrdinalIgnoreCase)));
        File.Move(SeenFile + ".tmp", SeenFile, overwrite: true);
    }

    private static HashSet<string> Seen()
    {
        try
        {
            return new HashSet<string>(JsonSerializer.Deserialize<List<string>>(File.ReadAllText(SeenFile)) ?? [],
                                       StringComparer.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        }
    }

    public static string AppIconPath(string exe) => Path.Combine(AppsDir, Path.GetFileNameWithoutExtension(exe).ToLowerInvariant() + ".ico");

    // ---- library -----------------------------------------------------------------------

    public static List<LibraryIcon> Library()
    {
        if (!Directory.Exists(LibraryDir)) return [];
        return Directory.GetFiles(LibraryDir, "*.ico")
            .Select(ico => Path.GetFileNameWithoutExtension(ico))
            .Select(id => new LibraryIcon(id, id, Path.Combine(LibraryDir, id + ".ico"), Path.Combine(LibraryDir, id + ".png")))
            .OrderBy(i => i.Name, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
    }

    /// <summary>Converts the file into the library; returns its entry. The name comes from
    /// the file's unless given. An icon of the same name is replaced (a newer version of it).</summary>
    public static LibraryIcon Import(string source, string? name = null)
    {
        Directory.CreateDirectory(LibraryDir);
        name = SafeFileName(name ?? IconNames.Clean(Path.GetFileNameWithoutExtension(source)));
        if (name.Length == 0) name = "Icon";
        var icon = new LibraryIcon(name, name, Path.Combine(LibraryDir, name + ".ico"), Path.Combine(LibraryDir, name + ".png"));
        IconImport.Convert(source, icon.IcoPath, icon.PreviewPath);
        return icon;
    }

    public static void Delete(LibraryIcon icon)
    {
        File.Delete(icon.IcoPath);
        File.Delete(icon.PreviewPath);
    }

    /// <summary>Empties the library and what the DLL shows. The icons that come with MacWin
    /// count as offered, so they don't come back at the next start.</summary>
    public static void DeleteAll()
    {
        foreach (string dir in new[] { LibraryDir, AppsDir })
        {
            if (!Directory.Exists(dir)) continue;
            foreach (string file in Directory.EnumerateFiles(dir).ToList())
            {
                try
                {
                    File.Delete(file);
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    // In use for a moment (a preview being read): left for the next clean-up.
                }
            }
        }
        if (Directory.Exists(BundledDir)) MarkSeen(Directory.EnumerateFiles(BundledDir));
    }

    // ---- sets --------------------------------------------------------------------------

    /// <summary>A set: every library icon and the program each one is given to, in one
    /// zip file (icons\&lt;id&gt;.ico + set.json), to keep, switch between or move to another PC.</summary>
    public const string SetExtension = ".macwinicons";

    internal sealed record SetFile(int Version, Dictionary<string, string> Apps);

    public static void SaveSet(string path, IReadOnlyDictionary<string, string> apps)
    {
        string tmp = $"{path}.{Guid.NewGuid():N}.tmp";
        using (var zip = ZipFile.Open(tmp, ZipArchiveMode.Create))
        {
            // The icons are PNG inside already: stored, not compressed again.
            foreach (var icon in Library())
                zip.CreateEntryFromFile(icon.IcoPath, "icons/" + icon.Id + ".ico", CompressionLevel.NoCompression);
            using var json = zip.CreateEntry("set.json").Open();
            JsonSerializer.Serialize(json, new SetFile(1, new Dictionary<string, string>(apps)));
        }
        File.Move(tmp, path, overwrite: true);
    }

    /// <summary>Unpacks a set's icons into `folder` as &lt;id&gt;.ico (they are converted again
    /// on import, like any file: nothing from the set reaches the DLL unchecked) and returns
    /// its choices, program -> icon id.</summary>
    public static Dictionary<string, string> ExtractSet(string path, string folder)
    {
        using var zip = ZipFile.OpenRead(path);
        var entry = zip.GetEntry("set.json") ?? throw new InvalidDataException("Not a MacWin icon set.");
        SetFile set;
        using (var json = entry.Open())
            set = JsonSerializer.Deserialize<SetFile>(json) ?? throw new InvalidDataException("Not a MacWin icon set.");
        Directory.CreateDirectory(folder);
        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in zip.Entries)
        {
            // Only the name is used: an entry can't reach outside the folder.
            string id = Path.GetFileNameWithoutExtension(e.Name);
            if (!e.FullName.StartsWith("icons/", StringComparison.Ordinal) || !e.Name.EndsWith(".ico", StringComparison.OrdinalIgnoreCase) ||
                id.Length == 0 || SafeFileName(id) != id || e.Length > IconImport.MaxBytes) continue;
            e.ExtractToFile(Path.Combine(folder, id + ".ico"), overwrite: true);
            ids.Add(id);
        }
        return (set.Apps ?? [])
            .Where(kv => ids.Contains(kv.Value) && kv.Key.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) &&
                         SafeFileName(kv.Key) == kv.Key)
            .GroupBy(kv => kv.Key.ToLowerInvariant())
            .ToDictionary(g => g.Key, g => g.First().Value);
    }

    private static string SafeFileName(string name)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) name = name.Replace(c, ' ');
        return name.Trim().TrimEnd('.');
    }

    // ---- what the DLL shows ------------------------------------------------------------

    /// <summary>The program shows `icon` from now on (its own windows, its shortcuts).</summary>
    public static void Assign(string exe, LibraryIcon icon, bool shortcuts)
    {
        Directory.CreateDirectory(AppsDir);
        string target = AppIconPath(exe);
        File.Copy(icon.IcoPath, target + ".tmp", overwrite: true);
        File.Move(target + ".tmp", target, overwrite: true);
        if (shortcuts) SetShortcuts(exe, target);
    }

    public static void Unassign(string exe)
    {
        RestoreShortcuts(exe);
        File.Delete(AppIconPath(exe));
    }

    // ---- shortcuts ---------------------------------------------------------------------

    private static IEnumerable<string> ShortcutFolders()
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        yield return Path.Combine(appData, @"Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar");
        yield return Environment.GetFolderPath(Environment.SpecialFolder.StartMenu);
        yield return Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
    }

    private static IEnumerable<string> Shortcuts(IEnumerable<string> folders)
    {
        // A subfolder that can't be opened is skipped on its own, not the whole folder with it.
        var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true };
        foreach (string folder in folders)
        {
            if (!Directory.Exists(folder)) continue;
            IEnumerable<string> files;
            try
            {
                files = Directory.EnumerateFiles(folder, "*.lnk", options).ToList();
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                continue;
            }
            foreach (string f in files) yield return f;
        }
    }

    /// <summary>The program a shortcut starts: its target, or for Squirrel installs
    /// (Discord, Slack, ...) the "--processStart X.exe" its Update.exe is given.</summary>
    internal static string? ProgramOf(string target, string arguments)
    {
        string exe = Path.GetFileName(target).ToLowerInvariant();
        if (exe == "update.exe")
        {
            const string flag = "--processstart";
            int i = arguments.ToLowerInvariant().IndexOf(flag, StringComparison.Ordinal);
            if (i < 0) return null;
            string rest = arguments[(i + flag.Length)..].Trim().Trim('"');
            int end = rest.IndexOfAny([' ', '"']);
            return (end < 0 ? rest : rest[..end]).ToLowerInvariant();
        }
        return exe.EndsWith(".exe", StringComparison.Ordinal) ? exe : null;
    }

    private static Dictionary<string, string> LoadBackups()
    {
        try
        {
            return JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(ShortcutsFile))
                   ?? new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        }
    }

    private static void SaveBackups(Dictionary<string, string> backups)
    {
        Directory.CreateDirectory(Root);
        File.WriteAllText(ShortcutsFile + ".tmp", JsonSerializer.Serialize(backups, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(ShortcutsFile + ".tmp", ShortcutsFile, overwrite: true);
    }

    /// <summary>Every shortcut of the program shows `icoPath`; each one's own icon is kept
    /// (once: a second change doesn't overwrite the original). Returns how many changed.</summary>
    public static int SetShortcuts(string exe, string icoPath)
    {
        var backups = LoadBackups();
        int changed = 0;
        foreach (string lnk in Shortcuts(ShortcutFolders()))
        {
            try
            {
                using var link = ShellLink.Open(lnk, write: true);
                if (ProgramOf(link.Target, link.Arguments) != exe.ToLowerInvariant()) continue;
                string wanted = icoPath + ",0";
                if (string.Equals(link.IconLocation, wanted, StringComparison.OrdinalIgnoreCase)) continue;
                backups.TryAdd(lnk, Original(link));
                link.IconLocation = wanted;
                link.Save();
                Native.SHChangeNotify(Native.SHCNE_UPDATEITEM, Native.SHCNF_PATHW, lnk, IntPtr.Zero);
                changed++;
            }
            catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException)
            {
                // A shortcut that can't be read or written is left as it is.
            }
        }
        SaveBackups(backups);
        if (changed > 0) Native.SHChangeNotify(Native.SHCNE_ASSOCCHANGED, Native.SHCNF_IDLIST, IntPtr.Zero, IntPtr.Zero);
        return changed;
    }

    /// <summary>The program's shortcuts get their own icons back (null: every program's).</summary>
    public static void RestoreShortcuts(string? exe)
    {
        var backups = LoadBackups();
        string mine = exe is null ? Root : AppIconPath(exe);
        foreach (var (lnk, original) in backups.ToList())
        {
            try
            {
                if (!File.Exists(lnk))
                {
                    backups.Remove(lnk);
                    continue;
                }
                using var link = ShellLink.Open(lnk, write: true);
                if (!link.IconLocation.StartsWith(mine, StringComparison.OrdinalIgnoreCase)) continue;
                link.IconLocation = original;
                link.Save();
                backups.Remove(lnk);
                Native.SHChangeNotify(Native.SHCNE_UPDATEITEM, Native.SHCNF_PATHW, lnk, IntPtr.Zero);
            }
            catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException)
            {
            }
        }
        SaveBackups(backups);
        // Shortcuts that point into MacWin's folder without a backup (set by hand, or by an
        // earlier test) get the program's own icon.
        foreach (string lnk in Shortcuts(ShortcutFolders()))
        {
            try
            {
                using var link = ShellLink.Open(lnk, write: true);
                if (!link.IconLocation.StartsWith(Root, StringComparison.OrdinalIgnoreCase) ||
                    (exe is not null && ProgramOf(link.Target, link.Arguments) != exe.ToLowerInvariant())) continue;
                link.IconLocation = DefaultIcon(link);
                link.Save();
                Native.SHChangeNotify(Native.SHCNE_UPDATEITEM, Native.SHCNF_PATHW, lnk, IntPtr.Zero);
            }
            catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException)
            {
            }
        }
        Native.SHChangeNotify(Native.SHCNE_ASSOCCHANGED, Native.SHCNF_IDLIST, IntPtr.Zero, IntPtr.Zero);
    }

    /// <summary>What a shortcut showed before any MacWin icon: its own setting, or - when it
    /// already points into MacWin's folder - the program's icon.</summary>
    private static string Original(ShellLink link) =>
        link.IconLocation.StartsWith(Root, StringComparison.OrdinalIgnoreCase) ? DefaultIcon(link) : link.IconLocation;

    private static string DefaultIcon(ShellLink link)
    {
        // Squirrel installs (Discord, Slack, ...) keep their icon next to Update.exe.
        if (Path.GetFileName(link.Target).Equals("update.exe", StringComparison.OrdinalIgnoreCase))
        {
            string ico = Path.Combine(Path.GetDirectoryName(link.Target) ?? "", "app.ico");
            if (File.Exists(ico)) return ico + ",0";
        }
        return "";   // the target's own icon
    }

    /// <summary>Restarts Explorer: the taskbar redraws pinned programs with their new icons
    /// (it keeps the old ones in memory until then).</summary>
    public static void RefreshTaskbar()
    {
        foreach (var p in Process.GetProcessesByName("explorer"))
        {
            try { p.Kill(); } catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception) { }
        }
        // Windows starts the shell again by itself; a missing one is started here.
        Task.Delay(2500).ContinueWith(_ =>
        {
            if (Process.GetProcessesByName("explorer").Length == 0) Process.Start("explorer.exe");
        });
    }

    // ---- programs on this PC -----------------------------------------------------------

    /// <summary>Programs with a Start menu or desktop shortcut, and those running with a
    /// window, by file name.</summary>
    public static List<KnownProgram> Programs()
    {
        var found = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var folders = ShortcutFolders().Concat(
        [
            Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu),
            Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory),
        ]);
        foreach (string lnk in Shortcuts(folders))
        {
            try
            {
                using var link = ShellLink.Open(lnk);
                if (ProgramOf(link.Target, link.Arguments) is string exe && !found.ContainsKey(exe) && !IsSetupTool(exe))
                    found[exe] = Path.GetFileNameWithoutExtension(lnk);
            }
            catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException)
            {
            }
        }
        foreach (var p in Process.GetProcesses())
        {
            try
            {
                if (p.MainWindowHandle == IntPtr.Zero) continue;
                string exe = p.ProcessName.ToLowerInvariant() + ".exe";
                if (found.ContainsKey(exe)) continue;
                string? description = null;
                try
                {
                    description = p.MainModule?.FileVersionInfo.FileDescription;
                }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
                {
                    // An elevated program: its file can't be read from here, its name will do.
                }
                found[exe] = string.IsNullOrWhiteSpace(description) ? p.ProcessName : description.Trim();
            }
            catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
            {
            }
            finally
            {
                p.Dispose();
            }
        }
        return found.Select(kv => new KnownProgram(kv.Key.ToLowerInvariant(), kv.Value))
            .OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    // Uninstallers and the like have shortcuts too; nobody gives them an icon.
    private static bool IsSetupTool(string exe) =>
        exe.StartsWith("unins", StringComparison.Ordinal) || exe.Contains("uninstall", StringComparison.Ordinal) ||
        exe is "msiexec.exe" or "rundll32.exe";
}
