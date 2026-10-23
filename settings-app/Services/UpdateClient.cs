// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.IO;
using System.Text.Json;

namespace MacWin.Settings;

/// <summary>Links to the project on GitHub.</summary>
internal static class Project
{
    public const string Author = "IMoggsYou";
    public const string AuthorUrl = "https://github.com/IMoggsYou";
    public const string RepoUrl = "https://github.com/IMoggsYou/MacWin";
    public const string ReleasesUrl = RepoUrl + "/releases";
    public const string ChangelogUrl = RepoUrl + "/blob/main/CHANGELOG.md";
    public const string IssuesUrl = RepoUrl + "/issues/new/choose";
    public const string LicenseUrl = RepoUrl + "/blob/main/LICENSE";

    public static string Version
    {
        get
        {
            var v = typeof(Project).Assembly.GetName().Version;
            return v is null ? "?" : $"{v.Major}.{v.Minor}.{v.Build}";
        }
    }
}

/// <summary>What MacWin.exe's updater last did (it writes update.json next to its log).</summary>
internal sealed record UpdateState(string Status, string? Current, string? Latest, DateTime? LastCheck, string? NotesUrl);

/// <summary>
/// The updater lives in MacWin.exe (it also works without this app): this side only
/// shows its state (%LOCALAPPDATA%\MacWin\update.json) and asks it to check or install.
/// </summary>
internal static class UpdateClient
{
    private const string UpdateMessage = "MacWin_Update_v1";
    private const int CheckNowCommand = 1, InstallCommand = 2;

    public static string StatePath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MacWin", "update.json");

    public static UpdateState Read()
    {
        try
        {
            if (File.Exists(StatePath))
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(StatePath));
                var root = doc.RootElement;
                string? Text(string name) =>
                    root.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.String ? p.GetString() : null;
                DateTime? lastCheck = DateTime.TryParse(Text("lastCheck"), null,
                    System.Globalization.DateTimeStyles.AdjustToUniversal | System.Globalization.DateTimeStyles.AssumeUniversal,
                    out var t) ? t.ToLocalTime() : null;
                return new UpdateState(Text("status") ?? "idle", Text("current"), Text("latest"), lastCheck, Text("notesUrl"));
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            // Being rewritten right now: the watcher brings the next version.
        }
        return new UpdateState("idle", null, null, null, null);
    }

    public static bool CheckNow() => HostControl.Post(UpdateMessage, CheckNowCommand);

    public static bool Install() => HostControl.Post(UpdateMessage, InstallCommand);
}
