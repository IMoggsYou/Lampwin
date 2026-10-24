// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Collections.Concurrent;
using System.IO;

namespace MacWin.Settings;

/// <summary>
/// What the App icons page does, also run at start for the icons that come with MacWin.
/// The work with files and shortcuts runs on threads of their own in single-threaded
/// apartments (the shell's shortcut objects and WPF's imaging want one); the model is only
/// touched on the UI thread, after each await.
/// </summary>
public static class AppIconActions
{
    /// <summary>The library or the choices changed outside the page (bundled icons).</summary>
    public static event Action? Changed;

    private static readonly SemaphoreSlim Gate = new(1, 1);
    private static List<KnownProgram>? _programs;

    /// <summary>The programs on this PC, found once per run (shortcuts and windows).</summary>
    public static async Task<List<KnownProgram>> ProgramsAsync(bool refresh = false)
    {
        if (_programs is null || refresh) _programs = await Sta(AppIconStore.Programs);
        return _programs;
    }

    /// <summary>Imports `files` on a few threads at once; reports (done, total) on the UI
    /// thread. Files that hold no usable image come back in Failed.</summary>
    public static async Task<(List<LibraryIcon> Added, List<string> Failed)> ImportAsync(
        IReadOnlyList<string> files, IProgress<(int Done, int Total)>? progress = null, Func<string, string?>? name = null)
    {
        var queue = new ConcurrentQueue<string>(files);
        var added = new ConcurrentBag<LibraryIcon>();
        var failed = new ConcurrentBag<string>();
        int done = 0;
        int workers = Math.Clamp(Environment.ProcessorCount / 2, 1, 4);
        var tasks = Enumerable.Range(0, Math.Min(workers, files.Count)).Select(_ => Sta(() =>
        {
            while (queue.TryDequeue(out string? file))
            {
                try
                {
                    added.Add(AppIconStore.Import(file, name?.Invoke(file)));
                }
                catch (Exception ex) when (ex is IOException or InvalidDataException or NotSupportedException
                                               or UnauthorizedAccessException or FileFormatException or ArgumentException
                                               or InvalidOperationException or System.Runtime.InteropServices.COMException
                                               or OverflowException)
                {
                    failed.Add(Path.GetFileName(file));
                }
                int n = Interlocked.Increment(ref done);
                progress?.Report((n, files.Count));
            }
            return 0;
        }));
        await Task.WhenAll(tasks);
        // Same name from two files: the later import replaced the earlier one.
        var unique = added.GroupBy(i => i.Id, StringComparer.OrdinalIgnoreCase).Select(g => g.First())
            .OrderBy(i => i.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
        return (unique, failed.Order(StringComparer.CurrentCultureIgnoreCase).ToList());
    }

    /// <summary>The icon goes to `exe` (null: to nobody). A program has one icon: whichever
    /// icon it had before is let go.</summary>
    public static async Task GiveAsync(SettingsModel model, LibraryIcon icon, string? exe)
    {
        var previous = model.AppIconChoices.FirstOrDefault(c => c.IconId == icon.Id);
        bool shortcuts = model.AppIcons;
        await Sta(() =>
        {
            if (previous is not null && previous.Program != exe) AppIconStore.Unassign(previous.Program);
            if (exe is not null) AppIconStore.Assign(exe, icon, shortcuts);
            return 0;
        });
        foreach (var c in model.AppIconChoices.Where(c => c.IconId == icon.Id || c.Program == exe).ToList())
            model.AppIconChoices.Remove(c);
        if (exe is not null) model.AppIconChoices.Add(new AppIconChoice(exe, icon.Id));
    }

    /// <summary>Gives each icon to its program when the name is a sure match and that program
    /// has no icon yet; the best icons first (a "low res" one only when nothing better is
    /// there). Returns how many were given.</summary>
    public static async Task<int> AutoGiveAsync(SettingsModel model, IEnumerable<LibraryIcon> icons)
    {
        var programs = await ProgramsAsync();
        int given = 0;
        foreach (var icon in icons.OrderBy(i => IconNames.IsLowQuality(i.Name)).ThenBy(i => i.Name.Length))
        {
            if (model.AppIconChoices.Any(c => c.IconId == icon.Id)) continue;
            string? exe = IconNames.Match(icon.Name, programs);
            if (exe is null || model.AppIconChoices.Any(c => c.Program == exe)) continue;
            await GiveAsync(model, icon, exe);
            given++;
        }
        return given;
    }

    /// <summary>Every program gets its own icon back (shortcuts too); the library stays.</summary>
    public static async Task UseWindowsIconsAsync(SettingsModel model)
    {
        var programs = model.AppIconChoices.Select(c => c.Program).ToList();
        await Sta(() =>
        {
            foreach (string exe in programs) AppIconStore.Unassign(exe);
            AppIconStore.RestoreShortcuts(null);   // leftovers of programs no longer listed
            return 0;
        });
        model.AppIconChoices.Clear();
    }

    /// <summary>Windows icons for everything, and the library emptied.</summary>
    public static async Task RemoveAllAsync(SettingsModel model)
    {
        await UseWindowsIconsAsync(model);
        await Sta(() =>
        {
            AppIconStore.DeleteAll();
            return 0;
        });
    }

    /// <summary>Gives every icon nobody uses yet to the program its name names; looks for
    /// programs again first (one may have been installed since). Returns how many.</summary>
    public static async Task<int> MatchAllAsync(SettingsModel model)
    {
        await ProgramsAsync(refresh: true);
        return await AutoGiveAsync(model, await Sta(AppIconStore.Library));
    }

    public static async Task SaveSetAsync(SettingsModel model, string path)
    {
        var apps = model.AppIconChoices.ToDictionary(c => c.Program, c => c.IconId, StringComparer.OrdinalIgnoreCase);
        await Sta(() =>
        {
            AppIconStore.SaveSet(path, apps);
            return 0;
        });
    }

    /// <summary>Adds a set's icons to the library and gives them to its programs; programs
    /// the set has no icon for get their own back. Returns how many programs got an icon.</summary>
    public static async Task<int> LoadSetAsync(SettingsModel model, string path, IProgress<(int Done, int Total)>? progress)
    {
        string folder = Path.Combine(Path.GetTempPath(), "MacWin-set-" + Guid.NewGuid().ToString("N"));
        try
        {
            var apps = await Sta(() => AppIconStore.ExtractSet(path, folder));
            var files = Directory.GetFiles(folder, "*.ico");
            var (added, _) = await ImportAsync(files, progress, f => Path.GetFileNameWithoutExtension(f));
            var byId = added.ToDictionary(i => i.Id, StringComparer.OrdinalIgnoreCase);
            foreach (var c in model.AppIconChoices.Where(c => !apps.ContainsKey(c.Program)).ToList())
            {
                await Sta(() =>
                {
                    AppIconStore.Unassign(c.Program);
                    return 0;
                });
                model.AppIconChoices.Remove(c);
            }
            int given = 0;
            foreach (var (exe, id) in apps)
            {
                if (!byId.TryGetValue(id, out var icon)) continue;
                await GiveAsync(model, icon, exe);
                given++;
            }
            return given;
        }
        finally
        {
            try
            {
                Directory.Delete(folder, recursive: true);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Temp files: Windows cleans them up eventually.
            }
        }
    }

    /// <summary>Runs one bigger change at a time (an import, a set, a reset, the bundled
    /// icons at start), so two never work on the same files.</summary>
    public static async Task<T> Exclusive<T>(Func<Task<T>> work)
    {
        await Gate.WaitAsync();
        try
        {
            return await work();
        }
        finally
        {
            Gate.Release();
        }
    }

    /// <summary>Imports the icons that come with MacWin and haven't been offered yet, and
    /// gives them to their programs. Runs at start; quiet when there is nothing new.</summary>
    public static async Task EnsureBundledAsync(SettingsModel model)
    {
        if (!await Gate.WaitAsync(0)) return;   // something else is changing icons: next start
        try
        {
            var files = await Sta(AppIconStore.NewBundledFiles);
            if (files.Count == 0) return;
            var (added, _) = await ImportAsync(files);
            await AutoGiveAsync(model, added);
            await Sta(() =>
            {
                AppIconStore.MarkSeen(files);
                return 0;
            });
            Changed?.Invoke();
        }
        catch (Exception ex)
        {
            App.Report(ex);
        }
        finally
        {
            Gate.Release();
        }
    }

    /// <summary>Runs `work` on a background thread in a single-threaded apartment.</summary>
    public static Task<T> Sta<T>(Func<T> work)
    {
        var done = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
        {
            try
            {
                done.SetResult(work());
            }
            catch (Exception ex)
            {
                done.SetException(ex);
            }
        })
        { IsBackground = true };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        return done.Task;
    }
}
