// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace MacWin.Settings.Controls;

/// <summary>
/// A small picture of a desktop widget (the gallery, the list of widgets): the same card,
/// corner and colors as the real one (src/host/widgets/widget_draw.cpp), drawn as vectors
/// in a 100 x 100 square scaled to the control's size.
/// </summary>
public sealed class WidgetPreview : FrameworkElement
{
    public static readonly DependencyProperty KindProperty = DependencyProperty.Register(
        nameof(Kind), typeof(string), typeof(WidgetPreview),
        new FrameworkPropertyMetadata("clock", FrameworkPropertyMetadataOptions.AffectsRender));

    /// <summary>The host's name of the kind ("clock", "weather", ...).</summary>
    public string Kind
    {
        get => (string)GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    public static readonly DependencyProperty LookProperty = DependencyProperty.Register(
        nameof(Look), typeof(string), typeof(WidgetPreview),
        new FrameworkPropertyMetadata("auto", FrameworkPropertyMetadataOptions.AffectsRender));

    /// <summary>"auto" (this window's light or dark), "light" or "dark": the widget's own look.</summary>
    public string Look
    {
        get => (string)GetValue(LookProperty);
        set => SetValue(LookProperty, value);
    }

    public WidgetPreview()
    {
        Loaded += (_, _) => Theme.Changed += InvalidateVisual;
        Unloaded += (_, _) => Theme.Changed -= InvalidateVisual;
    }

    private static readonly Color Orange = Color.FromRgb(0xFF, 0x95, 0x00);
    private static readonly Color Yellow = Color.FromRgb(0xFF, 0xD6, 0x0A);

    private sealed record Palette(Color Top, Color Bottom, Color Text, Color Secondary, Color Edge, Color Track,
                                  Color Face, Color Red);

    private bool IsDark => Look == "dark" || (Look != "light" && Theme.IsDark);

    private Palette Colors => IsDark
        ? new(Hex(0x2C2C2F), Hex(0x1C1C1E), Hex(0xF5F5F7), Hex(0x98989D), Color.FromArgb(0x1A, 255, 255, 255),
              Hex(0x3A3A3D), Hex(0x0B0B0C), Hex(0xFF453A))
        : new(Hex(0xFFFFFF), Hex(0xF5F5F7), Hex(0x1D1D1F), Hex(0x86868B), Color.FromArgb(0x14, 0, 0, 0),
              Hex(0xE8E8ED), Hex(0xF2F2F5), Hex(0xFF3B30));

    protected override void OnRender(DrawingContext dc)
    {
        double size = Math.Min(ActualWidth, ActualHeight);
        if (size <= 0) return;
        dc.PushTransform(new ScaleTransform(size / 100, size / 100));
        var card = Squircle.Create(100, 100, 22, 0.7);
        var p = Colors;
        switch (Kind)
        {
            case "weather":
                dc.DrawGeometry(Vertical(Hex(0x005AFF), Hex(0x5D95FB)), null, card);
                Weather(dc);
                break;
            case "photo":
                dc.PushClip(card);
                Photo(dc);
                dc.Pop();
                break;
            default:
                dc.DrawGeometry(Vertical(p.Top, p.Bottom), null, card);
                if (Kind == "clock") Clock(dc, p);
                else if (Kind == "calendar") Calendar(dc, p);
                else if (Kind == "system") Gauges(dc, p);
                else Notes(dc, p);
                break;
        }
        dc.DrawGeometry(null, new Pen(new SolidColorBrush(p.Edge), 1.2), card);
        dc.Pop();
    }

    private static void Clock(DrawingContext dc, Palette p)
    {
        var c = new Point(50, 50);
        const double r = 39;
        dc.DrawEllipse(new SolidColorBrush(p.Face), null, c, r, r);
        for (int i = 0; i < 12; i++)
        {
            double a = i * Math.PI / 6;
            var dir = new Vector(Math.Sin(a), -Math.Cos(a));
            dc.DrawLine(RoundPen(p.Secondary, i % 3 == 0 ? 2.2 : 1.2), c + dir * (r - 3), c + dir * (r - (i % 3 == 0 ? 9 : 6)));
        }
        void Hand(double angle, double length, double width, Color color, double tail = 0)
        {
            var dir = new Vector(Math.Sin(angle), -Math.Cos(angle));
            dc.DrawLine(RoundPen(color, width), c - dir * tail, c + dir * length);
        }
        Hand((10 + 9 / 60.0) * Math.PI / 6, r * 0.52, 4.6, p.Text);
        Hand(9 * Math.PI / 30, r * 0.8, 3.6, p.Text);
        Hand(32 * Math.PI / 30, r * 0.86, 1.3, Orange, r * 0.16);
        dc.DrawEllipse(new SolidColorBrush(Orange), null, c, 2.6, 2.6);
        dc.DrawEllipse(new SolidColorBrush(p.Face), null, c, 1, 1);
    }

    private void Calendar(DrawingContext dc, Palette p)
    {
        var culture = DateCulture();
        var today = DateTime.Today;
        double pixels = VisualTreeHelper.GetDpi(this).PixelsPerDip;
        Text(dc, culture.DateTimeFormat.GetDayName(today.DayOfWeek).ToUpper(culture), 10.5, FontWeights.SemiBold, p.Red,
             new Point(12, 10), pixels, 78);
        Text(dc, today.Day.ToString(CultureInfo.InvariantCulture), 46, FontWeights.Light, p.Text, new Point(9, 20), pixels,
             display: true);
        string month = culture.DateTimeFormat.GetMonthName(today.Month);
        if (month.Length > 0) month = char.ToUpper(month[0], culture) + month[1..];
        Text(dc, month, 10.5, FontWeights.SemiBold, p.Secondary, new Point(12, 76), pixels, 78);
    }

    private void Weather(DrawingContext dc)
    {
        var white = new SolidColorBrush(System.Windows.Media.Colors.White);
        var faint = new SolidColorBrush(Color.FromArgb(0xB0, 255, 255, 255));
        // The sun, a cloud in front of it, and lines standing in for the text.
        var sun = new SolidColorBrush(Yellow);
        dc.DrawEllipse(sun, null, new Point(70, 30), 9, 9);
        for (int i = 0; i < 8; i++)
        {
            double a = i * Math.PI / 4;
            var dir = new Vector(Math.Cos(a), Math.Sin(a));
            dc.DrawLine(RoundPen(Yellow, 3), new Point(70, 30) + dir * 13, new Point(70, 30) + dir * 17);
        }
        var cloud = new GeometryGroup { FillRule = FillRule.Nonzero };
        cloud.Children.Add(new RectangleGeometry(new Rect(46, 38, 44, 16), 8, 8));
        cloud.Children.Add(new EllipseGeometry(new Point(58, 42), 9, 9));
        cloud.Children.Add(new EllipseGeometry(new Point(71, 38), 12, 12));
        dc.DrawGeometry(white, null, cloud);
        dc.DrawRoundedRectangle(white, null, new Rect(12, 12, 30, 7), 3.5, 3.5);
        dc.DrawRoundedRectangle(faint, null, new Rect(12, 68, 40, 6), 3, 3);
        dc.DrawRoundedRectangle(faint, null, new Rect(12, 79, 28, 6), 3, 3);
        Text(dc, "22°", 30, FontWeights.Light, System.Windows.Media.Colors.White, new Point(9, 22),
             VisualTreeHelper.GetDpi(this).PixelsPerDip, display: true);
    }

    private static void Gauges(DrawingContext dc, Palette p)
    {
        (double X, double Y, double Value, uint Color)[] rings =
        [
            (30, 30, 0.37, 0x0A84FF), (70, 30, 0.62, 0xBF5AF2), (30, 70, 0.48, 0xFF9F0A), (70, 70, 0.81, 0x30D158),
        ];
        foreach (var (x, y, value, color) in rings)
        {
            const double r = 12.5;
            var c = new Point(x, y);
            dc.DrawEllipse(null, new Pen(new SolidColorBrush(p.Track), 4.4), c, r, r);
            double a = value * 2 * Math.PI;
            var arc = new StreamGeometry();
            using (var ctx = arc.Open())
            {
                ctx.BeginFigure(new Point(x, y - r), false, false);
                ctx.ArcTo(new Point(x + r * Math.Sin(a), y - r * Math.Cos(a)), new Size(r, r), 0, value > 0.5,
                          SweepDirection.Clockwise, true, false);
            }
            dc.DrawGeometry(null, RoundPen(Hex(color), 4.4), arc);
        }
    }

    private static void Notes(DrawingContext dc, Palette p)
    {
        dc.DrawRoundedRectangle(new SolidColorBrush(Yellow), null, new Rect(12, 12, 17, 17), 4.5, 4.5);
        var white = RoundPen(System.Windows.Media.Colors.White, 1.4);
        for (int i = 0; i < 3; i++) dc.DrawLine(white, new Point(16.5, 17.5 + i * 3.2), new Point(i == 2 ? 21 : 24.5, 17.5 + i * 3.2));
        dc.DrawRoundedRectangle(new SolidColorBrush(p.Text), null, new Rect(12, 39, 50, 7), 3.5, 3.5);
        var line = new SolidColorBrush(Color.FromArgb(0x70, p.Secondary.R, p.Secondary.G, p.Secondary.B));
        double[] widths = [74, 62, 70, 44];
        for (int i = 0; i < widths.Length; i++) dc.DrawRoundedRectangle(line, null, new Rect(12, 53 + i * 10, widths[i], 5), 2.5, 2.5);
    }

    private static void Photo(DrawingContext dc)
    {
        dc.DrawRectangle(Vertical(Hex(0x6FA8F0), Hex(0xF6C79A)), null, new Rect(0, 0, 100, 100));
        dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(0xFF, 0xF1, 0xC9)), null, new Point(68, 40), 10, 10);
        dc.DrawGeometry(new SolidColorBrush(Hex(0x5B7FB5)), null, Geometry.Parse("M0,72 L28,46 L48,64 L70,42 L100,70 V100 H0 Z"));
        dc.DrawGeometry(new SolidColorBrush(Hex(0x34507E)), null, Geometry.Parse("M0,84 L22,70 L46,86 L72,66 L100,86 V100 H0 Z"));
    }

    // Dates as the host writes them (Arabic as Egypt does: Gregorian months).
    private static CultureInfo DateCulture()
    {
        string name = Loc.Code switch
        {
            "zh-Hans" => "zh-CN",
            "ar" => "ar-EG",
            "bn" => "bn-BD",
            _ => Loc.Code,
        };
        try
        {
            return CultureInfo.GetCultureInfo(name);
        }
        catch (CultureNotFoundException)
        {
            return CultureInfo.InvariantCulture;
        }
    }

    private static void Text(DrawingContext dc, string text, double size, FontWeight weight, Color color, Point at,
                             double pixelsPerDip, double maxWidth = 0, bool display = false)
    {
        var family = Application.Current?.TryFindResource(display ? "Font.Display" : "Font.Text") as FontFamily
                     ?? new FontFamily("Segoe UI");
        var formatted = new FormattedText(text, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight,
                                          new Typeface(family, FontStyles.Normal, weight, FontStretches.Normal), size,
                                          new SolidColorBrush(color), pixelsPerDip);
        if (maxWidth > 0)
        {
            formatted.MaxTextWidth = maxWidth;
            formatted.MaxLineCount = 1;
            formatted.Trimming = TextTrimming.CharacterEllipsis;
        }
        dc.DrawText(formatted, at);
    }

    private static Pen RoundPen(Color color, double width) =>
        new(new SolidColorBrush(color), width) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round };

    private static LinearGradientBrush Vertical(Color top, Color bottom) => new(top, bottom, 90);

    private static Color Hex(uint rgb) => Color.FromRgb((byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
}
