// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

namespace MacWin.Settings;

/// <summary>
/// The switches of the Tweaks page: File Explorer, taskbar and Start options that Windows'
/// own Settings app and Folder Options write, for the signed-in user only (no administrator
/// prompt). The same engine as the Privacy page changes them, with a backup of its own.
/// Here a switch that is on means Lampwin's change is made; "Off" in the actions is that
/// change, "On" Windows' default (null: the value is deleted).
/// </summary>
public static class TweaksCatalog
{
    private const string ExplorerAdvanced = @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";

    /// <summary>The groups, in page order (settings.tweaks.group.&lt;name&gt;).</summary>
    public static readonly string[] Groups = ["explorer", "taskbar", "start"];

    public static readonly IReadOnlyList<PrivacyTweak> All =
    [
        // File Explorer (Folder Options; Settings > System > Advanced)
        Tweak("showExtensions", "explorer", ExplorerAdvanced, "HideFileExt", 0, 1),
        Tweak("showHidden", "explorer", ExplorerAdvanced, "Hidden", 1, 2),
        Tweak("fullPath", "explorer", @"Software\Microsoft\Windows\CurrentVersion\Explorer\CabinetState", "FullPath", 1, 0),
        Tweak("openThisPc", "explorer", ExplorerAdvanced, "LaunchTo", 1, null),
        Tweak("compactView", "explorer", ExplorerAdvanced, "UseCompactMode", 1, 0),

        // Taskbar (Settings > Personalization > Taskbar; Settings > System > Advanced)
        Tweak("taskbarLeft", "taskbar", ExplorerAdvanced, "TaskbarAl", 0, null),
        Tweak("hideSearch", "taskbar", @"Software\Microsoft\Windows\CurrentVersion\Search", "SearchboxTaskbarMode", 0, null),
        Tweak("hideTaskView", "taskbar", ExplorerAdvanced, "ShowTaskViewButton", 0, 1),
        Tweak("clockSeconds", "taskbar", ExplorerAdvanced, "ShowSecondsInSystemClock", 1, 0),
        Tweak("endTask", "taskbar", ExplorerAdvanced + @"\TaskbarDeveloperSettings", "TaskbarEndTask", 1, null),

        // Start (Settings > Personalization > Start)
        Tweak("startMorePins", "start", ExplorerAdvanced, "Start_Layout", 1, null),
    ];

    public static PrivacyTweak? Find(string id) => All.FirstOrDefault(t => t.Id == id);

    private static PrivacyTweak Tweak(string id, string group, string key, string name, int changed, int? windows) =>
        new(id, group,
            [new RegistryAction(PrivacyHive.User, key, name, RegData.Dword(changed),
                                windows is int value ? RegData.Dword(value) : null)],
            Texts: "tweaks");
}

/// <summary>The switches one page shows: the Privacy page's or the Tweaks page's.</summary>
public sealed class SwitchSet
{
    /// <summary>Text keys: settings.&lt;Texts&gt;.* (and the tweaks' own, see PrivacyTweak.TitleKey).</summary>
    public required string Texts { get; init; }
    public required string[] Groups { get; init; }
    public required IReadOnlyList<PrivacyTweak> All { get; init; }
    /// <summary>A switch that is on means the change is made (Tweaks), not that Windows does
    /// the thing (Privacy).</summary>
    public bool OnMeansChanged { get; init; }
    /// <summary>The file name of the backup in the privacy folder.</summary>
    public required string BackupName { get; init; }

    public static SwitchSet Privacy { get; } = new()
    {
        Texts = "privacy", Groups = PrivacyCatalog.Groups, All = PrivacyCatalog.All, BackupName = "backup.json",
    };

    public static SwitchSet Tweaks { get; } = new()
    {
        Texts = "tweaks", Groups = TweaksCatalog.Groups, All = TweaksCatalog.All, OnMeansChanged = true,
        BackupName = "tweaks.json",
    };

    public PrivacyTweak? Find(string id) => All.FirstOrDefault(t => t.Id == id);
}
