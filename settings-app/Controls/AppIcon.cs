// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Shapes;

namespace MacWin.Settings.Controls;

/// <summary>The MacWin icon (same motif as the tray icon), drawn as vectors at any size.</summary>
public sealed class AppIcon : Viewbox
{
    public AppIcon()
    {
        var canvas = new Canvas { Width = 100, Height = 100 };
        var tile = new Rectangle
        {
            Width = 92,
            Height = 92,
            RadiusX = 22,
            RadiusY = 22,
            Fill = new LinearGradientBrush(Color.FromRgb(0x33, 0x73, 0xFA), Color.FromRgb(0x8B, 0x4C, 0xE8), 90),
            Effect = new DropShadowEffect { BlurRadius = 10, ShadowDepth = 3, Direction = 270, Opacity = 0.25 },
        };
        Canvas.SetLeft(tile, 4);
        Canvas.SetTop(tile, 2);
        canvas.Children.Add(tile);

        // A window on top, pouring into a funnel towards the bottom right: the genie.
        var glyph = new Path
        {
            Fill = Brushes.White,
            Data = Geometry.Parse(
                "M22,20 H62 A5,5 0 0 1 67,25 V42 H17 V25 A5,5 0 0 1 22,20 Z " +
                "M17,46 H67 C62,56 64,66 72,74 L75,79 H68 C58,72 44,56 17,46 Z"),
        };
        canvas.Children.Add(glyph);
        Child = canvas;
    }
}
