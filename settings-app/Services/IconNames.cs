// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.IO;
using System.Text.RegularExpressions;

namespace MacWin.Settings;

/// <summary>Names for imported icons, and the program an icon is probably meant for.</summary>
internal static partial class IconNames
{
    /// <summary>A readable name from an icon's file name. Icon sites name their downloads
    /// like "Discord__Dark___macOS_26.2___VGpMiSdUqI_icns-f048a3f6c5": the id and hash are
    /// dropped, "__Dark__" becomes "(Dark)", underscores become spaces.</summary>
    public static string Clean(string fileNameWithoutExtension)
    {
        string s = fileNameWithoutExtension;
        s = HashSuffix().Replace(s, "");       // "_icns-f048a3f6c5" / "-4a53dc97aa"
        s = IdSuffix().Replace(s, "");         // "___VGpMiSdUqI"
        s = Qualifier().Replace(s, " ($1) ");  // "__Dark__"
        s = s.Replace('_', ' ');
        s = Spaces().Replace(s, " ").Trim();
        s = s.Replace("( ", "(").Replace(" )", ")");
        return s.Length > 0 ? s : fileNameWithoutExtension;
    }

    [GeneratedRegex(@"(_icns)?-[0-9a-f]{8,}$", RegexOptions.IgnoreCase)]
    private static partial Regex HashSuffix();
    // A random id: 10 letters/digits with at least one capital and one lowercase letter,
    // after an underscore (only that one: "__Tahoe__id" keeps the "__" that closes "(Tahoe)").
    [GeneratedRegex(@"_(?=[A-Za-z0-9]{10}$)(?=[A-Za-z0-9]*[A-Z])(?=[A-Za-z0-9]*[a-z])[A-Za-z0-9]{10}$")]
    private static partial Regex IdSuffix();
    // "__Dark__", or a last "__Liquid_Glass_" whose closing underscores went with the id.
    [GeneratedRegex(@"__([^_]+(?:_[^_]+)*?)(?:__|_?$)")]
    private static partial Regex Qualifier();
    [GeneratedRegex(@"\s+")]
    private static partial Regex Spaces();

    // Programs whose file name doesn't follow from their name. Ambiguous ones (Tor
    // Browser runs as firefox.exe) are left to the user.
    private static readonly (string Prefix, string[] Programs)[] Known =
    [
        ("visual studio code", ["code.exe"]),
        ("vs code", ["code.exe"]),
        ("counter-strike 2", ["cs2.exe"]),
        ("davinci resolve", ["resolve.exe"]),
        ("jetbrains intellij", ["idea64.exe"]),
        ("jetbrains inteliij", ["idea64.exe"]),
        ("intellij", ["idea64.exe"]),
        ("microsoft outlook", ["olk.exe", "outlook.exe"]),
        ("outlook", ["olk.exe", "outlook.exe"]),
        ("microsoft powerpoint", ["powerpnt.exe"]),
        ("microsoft word", ["winword.exe"]),
        ("microsoft excel", ["excel.exe"]),
        ("microsoft teams", ["ms-teams.exe"]),
        ("proton vpn", ["protonvpn.client.exe", "protonvpn.exe"]),
        ("roblox studio", ["robloxstudiobeta.exe"]),
        ("roblox", ["robloxplayerbeta.exe"]),
        ("terminal", ["windowsterminal.exe"]),
        ("finder", ["explorer.exe"]),
        ("google chrome", ["chrome.exe"]),
        ("microsoft edge", ["msedge.exe"]),
        ("obs", ["obs64.exe"]),
        ("steam", ["steam.exe"]),
    ];

    // Words in icon names that describe the icon, not the program.
    private static readonly HashSet<string> Noise = new(StringComparer.Ordinal)
    {
        "dark", "light", "macos", "ios", "ipados", "icon", "icons", "beta", "liquid", "glass", "low", "res",
        "golden", "gate", "tahoe", "sequoia", "sonoma", "ventura", "monterey", "big", "sur", "app", "logo",
        "alt", "alternative", "new", "old", "classic", "mono", "color", "colour", "default", "flat", "squircle",
    };

    /// <summary>The program's name inside an icon name, as letters and digits only:
    /// "Discord (Dark) macOS 26.2" -> "discord", "low res Google Chrome" -> "googlechrome",
    /// "Counter-Strike 2" -> "counterstrike".</summary>
    public static string Key(string name)
    {
        string s = Parenthesis().Replace(name, " ").ToLowerInvariant();
        var words = NonWord().Split(s).Where(w => w.Length > 0 && !Noise.Contains(w) && !w.All(char.IsDigit)).ToList();
        return string.Concat(words);
    }

    private static string Compact(string text) => NonWord().Replace(text.ToLowerInvariant(), "");

    /// <summary>The program on this PC an icon called `name` is for, or null when none is a
    /// sure match (then the icon waits in the library, given to nobody). A program's name
    /// (its shortcut's) or file name must be the icon's name, or start or end with it.</summary>
    public static string? Match(string name, IReadOnlyList<KnownProgram> programs)
    {
        // Programs whose file name doesn't follow from their name come first.
        foreach (string exe in Candidates(name))
            if (programs.Any(p => p.Exe == exe)) return exe;
        string key = Key(name);
        // Tor Browser runs as firefox.exe: an icon given to it would go to Firefox as well.
        if (key.Length < 3 || key.StartsWith("torbrowser", StringComparison.Ordinal)) return null;
        KnownProgram? best = null;
        int bestScore = 0;
        foreach (var p in programs)
        {
            foreach (string candidate in new[] { Compact(p.Name), Compact(Path.GetFileNameWithoutExtension(p.Exe)) })
            {
                int score = candidate == key ? 3
                    : candidate.Length >= 4 && key.Length >= 4 && (candidate.StartsWith(key, StringComparison.Ordinal) ||
                                                                   key.StartsWith(candidate, StringComparison.Ordinal)) ? 2
                    : candidate.Length >= 5 && key.Length >= 5 && (key.EndsWith(candidate, StringComparison.Ordinal) ||
                                                                   candidate.EndsWith(key, StringComparison.Ordinal)) ? 1
                    : 0;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = p;
                }
            }
        }
        return best?.Exe;
    }

    /// <summary>Icons of lower quality are matched last, so a program gets the best one.</summary>
    public static bool IsLowQuality(string name) =>
        name.StartsWith("low res", StringComparison.OrdinalIgnoreCase) || name.Contains("lowres", StringComparison.OrdinalIgnoreCase);

    [GeneratedRegex(@"\([^)]*\)")]
    private static partial Regex Parenthesis();
    [GeneratedRegex(@"[^\p{L}\p{N}]+")]
    private static partial Regex NonWord();

    /// <summary>The programs (lowercase file names) an icon called `name` may be for, best
    /// first. The caller keeps those found on the PC.</summary>
    public static IEnumerable<string> Candidates(string name)
    {
        string lower = Qualifier().Replace(name.ToLowerInvariant(), "");
        lower = lower.Replace('(', ' ').Replace(')', ' ').Trim();
        if (lower.StartsWith("low res ", StringComparison.Ordinal)) lower = lower[8..];
        foreach (var (prefix, programs) in Known)
        {
            if (lower.StartsWith(prefix, StringComparison.Ordinal))
            {
                foreach (string p in programs) yield return p;
                yield break;
            }
        }
    }
}
