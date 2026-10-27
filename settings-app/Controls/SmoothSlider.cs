// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace MacWin.Settings.Controls;

/// <summary>
/// SmoothSlider.IsEnabled (set by the MacSlider style): a click on the track glides the
/// knob to that spot instead of jumping, and the knob grows under the pointer and a little
/// more while it is held, with a soft spring. The knob is scaled as a whole (the Thumb
/// itself), so nothing inside it can clip the bigger knob.
/// </summary>
public static class SmoothSlider
{
    public static readonly DependencyProperty IsEnabledProperty = DependencyProperty.RegisterAttached(
        "IsEnabled", typeof(bool), typeof(SmoothSlider), new PropertyMetadata(false, OnIsEnabledChanged));

    public static bool GetIsEnabled(DependencyObject d) => (bool)d.GetValue(IsEnabledProperty);
    public static void SetIsEnabled(DependencyObject d, bool value) => d.SetValue(IsEnabledProperty, value);

    // The glide running on a slider (only the current one may set the final value).
    private static readonly DependencyProperty GlideProperty = DependencyProperty.RegisterAttached(
        "Glide", typeof(DoubleAnimation), typeof(SmoothSlider), new PropertyMetadata(null));

    private const double HoverScale = 1.08, HeldScale = 1.2;

    private static void OnIsEnabledChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not Slider slider || !(bool)e.NewValue) return;
        slider.PreviewMouseLeftButtonDown += OnPreviewMouseDown;
        slider.Loaded += (_, _) => HookThumb(slider);
        if (slider.IsLoaded) HookThumb(slider);
    }

    private static Track? TrackOf(Slider slider) => slider.Template?.FindName("PART_Track", slider) as Track;

    private static void HookThumb(Slider slider)
    {
        slider.ApplyTemplate();
        if (TrackOf(slider)?.Thumb is not Thumb thumb || thumb.RenderTransform is ScaleTransform { IsFrozen: false }) return;
        thumb.RenderTransformOrigin = new Point(0.5, 0.5);
        thumb.RenderTransform = new ScaleTransform();
        thumb.MouseEnter += (_, _) => UpdateKnob(thumb);
        thumb.MouseLeave += (_, _) => UpdateKnob(thumb);
        thumb.DragStarted += (_, _) => UpdateKnob(thumb);
        thumb.DragCompleted += (_, _) => UpdateKnob(thumb);
    }

    private static void UpdateKnob(Thumb thumb)
    {
        if (thumb.RenderTransform is not ScaleTransform scale) return;
        double to = thumb.IsDragging ? HeldScale : thumb.IsMouseOver ? HoverScale : 1;
        if (!Motion.On)
        {
            scale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
            scale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
            scale.ScaleX = scale.ScaleY = to;
            return;
        }
        bool growing = to > scale.ScaleX;
        // Growing overshoots a touch and settles (a spring); shrinking just eases back.
        IEasingFunction ease = growing
            ? new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.45 }
            : new QuinticEase { EasingMode = EasingMode.EaseOut };
        var duration = TimeSpan.FromMilliseconds(growing ? 280 : 220);
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(to, duration) { EasingFunction = ease });
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(to, duration) { EasingFunction = ease });
    }

    private static void OnPreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        var slider = (Slider)sender;
        if (TrackOf(slider) is not Track track || !slider.IsEnabled) return;
        // On the knob: it is dragged as usual, from wherever a glide left it.
        if (IsInside<Thumb>(e.OriginalSource as DependencyObject, track))
        {
            Settle(slider);
            return;
        }
        double target = track.ValueFromPoint(e.GetPosition(track));
        if (slider.IsSnapToTickEnabled && slider.TickFrequency > 0)
        {
            target = slider.Minimum + Math.Round((target - slider.Minimum) / slider.TickFrequency) * slider.TickFrequency;
        }
        target = Math.Clamp(target, slider.Minimum, slider.Maximum);
        e.Handled = true;
        Glide(slider, target, track);
    }

    private static void Glide(Slider slider, double target, Track track)
    {
        double from = slider.Value;
        if (from == target) return;
        if (!Motion.On)
        {
            slider.SetCurrentValue(RangeBase.ValueProperty, target);
            return;
        }
        // Longer ways take a little longer, never sluggish.
        double px = Math.Abs(target - from) / Math.Max(1e-9, slider.Maximum - slider.Minimum) * track.ActualWidth;
        var anim = new DoubleAnimation(from, target, TimeSpan.FromMilliseconds(Math.Clamp(160 + px * 0.6, 180, 340)))
        {
            EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut },
            FillBehavior = FillBehavior.Stop,
        };
        // The animation drives the value (and its binding) on the way; the base value is
        // already the target, so nothing jumps when it ends. Set again at the end: the
        // binding may have written a rounded value from the way back meanwhile.
        anim.Completed += (_, _) =>
        {
            if (slider.GetValue(GlideProperty) == anim) slider.SetCurrentValue(RangeBase.ValueProperty, target);
        };
        slider.SetValue(GlideProperty, anim);
        slider.BeginAnimation(RangeBase.ValueProperty, anim);
        slider.SetCurrentValue(RangeBase.ValueProperty, target);
    }

    /// <summary>Stops a glide where it is.</summary>
    private static void Settle(Slider slider)
    {
        slider.ClearValue(GlideProperty);
        double now = slider.Value;
        slider.BeginAnimation(RangeBase.ValueProperty, null);
        slider.SetCurrentValue(RangeBase.ValueProperty, now);
    }

    private static bool IsInside<T>(DependencyObject? d, DependencyObject stop) where T : DependencyObject
    {
        for (; d is not null && d != stop; d = d is Visual ? VisualTreeHelper.GetParent(d) : LogicalTreeHelper.GetParent(d))
        {
            if (d is T) return true;
        }
        return false;
    }
}
