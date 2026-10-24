// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Shell;

namespace MacWin.Settings.Controls;

/// <summary>
/// The red / yellow / green window buttons. Symbols appear while the pointer is over the
/// group; the buttons turn grey while the window is inactive. Minimize goes through the
/// normal SC_MINIMIZE path, so MacWin's own genie animates this window too.
/// </summary>
public sealed class TrafficLights : StackPanel
{
    private readonly List<(Ellipse dot, Shape glyph, Color fill, Color stroke)> _buttons = new();
    private Window? _window;

    public TrafficLights()
    {
        Orientation = Orientation.Horizontal;
        WindowChrome.SetIsHitTestVisibleInChrome(this, true);
        Background = Brushes.Transparent;
        Add("#FFFF5F57", "#FFE2463F", CloseGlyph(), w => w.Close(), Loc.T("settings.window.close"));
        Add("#FFFEBC2E", "#FFDFA023", MinimizeGlyph(), w => SystemCommands.MinimizeWindow(w), Loc.T("settings.window.minimize"));
        Add("#FF28C840", "#FF1DAD2C", ZoomGlyph(), w =>
        {
            if (w.WindowState == WindowState.Maximized) SystemCommands.RestoreWindow(w);
            else SystemCommands.MaximizeWindow(w);
        }, Loc.T("settings.window.zoom"));
        MouseEnter += (_, _) => Refresh();
        MouseLeave += (_, _) => Refresh();
        Loaded += (_, _) =>
        {
            _window = Window.GetWindow(this);
            if (_window is null) return;
            _window.Activated += (_, _) => Refresh();
            _window.Deactivated += (_, _) => Refresh();
            Refresh();
        };
        Theme.Changed += Refresh;
    }

    private void Add(string fill, string stroke, Shape glyph, Action<Window> click, string tip)
    {
        var fillColor = (Color)ColorConverter.ConvertFromString(fill);
        var strokeColor = (Color)ColorConverter.ConvertFromString(stroke);
        var dot = new Ellipse { Width = 12, Height = 12, StrokeThickness = 0.5 };
        glyph.Opacity = 0;
        var grid = new Grid { Width = 12, Height = 12, Margin = new Thickness(0, 0, 8, 0), Cursor = Cursors.Arrow, ToolTip = tip };
        grid.Children.Add(dot);
        grid.Children.Add(glyph);
        grid.Background = Brushes.Transparent;
        grid.MouseLeftButtonDown += (_, e) => e.Handled = true;
        grid.MouseLeftButtonUp += (_, e) =>
        {
            e.Handled = true;
            if (Window.GetWindow(this) is Window w) click(w);
        };
        Children.Add(grid);
        _buttons.Add((dot, glyph, fillColor, strokeColor));
    }

    private void Refresh()
    {
        bool active = _window?.IsActive ?? true;
        bool hover = IsMouseOver;
        foreach (var (dot, glyph, fill, stroke) in _buttons)
        {
            bool colored = active || hover;
            dot.Fill = new SolidColorBrush(colored ? fill : Theme.Color("TrafficInactive"));
            dot.Stroke = new SolidColorBrush(colored ? stroke : Theme.Color("TrafficInactive"));
            glyph.Opacity = hover ? 1 : 0;
        }
    }

    private static Shape CloseGlyph() => new Path
    {
        Data = Geometry.Parse("M3.6,3.6 L8.4,8.4 M8.4,3.6 L3.6,8.4"),
        Stroke = new SolidColorBrush(Color.FromArgb(0xCC, 0x4D, 0x00, 0x00)),
        StrokeThickness = 1.25,
        StrokeStartLineCap = PenLineCap.Round,
        StrokeEndLineCap = PenLineCap.Round,
    };

    private static Shape MinimizeGlyph() => new Path
    {
        Data = Geometry.Parse("M3.2,6 L8.8,6"),
        Stroke = new SolidColorBrush(Color.FromArgb(0xCC, 0x99, 0x57, 0x00)),
        StrokeThickness = 1.4,
        StrokeStartLineCap = PenLineCap.Round,
        StrokeEndLineCap = PenLineCap.Round,
    };

    private static Shape ZoomGlyph() => new Path
    {
        Data = Geometry.Parse("M3.6,8.4 L3.6,4.9 L7.1,8.4 Z M8.4,3.6 L8.4,7.1 L4.9,3.6 Z"),
        Fill = new SolidColorBrush(Color.FromArgb(0xCC, 0x00, 0x65, 0x00)),
    };
}
