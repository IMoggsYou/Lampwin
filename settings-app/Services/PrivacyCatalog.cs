// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Text.RegularExpressions;

namespace MacWin.Settings;

/// <summary>
/// The switches of the Privacy page: Windows' documented policies and the values its own
/// Settings app writes (preferred where it has a switch, so Windows Settings keeps showing
/// the truth and stays usable). Each switch is off when all of its values are set.
///
/// Never in here, whatever the reason: Windows Update, Microsoft Defender, SmartScreen, the
/// firewall, UAC, virtualization-based security or anything else that keeps the PC safe
/// (tests/settings checks it).
/// </summary>
public static partial class PrivacyCatalog
{
    private const string DataCollection = @"SOFTWARE\Policies\Microsoft\Windows\DataCollection";
    private const string ContentDelivery = @"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager";
    private const string ExplorerAdvanced = @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";
    private const string SearchSettings = @"Software\Microsoft\Windows\CurrentVersion\SearchSettings";
    private const string WindowsAI = @"Software\Policies\Microsoft\Windows\WindowsAI";

    /// <summary>The groups, in page order (settings.privacy.group.&lt;name&gt;).</summary>
    public static readonly string[] Groups = ["diagnostics", "personalization", "suggestions", "search", "ai", "other"];

    public static readonly IReadOnlyList<PrivacyTweak> All =
    [
        // Diagnostic data
        new("telemetry", "diagnostics", [MachinePolicy(DataCollection, "AllowTelemetry", 0)]),
        new("diagTrack", "diagnostics",
            [new RegistryAction(PrivacyHive.Machine, @"SYSTEM\CurrentControlSet\Services\DiagTrack", "Start",
                                RegData.Dword(4), RegData.Dword(2), OnlyIfKeyExists: true)],
            Service: "DiagTrack"),
        new("appCompat", "diagnostics",
            [MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\AppCompat", "AITEnable", 0),
             MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\AppCompat", "DisableInventory", 1)]),
        new("ceip", "diagnostics",
            [MachinePolicy(@"SOFTWARE\Policies\Microsoft\SQMClient\Windows", "CEIPEnable", 0),
             new TaskAction(@"\Microsoft\Windows\Customer Experience Improvement Program\Consolidator"),
             new TaskAction(@"\Microsoft\Windows\Customer Experience Improvement Program\UsbCeip"),
             new TaskAction(@"\Microsoft\Windows\DiskDiagnostic\Microsoft-Windows-DiskDiagnosticDataCollector"),
             new TaskAction(@"\Microsoft\Windows\Feedback\Siuf\DmClient"),
             new TaskAction(@"\Microsoft\Windows\Feedback\Siuf\DmClientOnScenarioDownload")]),
        new("errorReporting", "diagnostics",
            [MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\Windows Error Reporting", "Disabled", 1)]),
        new("feedback", "diagnostics", [UserValue(@"Software\Microsoft\Siuf\Rules", "NumberOfSIUFInPeriod", 0, null)]),
        new("typingData", "diagnostics", [UserValue(@"Software\Microsoft\Input\TIPC", "Enabled", 0, 1)]),

        // Ads and personalization
        new("adId", "personalization",
            [UserValue(@"Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo", "Enabled", 0, 1)]),
        new("tailored", "personalization",
            [UserValue(@"Software\Microsoft\Windows\CurrentVersion\Privacy", "TailoredExperiencesWithDiagnosticDataEnabled", 0, 1)]),
        new("languageList", "personalization",
            [UserValue(@"Control Panel\International\User Profile", "HttpAcceptLanguageOptOut", 1, null)]),
        new("appLaunches", "personalization", [UserValue(ExplorerAdvanced, "Start_TrackProgs", 0, 1)]),
        new("inkingTyping", "personalization",
            [UserValue(@"Software\Microsoft\InputPersonalization", "RestrictImplicitInkCollection", 1, 0),
             UserValue(@"Software\Microsoft\InputPersonalization", "RestrictImplicitTextCollection", 1, 0),
             UserValue(@"Software\Microsoft\InputPersonalization\TrainedDataStore", "HarvestContacts", 0, 1)]),
        new("onlineSpeech", "personalization",
            [UserValue(@"Software\Microsoft\Speech_OneCore\Settings\OnlineSpeechPrivacy", "HasAccepted", 0, 1)]),

        // Suggestions and tips
        new("startRecommendations", "suggestions", [UserValue(ExplorerAdvanced, "Start_IrisRecommendations", 0, 1)],
            Explorer: true),
        new("accountNotifications", "suggestions", [UserValue(ExplorerAdvanced, "Start_AccountNotifications", 0, 1)],
            Explorer: true),
        new("settingsSuggestions", "suggestions",
            [UserValue(ContentDelivery, "SubscribedContent-338393Enabled", 0, 1),
             UserValue(ContentDelivery, "SubscribedContent-353694Enabled", 0, 1),
             UserValue(ContentDelivery, "SubscribedContent-353696Enabled", 0, 1)]),
        new("tips", "suggestions",
            [UserValue(ContentDelivery, "SubscribedContent-338389Enabled", 0, 1),
             UserValue(ContentDelivery, "SoftLandingEnabled", 0, 1)]),
        new("welcome", "suggestions",
            [UserValue(ContentDelivery, "SubscribedContent-310093Enabled", 0, 1),
             UserValue(@"Software\Microsoft\Windows\CurrentVersion\UserProfileEngagement", "ScoobeSystemSettingEnabled", 0, 1)]),
        // Windows installing promoted apps by itself (SystemPaneSuggestions is Windows 10's and
        // stays for the backups that have it; the Start menu's own suggestions are
        // startRecommendations).
        new("suggestedApps", "suggestions",
            [UserValue(ContentDelivery, "SilentInstalledAppsEnabled", 0, 1),
             UserValue(ContentDelivery, "SystemPaneSuggestionsEnabled", 0, 1),
             UserValue(ContentDelivery, "PreInstalledAppsEnabled", 0, 1),
             UserValue(ContentDelivery, "OemPreInstalledAppsEnabled", 0, 1)]),
        // Windows Spotlight brings its own facts with each picture: these tips only exist over
        // the user's own picture or slideshow.
        new("lockScreenTips", "suggestions",
            [UserValue(ContentDelivery, "RotatingLockScreenOverlayEnabled", 0, 1),
             UserValue(ContentDelivery, "SubscribedContent-338387Enabled", 0, 1)],
            Needs: PrivacyNeed.OwnLockScreenPicture),
        new("explorerAds", "suggestions", [UserValue(ExplorerAdvanced, "ShowSyncProviderNotifications", 0, 1)],
            Explorer: true),

        // Search and activity
        new("webSearch", "search",
            [UserValue(@"Software\Policies\Microsoft\Windows\Explorer", "DisableSearchBoxSuggestions", 1, null),
             UserValue(@"Software\Microsoft\Windows\CurrentVersion\Search", "BingSearchEnabled", 0, null)],
            Explorer: true),
        new("searchHighlights", "search", [UserValue(SearchSettings, "IsDynamicSearchBoxEnabled", 0, 1)]),
        new("searchHistory", "search", [UserValue(SearchSettings, "IsDeviceSearchHistoryEnabled", 0, 1)]),
        new("cloudSearch", "search",
            [UserValue(SearchSettings, "IsMSACloudSearchEnabled", 0, 1),
             UserValue(SearchSettings, "IsAADCloudSearchEnabled", 0, 1)]),
        new("activityHistory", "search",
            [MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\System", "PublishUserActivities", 0),
             MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\System", "UploadUserActivities", 0)]),

        // Copilot and AI. The Copilot in Windows 11 23H2 (a side pane) obeys the policy; since
        // 24H2 Copilot is an app of its own, which only goes away when it is uninstalled.
        new("copilot", "ai",
            [UserValue(@"Software\Policies\Microsoft\Windows\WindowsCopilot", "TurnOffWindowsCopilot", 1, null),
             UserValue(ExplorerAdvanced, "ShowCopilotButton", 0, null)],
            Explorer: true),
        new("copilotApp", "ai", [new PackageAction("Microsoft.Copilot_8wekyb3d8bbwe", "9NHT9RB2F4HD")]),
        // Recall: no snapshots, and (24H2 and later) the feature itself removed.
        new("recall", "ai",
            [UserValue(WindowsAI, "DisableAIDataAnalysis", 1, null),
             MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\WindowsAI", "DisableAIDataAnalysis", 1),
             MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\WindowsAI", "AllowRecallEnablement", 0)],
            Needs: PrivacyNeed.CopilotPlus),
        new("clickToDo", "ai",
            [UserValue(WindowsAI, "DisableClickToDo", 1, null),
             MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\WindowsAI", "DisableClickToDo", 1)],
            Needs: PrivacyNeed.CopilotPlus),

        // Other. The Widgets button is protected from other programs since 2024; the policy
        // is what turns Widgets off, once Explorer restarts.
        new("windowsWidgets", "other", [MachinePolicy(@"SOFTWARE\Policies\Microsoft\Dsh", "AllowNewsAndInterests", 0)],
            Explorer: true),
        new("deliveryOptimization", "other",
            [MachinePolicy(@"SOFTWARE\Policies\Microsoft\Windows\DeliveryOptimization", "DODownloadMode", 0)]),
    ];

    public static PrivacyTweak? Find(string id) => All.FirstOrDefault(t => t.Id == id);

    // A machine policy: set to block, deleted (not configured) for Windows' default.
    private static RegistryAction MachinePolicy(string key, string name, int off) =>
        new(PrivacyHive.Machine, key, name, RegData.Dword(off), null);

    private static RegistryAction UserValue(string key, string name, int off, int? on) =>
        new(PrivacyHive.User, key, name, RegData.Dword(off), on is int value ? RegData.Dword(value) : null);

    /// <summary>"telemetry=off,adId=on" (the elevated helper's argument): only switches of the
    /// catalogue, each once. Null when anything else is in it.</summary>
    public static IReadOnlyList<PrivacyChange>? ParseChanges(string? text)
    {
        if (string.IsNullOrWhiteSpace(text)) return null;
        var changes = new List<PrivacyChange>();
        foreach (string part in text.Split(','))
        {
            string[] pair = part.Split('=');
            if (pair.Length != 2 || Find(pair[0]) is not PrivacyTweak tweak || changes.Any(c => c.Tweak == tweak)) return null;
            if (pair[1] is not ("on" or "off")) return null;
            changes.Add(new PrivacyChange(tweak, pair[1] == "off"));
        }
        return changes;
    }

    public static string FormatChanges(IEnumerable<PrivacyChange> changes) =>
        string.Join(',', changes.Select(c => $"{c.Tweak.Id}={(c.TurnOff ? "off" : "on")}"));

    /// <summary>A user account's SID (local, Microsoft or Entra ID account).</summary>
    public static bool IsUserSid(string? sid) => sid is not null && UserSid().IsMatch(sid);

    [GeneratedRegex(@"\AS-1-(5-21|12-1)(-\d{1,10}){3,6}\z")]
    private static partial Regex UserSid();
}
