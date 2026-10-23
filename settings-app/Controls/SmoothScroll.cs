// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Animation;

namespace MacWin.Settings.Controls;

/// <summary>Eased mouse-wheel scrolling (ScrollViewer.VerticalOffset itself can't be animated).</summary>
public static class SmoothScroll
{
    public static readonly DependencyProperty EnabledProperty = DependencyProperty.RegisterAttached(
        "Enabled", typeof(bool), typeof(SmoothScroll), new PropertyMetadata(false, OnEnabledChanged));

    private static readonly DependencyProperty OffsetProperty = DependencyProperty.RegisterAttached(
        "Offset", typeof(double), typeof(SmoothScroll), new PropertyMetadata(0.0, OnOffsetChanged));

    private static readonly DependencyProperty TargetProperty = DependencyProperty.RegisterAttached(
        "Target", typeof(double), typeof(SmoothScroll), new PropertyMetadata(-1.0));

    public static bool GetEnabled(DependencyObject d) => (bool)d.GetValue(EnabledProperty);
    public static void SetEnabled(DependencyObject d, bool value) => d.SetValue(EnabledProperty, value);

    private static void OnEnabledChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ScrollViewer sv) return;
        if ((bool)e.NewValue) sv.PreviewMouseWheel += OnWheel;
        else sv.PreviewMouseWheel -= OnWheel;
    }

    private static void OnWheel(object sender, MouseWheelEventArgs e)
    {
        var sv = (ScrollViewer)sender;
        if (sv.ScrollableHeight <= 0) return;
        e.Handled = true;
        double target = (double)sv.GetValue(TargetProperty);
        if (target < 0 || Math.Abs(target - sv.VerticalOffset) > sv.ViewportHeight) target = sv.VerticalOffset;
        target = Math.Clamp(target - e.Delta * 0.9, 0, sv.ScrollableHeight);
        sv.SetValue(TargetProperty, target);

        var anim = new DoubleAnimation(sv.VerticalOffset, target, TimeSpan.FromMilliseconds(260))
        {
            EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut },
        };
        anim.Completed += (_, _) => sv.SetValue(TargetProperty, -1.0);
        sv.BeginAnimation(OffsetProperty, anim, HandoffBehavior.SnapshotAndReplace);
    }

    private static void OnOffsetChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ScrollViewer sv) sv.ScrollToVerticalOffset((double)e.NewValue);
    }
}
