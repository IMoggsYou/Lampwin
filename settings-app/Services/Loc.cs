// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Data;
using System.Windows.Markup;
using System.Windows.Media;

namespace MacWin.Settings;

/// <summary>
/// UI strings from i18n/&lt;code&gt;.json (embedded in this app). The language is the
/// "language" setting, or with "auto" the first Windows display language MacWin has;
/// anything else gets English. The host picks its language the same way
/// (src/host/i18n.cpp), so the tray menu and this window always agree.
/// </summary>
public static class Loc
{
    /// <summary>Every language MacWin ships, English first.</summary>
    public static readonly string[] Languages = ["en", "pl", "zh-Hans", "hi", "es", "fr", "ar", "bn", "ru", "pt-BR", "ja"];

    private static readonly Dictionary<string, Dictionary<string, string>> Cache = new();
    private static readonly Dictionary<string, string> English = Load("en");
    private static Dictionary<string, string> _strings = English;

    /// <summary>The language in use, one of <see cref="Languages"/>.</summary>
    public static string Code { get; private set; } = "en";

    public static bool IsRightToLeft => Code == "ar";
    public static FlowDirection FlowDirection => IsRightToLeft ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
    public static XmlLanguage XmlLanguage => XmlLanguage.GetLanguage(Code switch
    {
        "zh-Hans" => "zh-CN",
        _ => Code,
    });

    /// <summary>Raised after the language changed (the window rebuilds its pages).</summary>
    public static event Action? Changed;

    public static string T(string key) =>
        _strings.TryGetValue(key, out var s) ? s : English.TryGetValue(key, out var e) ? e : key;

    public static string F(string key, params object?[] args) => string.Format(CultureInfo.CurrentCulture, T(key), args);

    /// <summary>The English text, e.g. so English search words always work.</summary>
    public static string InEnglish(string key) => English.TryGetValue(key, out var e) ? e : key;

    /// <summary>A language's own name ("Polski", "日本語").</summary>
    public static string NativeName(string code) =>
        Load(code).TryGetValue("language.name", out var name) ? name : code;

    /// <summary>Switches to the language for a "language" setting ("auto" or a code).</summary>
    public static void Apply(string? setting)
    {
        string code = Resolve(setting, PreferredUILanguages());
        if (code == Code && ReferenceEquals(_strings, Load(code))) return;
        Code = code;
        _strings = Load(code);
        ApplyFonts();
        Changed?.Invoke();
    }

    /// <summary>The shared rule: an explicit supported code wins; otherwise the first
    /// preferred Windows language MacWin has (any zh-* is Simplified Chinese, any pt-* is
    /// Brazilian Portuguese); otherwise English.</summary>
    public static string Resolve(string? setting, IEnumerable<string> preferred)
    {
        if (!string.IsNullOrEmpty(setting) && !setting.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            string? exact = Languages.FirstOrDefault(l => l.Equals(setting, StringComparison.OrdinalIgnoreCase));
            if (exact is not null) return exact;
        }
        foreach (string tag in preferred)
        {
            string primary = tag.Split('-', '_')[0].ToLowerInvariant();
            string? match = primary switch
            {
                "zh" => "zh-Hans",
                "pt" => "pt-BR",
                _ => Languages.FirstOrDefault(l => l == primary),
            };
            if (match is not null) return match;
        }
        return "en";
    }

    // Segoe UI Variable has Latin, Greek and Cyrillic only: other scripts get the
    // Windows UI font made for them (Segoe UI itself has Arabic).
    private static void ApplyFonts()
    {
        if (Application.Current?.Resources is not { } resources) return;
        string text = "Segoe UI Variable Text, Segoe UI", display = "Segoe UI Variable Display, Segoe UI";
        string? script = Code switch
        {
            "ja" => "Yu Gothic UI, Meiryo UI",
            "zh-Hans" => "Microsoft YaHei UI",
            "hi" or "bn" => "Nirmala UI",
            _ => null,
        };
        if (Code == "ar")
        {
            text = display = "Segoe UI";
        }
        else if (script is not null)
        {
            text += ", " + script;
            display += ", " + script;
        }
        resources["Font.Text"] = new FontFamily(text);
        resources["Font.Display"] = new FontFamily(display);
    }

    private static Dictionary<string, string> Load(string code)
    {
        lock (Cache)
        {
            if (Cache.TryGetValue(code, out var cached)) return cached;
            var strings = new Dictionary<string, string>(StringComparer.Ordinal);
            try
            {
                using Stream? stream = typeof(Loc).Assembly.GetManifestResourceStream($"i18n.{code}.json");
                if (stream is not null)
                {
                    var parsed = JsonSerializer.Deserialize<Dictionary<string, string>>(stream);
                    if (parsed is not null) strings = new Dictionary<string, string>(parsed, StringComparer.Ordinal);
                }
            }
            catch (JsonException)
            {
                // A broken file falls back to English key by key (T).
            }
            Cache[code] = strings;
            return strings;
        }
    }

    /// <summary>The user's Windows display languages, most preferred first.</summary>
    public static IReadOnlyList<string> PreferredUILanguages()
    {
        const uint MUI_LANGUAGE_NAME = 0x8;
        try
        {
            uint size = 0;
            if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, out _, null, ref size) && size > 0)
            {
                var buffer = new char[size];
                if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, out _, buffer, ref size))
                    return new string(buffer).Split('\0', StringSplitOptions.RemoveEmptyEntries);
            }
        }
        catch (EntryPointNotFoundException)
        {
        }
        return [CultureInfo.CurrentUICulture.Name];
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetUserPreferredUILanguages(uint flags, out uint count, char[]? buffer, ref uint size);
}

/// <summary>{l:Tr some.key} in XAML: the text in the current language. Pages are rebuilt
/// when the language changes, so a plain string is enough.</summary>
[MarkupExtensionReturnType(typeof(string))]
public sealed class TrExtension : MarkupExtension
{
    public TrExtension() { }

    public TrExtension(string key) => Key = key;

    [ConstructorArgument("key")]
    public string Key { get; set; } = "";

    public override object ProvideValue(IServiceProvider serviceProvider) => Loc.T(Key);
}

/// <summary>{l:UnitWidth settings.unit.ms}: the width a slider's value needs ("8888 ms" in the
/// current language and font), so the slider doesn't shift while it is dragged.</summary>
[MarkupExtensionReturnType(typeof(double))]
public sealed class UnitWidthExtension : MarkupExtension
{
    public UnitWidthExtension() { }

    public UnitWidthExtension(string key) => Key = key;

    [ConstructorArgument("key")]
    public string Key { get; set; } = "";

    public override object ProvideValue(IServiceProvider serviceProvider)
    {
        var family = Application.Current?.TryFindResource("Font.Text") as FontFamily ?? new FontFamily("Segoe UI");
        var text = new FormattedText(Loc.F(Key, "8888"), CultureInfo.CurrentUICulture, Loc.FlowDirection,
                                     new Typeface(family, FontStyles.Normal, FontWeights.Normal, FontStretches.Normal),
                                     13, Brushes.Black, 1.0);
        return Math.Max(48, Math.Ceiling(text.WidthIncludingTrailingWhitespace) + 6);
    }
}

/// <summary>A number with its unit in the current language ("450 ms", "450 мс").</summary>
public sealed class UnitConverter : IValueConverter
{
    public string Key { get; set; } = "";

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
        Loc.F(Key, value is double d ? Math.Round(d).ToString(CultureInfo.CurrentCulture) : value);

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
        Binding.DoNothing;
}
