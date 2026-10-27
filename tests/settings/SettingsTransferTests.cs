// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Text.Json.Nodes;
using MacWin.Settings;

namespace MacWin.Settings.Tests;

/// <summary>General → Your settings: export, import and restore defaults, on JSON only.</summary>
internal static class SettingsTransferTests
{
    private const string Current = """
        {
          "settingsVersion": 3, "enabled": true, "language": "pl",
          "genie": {"style": "classic", "durationMs": 700, "excludedPrograms": ["game.exe"]},
          "appIcons": {"enabled": true, "apps": {"discord.exe": "discord"}},
          "widgets": {"show": true, "items": [{"id": "w1", "type": "notes", "text": "milk"}]},
          "updates": {"check": false}
        }
        """;

    public static void RunAll(Action<string, Action> run, Action<bool, string> check)
    {
        run("ExportKeepsSettingsNotIcons", () =>
        {
            var file = JsonNode.Parse(SettingsTransfer.Export(Parse(Current), "1.5.0"))!.AsObject();
            check(file[SettingsTransfer.Marker]?.GetValue<int>() == 1, "export has the marker");
            check(file["genie"]?["style"]?.GetValue<string>() == "classic", "export has the animations");
            check(file["widgets"]?["items"]?.AsArray().Count == 1, "export has the widgets");
            check(file["appIcons"] is null, "export leaves the app icons (files on this PC) out");
        });

        run("ImportReplacesWhatTheFileHas", () =>
        {
            var exported = Parse(SettingsTransfer.Export(Parse(Current), "1.5.0"));
            exported["genie"] = new JsonObject { ["style"] = "scale" };
            exported.Remove("widgets");
            var mine = Parse(Current);
            mine["genie"]!["style"] = "modern";
            var result = SettingsTransfer.Import(mine, exported.ToJsonString());
            check(result?["genie"]?["style"]?.GetValue<string>() == "scale", "import takes the file's animations");
            check(result?["widgets"]?["items"]?.AsArray().Count == 1, "import keeps widgets the file doesn't have");
            check(result?["appIcons"]?["apps"]?["discord.exe"]?.GetValue<string>() == "discord",
                  "import keeps this PC's app icons");
        });

        run("ImportRejectsOtherFiles", () =>
        {
            var mine = Parse(Current);
            check(SettingsTransfer.Import(mine, Current) is null, "a settings.json without the marker");
            check(SettingsTransfer.Import(mine, "not json") is null, "not JSON");
            check(SettingsTransfer.Import(mine, "[1, 2]") is null, "not an object");
            check(SettingsTransfer.Import(mine, """{"lampwinSettings": "yes"}""") is null, "a marker that isn't 1+");
        });

        run("ImportSkipsWrongShapes", () =>
        {
            var result = SettingsTransfer.Import(Parse(Current),
                """{"lampwinSettings": 1, "genie": "broken", "enabled": "no", "language": "fr", "evil": {"x": 1}}""");
            check(result?["genie"]?["style"]?.GetValue<string>() == "classic", "a genie that isn't an object is skipped");
            check(result?["enabled"]?.GetValue<bool>() == true, "an enabled that isn't true/false is skipped");
            check(result?["language"]?.GetValue<string>() == "fr", "a good value next to them comes in");
            check(result?["evil"] is null, "keys MacWin doesn't know stay out");
        });

        run("DefaultsKeepWidgetsIconsLanguage", () =>
        {
            var result = SettingsTransfer.Defaults(Parse(Current));
            check(result["genie"] is null && result["updates"] is null && result["enabled"] is null,
                  "animations, exclusions and updates go back to the defaults");
            check(result["widgets"]?["items"]?.AsArray().Count == 1, "widgets stay");
            check(result["appIcons"]?["apps"] is JsonObject, "app icons stay");
            check(result["language"]?.GetValue<string>() == "pl", "the language stays");
        });
    }

    private static JsonObject Parse(string json) => JsonNode.Parse(json)!.AsObject();
}
