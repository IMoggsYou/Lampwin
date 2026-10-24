// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace MacWin.Settings.Controls;

/// <summary>
/// ComboBoxMotion.IsEnabled (set by the MacComboBox style): the open menu unfolds - it
/// grows in from its pop-up button and its items slide in one after another - and it
/// stays with its button while the page scrolls. A popup is a window of its own and would
/// otherwise stay where it opened; once the button scrolls out of sight the menu closes.
/// </summary>
public static class ComboBoxMotion
{
    public static readonly DependencyProperty IsEnabledProperty = DependencyProperty.RegisterAttached(
        "IsEnabled", typeof(bool), typeof(ComboBoxMotion), new PropertyMetadata(false, OnIsEnabledChanged));

    public static bool GetIsEnabled(DependencyObject d) => (bool)d.GetValue(IsEnabledProperty);
    public static void SetIsEnabled(DependencyObject d, bool value) => d.SetValue(IsEnabledProperty, value);

    private const double ItemDelayMs = 22, ItemMs = 260, MenuMs = 220;

    private static void OnIsEnabledChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ComboBox box || !(bool)e.NewValue) return;
        box.DropDownOpened += (_, _) => Opened(box);
        box.DropDownClosed += (_, _) => Follow(box, false);
    }

    private static void Opened(ComboBox box)
    {
        if (box.Template?.FindName("PART_Popup", box) is not Popup popup || popup.Child is not FrameworkElement menu) return;
        Follow(box, true);
        // The menu grows out of its button: from slightly smaller, just above, see-through.
        menu.RenderTransformOrigin = new Point(0.5, 0);
        var scale = new ScaleTransform(0.96, 0.96);
        var shift = new TranslateTransform(0, -4);
        menu.RenderTransform = new TransformGroup { Children = { scale, shift } };
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        var menuTime = TimeSpan.FromMilliseconds(MenuMs);
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, new DoubleAnimation(1, menuTime) { EasingFunction = ease });
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, new DoubleAnimation(1, menuTime) { EasingFunction = ease });
        shift.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(0, menuTime) { EasingFunction = ease });
        menu.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(MenuMs * 0.7)));

        // The items once laid out: each slides down into place a moment after the one above.
        box.Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Loaded, () =>
        {
            for (int i = 0; i < box.Items.Count; i++)
            {
                if (box.ItemContainerGenerator.ContainerFromIndex(i) is not FrameworkElement item) continue;
                double begin = 40 + i * ItemDelayMs;
                var slide = new TranslateTransform();
                item.RenderTransform = slide;
                // Held at the start until its turn (a delayed BeginTime would show it first).
                slide.BeginAnimation(TranslateTransform.YProperty, Staggered(-7, 0, begin, ease));
                item.BeginAnimation(UIElement.OpacityProperty, Staggered(0, 1, begin, ease));
            }
        });
    }

    private static DoubleAnimationUsingKeyFrames Staggered(double from, double to, double beginMs, IEasingFunction ease)
    {
        var anim = new DoubleAnimationUsingKeyFrames();
        anim.KeyFrames.Add(new DiscreteDoubleKeyFrame(from, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        anim.KeyFrames.Add(new DiscreteDoubleKeyFrame(from, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(beginMs))));
        anim.KeyFrames.Add(new EasingDoubleKeyFrame(to, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(beginMs + ItemMs)), ease));
        return anim;
    }

    // While open, the menu follows its button through every scroll of the page.
    private static readonly DependencyProperty ScrollHandlerProperty = DependencyProperty.RegisterAttached(
        "ScrollHandler", typeof(ScrollChangedEventHandler), typeof(ComboBoxMotion), new PropertyMetadata(null));

    private static void Follow(ComboBox box, bool on)
    {
        var scroller = FindAncestor<ScrollViewer>(box);
        if (scroller is null) return;
        if (box.GetValue(ScrollHandlerProperty) is ScrollChangedEventHandler old)
        {
            scroller.ScrollChanged -= old;
            box.ClearValue(ScrollHandlerProperty);
        }
        if (!on) return;
        ScrollChangedEventHandler handler = (_, e) =>
        {
            if (e.VerticalChange == 0 && e.HorizontalChange == 0) return;
            if (!IsVisibleIn(box, scroller))
            {
                box.IsDropDownOpen = false;
                return;
            }
            if (box.Template?.FindName("PART_Popup", box) is Popup popup)
            {
                // Nudging an offset is how a popup is told to place itself again.
                double x = popup.HorizontalOffset;
                popup.HorizontalOffset = x + 0.01;
                popup.HorizontalOffset = x;
            }
        };
        scroller.ScrollChanged += handler;
        box.SetValue(ScrollHandlerProperty, handler);
    }

    private static bool IsVisibleIn(FrameworkElement element, ScrollViewer scroller)
    {
        if (!element.IsVisible) return false;
        Rect bounds = element.TransformToAncestor(scroller).TransformBounds(new Rect(element.RenderSize));
        return bounds.Bottom > 0 && bounds.Top < scroller.ViewportHeight;
    }

    private static T? FindAncestor<T>(DependencyObject d) where T : DependencyObject
    {
        for (d = VisualTreeHelper.GetParent(d); d is not null; d = VisualTreeHelper.GetParent(d))
        {
            if (d is T found) return found;
        }
        return null;
    }
}
