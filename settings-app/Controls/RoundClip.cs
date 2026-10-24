// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Media;

namespace MacWin.Settings.Controls;

/// <summary>
/// RoundClip.Radius: clips an element to a rounded rectangle of its own size. A Border's
/// CornerRadius only rounds what the Border draws itself, and ClipToBounds clips square,
/// so content filling a rounded card would otherwise cover the card's corners.
/// </summary>
public static class RoundClip
{
    public static readonly DependencyProperty RadiusProperty = DependencyProperty.RegisterAttached(
        "Radius", typeof(double), typeof(RoundClip), new PropertyMetadata(0.0, OnRadiusChanged));

    public static double GetRadius(DependencyObject d) => (double)d.GetValue(RadiusProperty);
    public static void SetRadius(DependencyObject d, double value) => d.SetValue(RadiusProperty, value);

    private static void OnRadiusChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not FrameworkElement element) return;
        element.SizeChanged -= OnSizeChanged;
        element.SizeChanged += OnSizeChanged;
        Apply(element);
    }

    private static void OnSizeChanged(object sender, SizeChangedEventArgs e) => Apply((FrameworkElement)sender);

    private static void Apply(FrameworkElement element)
    {
        double r = GetRadius(element);
        var clip = new RectangleGeometry(new Rect(0, 0, element.ActualWidth, element.ActualHeight), r, r);
        clip.Freeze();
        element.Clip = clip;
    }
}
