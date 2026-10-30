// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace MacWin.Settings.Controls;

/// <summary>
/// The first time the settings open after a new install: three looks to start from (all of
/// it can be changed later) and, if wanted, a clock and a calendar on the desktop.
/// </summary>
public partial class WelcomeSheet : UserControl
{
    /// <summary>The sheet went away (the choice is in the model by then).</summary>
    public event Action? Finished;

    public bool IsOpen { get; private set; }

    public WelcomeSheet()
    {
        InitializeComponent();
    }

    public void Show()
    {
        IsOpen = true;
        Visibility = Visibility.Visible;
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        Dim.BeginAnimation(OpacityProperty, new DoubleAnimation(0, Theme.IsDark ? 0.38 : 0.18, TimeSpan.FromMilliseconds(240)));
        Sheet.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(180)));
        SheetShift.BeginAnimation(TranslateTransform.YProperty,
                                  new DoubleAnimation(-30, 0, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease });
        var grow = new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease };
        SheetScale.BeginAnimation(ScaleTransform.ScaleXProperty, grow);
        SheetScale.BeginAnimation(ScaleTransform.ScaleYProperty, grow);
        Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Input, () => StartButton.Focus());
    }

    private void Close()
    {
        if (!IsOpen) return;
        IsOpen = false;
        if (DataContext is SettingsModel model) ApplyChoice(model);
        var ease = new CubicEase { EasingMode = EasingMode.EaseIn };
        var duration = TimeSpan.FromMilliseconds(200);
        var fade = new DoubleAnimation(0, duration);
        fade.Completed += (_, _) =>
        {
            if (!IsOpen) Visibility = Visibility.Collapsed;
        };
        Dim.BeginAnimation(OpacityProperty, new DoubleAnimation(0, duration));
        Sheet.BeginAnimation(OpacityProperty, fade);
        SheetShift.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-20, duration) { EasingFunction = ease });
        var shrink = new DoubleAnimation(0.97, duration) { EasingFunction = ease };
        SheetScale.BeginAnimation(ScaleTransform.ScaleXProperty, shrink);
        SheetScale.BeginAnimation(ScaleTransform.ScaleYProperty, shrink);
        Finished?.Invoke();
    }

    private void ApplyChoice(SettingsModel m)
    {
        if (MacChoice.IsChecked == true)
        {
            // The genie everywhere, squircle corners, Exposé in the top-left corner and
            // the desktop in the bottom-right one.
            m.Style = 1;
            m.OpenEffect = m.CloseEffect = 1;
            m.CornerShape = 1;
            m.WindowCorners = true;
            m.Corners = [1, 0, 0, 2];
            m.OverviewHotkey = true;
        }
        else if (CalmChoice.IsChecked == true)
        {
            // Short, soft movements and Windows 11's own corners.
            m.Style = 2;
            m.OpenEffect = m.CloseEffect = 4;
            m.DurationMs = 300;
            m.OpenDurationMs = 300;
            m.CloseDurationMs = 250;
            m.CornerShape = 0;
            m.OverviewHotkey = true;
        }
        if (AddWidgets.IsChecked == true && m.Widgets.Count == 0)
        {
            m.Widgets.Add(new WidgetEntry { Id = WidgetEntry.NewId(), Kind = "clock" });
            m.Widgets.Add(new WidgetEntry { Id = WidgetEntry.NewId(), Kind = "calendar" });
        }
    }

    private void Sheet_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!IsOpen || e.Key is not (Key.Escape or Key.Enter)) return;
        // Escape keeps the settings as they are.
        if (e.Key == Key.Escape)
        {
            KeepChoice.IsChecked = true;
            AddWidgets.IsChecked = false;
        }
        Close();
        e.Handled = true;
    }

    private void Start_Click(object sender, RoutedEventArgs e) => Close();
}
