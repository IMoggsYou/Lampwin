// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MacWin.Settings;

/// <summary>
/// The settings as a file of their own (General → Your settings): exported to move them to
/// another PC or keep them safe, imported back, or reset. Works on settings.json's content
/// only; SettingsStore reads and writes the file.
/// </summary>
public static class SettingsTransfer
{
    /// <summary>Marks an exported file: anything else is not imported.</summary>
    public const string Marker = "lampwinSettings";

    // What moves between PCs, with the kind of JSON each one is. App icons don't: their
    // pictures are files on this PC (the App icons page saves them as an icon set).
    private static readonly (string Key, JsonValueKind Kind)[] Portable =
    [
        ("settingsVersion", JsonValueKind.Number), ("enabled", JsonValueKind.True), ("devMode", JsonValueKind.True),
        ("devAllowlist", JsonValueKind.Array), ("noInject", JsonValueKind.Array), ("language", JsonValueKind.String),
        ("updates", JsonValueKind.Object), ("genie", JsonValueKind.Object), ("widgets", JsonValueKind.Object),
        ("desktop", JsonValueKind.Object), ("appearance", JsonValueKind.Object),
    ];

    // What "Restore defaults" takes back: animations, corners, updates, the programs left
    // out, hot corners and the light / dark schedule. Widgets, app icons and the language
    // stay (the Privacy and Tweaks pages keep their own backups).
    private static readonly string[] Resettable =
        ["enabled", "devMode", "devAllowlist", "noInject", "updates", "genie", "desktop", "appearance"];

    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>The file's text; `version` (the app's) says which one wrote it.</summary>
    public static string Export(JsonObject settings, string version)
    {
        var file = new JsonObject { [Marker] = 1, ["exportedBy"] = version };
        foreach (var (key, kind) in Portable)
        {
            if (Fits(settings[key], kind)) file[key] = settings[key]!.DeepClone();
        }
        return file.ToJsonString(WriteOptions) + "\n";
    }

    /// <summary>The settings with an exported file's put in; null when `text` isn't one.
    /// Settings the file doesn't have, or has in a shape MacWin can't read, stay as they are.</summary>
    public static JsonObject? Import(JsonObject settings, string text)
    {
        JsonObject? file;
        try
        {
            file = JsonNode.Parse(text) as JsonObject;
        }
        catch (JsonException)
        {
            return null;
        }
        if (file?[Marker] is not JsonValue marker || !marker.TryGetValue(out int version) || version < 1) return null;
        var result = (JsonObject)settings.DeepClone();
        foreach (var (key, kind) in Portable)
        {
            if (Fits(file[key], kind)) result[key] = file[key]!.DeepClone();
        }
        return result;
    }

    public static JsonObject Defaults(JsonObject settings)
    {
        var result = (JsonObject)settings.DeepClone();
        foreach (string key in Resettable) result.Remove(key);
        return result;
    }

    private static bool Fits(JsonNode? node, JsonValueKind kind)
    {
        var actual = node?.GetValueKind();
        return kind == JsonValueKind.True ? actual is JsonValueKind.True or JsonValueKind.False : actual == kind;
    }
}
