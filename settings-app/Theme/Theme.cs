// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace MacWin.Settings;

/// <summary>
/// macOS-style light / dark palettes. Every brush is published under a "Brush.*" key and
/// used through DynamicResource, so switching the system theme recolors the app live.
/// </summary>
public static class Theme
{
    public static bool IsDark { get; private set; }
    public static event Action? Changed;

    public static bool SystemIsDark()
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
        return key?.GetValue("AppsUseLightTheme") is int v && v == 0;
    }

    public static void Apply(bool dark)
    {
        IsDark = dark;
        var palette = dark ? Dark : Light;
        var resources = Application.Current.Resources;
        foreach (var (key, hex) in palette)
        {
            var brush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
            brush.Freeze();
            resources["Brush." + key] = brush;
        }
        Changed?.Invoke();
    }

    public static Color Color(string key) =>
        Application.Current.Resources["Brush." + key] is SolidColorBrush b ? b.Color : Colors.Transparent;

    private static readonly (string, string)[] Light =
    {
        ("WindowBg", "#FFF5F5F7"),
        ("WindowBase", "#FFE8E8EC"),
        ("WindowOutline", "#2E000000"),
        ("Sidebar", "#99ECECF0"),
        ("Group", "#FFFFFFFF"),
        ("GroupBorder", "#FFE3E3E8"),
        ("Separator", "#FFE5E5EA"),
        ("Text", "#FF1D1D1F"),
        ("TextSecondary", "#FF6E6E73"),
        ("TextTertiary", "#FFAEAEB2"),
        ("Accent", "#FF007AFF"),
        ("AccentHover", "#FF1A86FF"),
        ("AccentPressed", "#FF0062CC"),
        ("Control", "#FFFFFFFF"),
        ("ControlBorder", "#FFCFCFD4"),
        ("ControlHover", "#FFF4F4F6"),
        ("ControlPressed", "#FFE6E6EA"),
        ("ToggleOff", "#FFE3E3E8"),
        ("Knob", "#FFFFFFFF"),
        ("SegmentBg", "#FFE6E6EB"),
        ("SegmentPill", "#FFFFFFFF"),
        ("SidebarHover", "#12000000"),
        ("Field", "#FFFFFFFF"),
        ("FieldBorder", "#FFD1D1D6"),
        ("FocusRing", "#59007AFF"),
        ("ScrollThumb", "#59000000"),
        ("TrafficInactive", "#FFD5D5D9"),
        ("ListHover", "#0A000000"),
        ("ListSelected", "#24007AFF"),
        ("Good", "#FF34C759"),
        ("Bad", "#FFFF3B30"),
    };

    private static readonly (string, string)[] Dark =
    {
        ("WindowBg", "#FF1E1E20"),
        ("WindowBase", "#FF262629"),
        ("WindowOutline", "#38FFFFFF"),
        ("Sidebar", "#8C262629"),
        ("Group", "#FF2A2A2D"),
        ("GroupBorder", "#FF37373A"),
        ("Separator", "#FF3A3A3D"),
        ("Text", "#FFF5F5F7"),
        ("TextSecondary", "#FF98989D"),
        ("TextTertiary", "#FF636366"),
        ("Accent", "#FF0A84FF"),
        ("AccentHover", "#FF2B95FF"),
        ("AccentPressed", "#FF0069D9"),
        ("Control", "#FF3A3A3D"),
        ("ControlBorder", "#FF4A4A4D"),
        ("ControlHover", "#FF444447"),
        ("ControlPressed", "#FF505053"),
        ("ToggleOff", "#FF3C3C3F"),
        ("Knob", "#FFF2F2F2"),
        ("SegmentBg", "#FF333336"),
        ("SegmentPill", "#FF5C5C60"),
        ("SidebarHover", "#14FFFFFF"),
        ("Field", "#FF2C2C2E"),
        ("FieldBorder", "#FF48484A"),
        ("FocusRing", "#800A84FF"),
        ("ScrollThumb", "#66FFFFFF"),
        ("TrafficInactive", "#FF4B4B4E"),
        ("ListHover", "#0FFFFFFF"),
        ("ListSelected", "#330A84FF"),
        ("Good", "#FF30D158"),
        ("Bad", "#FFFF453A"),
    };
}
