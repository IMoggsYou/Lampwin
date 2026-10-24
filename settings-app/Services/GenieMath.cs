// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin
// Portions ported from the Windhawk mod "MacOS Minimize Animation" (MIT, Abdullah Masood
// and Potassiumuncher); see THIRD_PARTY_NOTICES.md.

namespace MacWin.Settings;

/// <summary>
/// The same curves the MacWin renderer runs on the GPU (src/host/render/genie_effect.cpp),
/// used by the live preview. Modern = Potassiumuncher's lamp curve, classic = the original
/// funnel of the "MacOS Minimize Animation" mod, scale = MacWin's scale effect.
/// </summary>
internal static class GenieMath
{
    public readonly record struct Rect(double X, double Y, double W, double H);

    public const int Modern = 0, Classic = 1, Scale = 2;

    private const double Spread = 0.65;

    public static double Ease(int style, double raw)
    {
        raw = Math.Clamp(raw, 0, 1);
        return style == Classic ? raw : raw * raw * raw * (raw * (raw * 6 - 15) + 10);
    }

    public static double Fade(int style, double t) => style switch
    {
        Classic => 1 - Smoothstep(0.8, 1, t),
        Scale => 1 - Smoothstep(0.6, 1, t),
        _ => 1 - Smoothstep(0.92, 1, t),
    };

    /// <summary>Scale: the rect a w-sized window ends in, its shape fitted into an icon of
    /// `size` centred at (cx, cy).</summary>
    public static Rect ScaleEnd(Rect w, double cx, double cy, double size)
    {
        double ew = w.W >= w.H ? size : size * w.W / w.H;
        double eh = w.W >= w.H ? size * w.H / w.W : size;
        return new Rect(cx - ew / 2, cy - eh / 2, ew, eh);
    }

    /// <summary>Modern: icon = taskbar icon rect. Classic: icon.X = dock x, icon.Y = dock y,
    /// icon.W = neck width. Scale: icon = the rect the window ends in (<see cref="ScaleEnd"/>).</summary>
    public static (double X, double Y) Vertex(int style, double tx, double ty, double p, Rect w, Rect i) => style switch
    {
        Classic => ClassicVertex(tx, ty, p, w, i),
        Scale => ScaleVertex(tx, ty, p, w, i),
        _ => Lamp(tx, ty, p, w, i),
    };

    private static double Smoothstep(double a, double b, double x)
    {
        double s = Math.Clamp((x - a) / (b - a), 0, 1);
        return s * s * (3 - 2 * s);
    }

    private static (double, double) ScaleVertex(double tx, double ty, double t, Rect w, Rect e)
    {
        double size = 1 - (1 - t) * (1 - t);
        double pos = t * t * (3 - 2 * t);
        double cx = w.X + w.W / 2 + (e.X + e.W / 2 - w.X - w.W / 2) * pos;
        double cy = w.Y + w.H / 2 + (e.Y + e.H / 2 - w.Y - w.H / 2) * pos;
        double width = w.W + (e.W - w.W) * size;
        double height = w.H + (e.H - w.H) * size;
        return (cx + (tx - 0.5) * width, cy + (ty - 0.5) * height);
    }

    private static (double, double) Lamp(double tx, double ty, double p, Rect w, Rect i)
    {
        const double split = 0.3;
        double k = p <= split ? p / split : 1;
        double j = p > split ? (p - split) / (1 - split) : 0;

        double expandHeight = i.Y - w.Y - w.H;
        double fullHeight = (i.Y - w.Y) - expandHeight * (1 - k);
        double height = fullHeight - j * fullHeight;
        double safeFull = Math.Abs(fullHeight) < 0.001 ? 0.001 : fullHeight;

        double y = ty * height;
        double x = tx * i.W + tx * (w.W - i.W) * (1 - j) * (1 - ty) + tx * (w.W - i.W) * (1 - k) * ty;
        double offsetX = (i.X - w.X) * (y / (fullHeight + 0.1)) * k + (i.X - w.X) * j;
        double offsetY = i.Y - w.Y - height - expandHeight * (1 - k);
        double effectX = Math.Sin((height - y) / safeFull * 2 * Math.PI + Math.PI) *
                         (w.X + w.W * tx - (i.X + i.W * tx)) / 7 * k;
        return (w.X + x + offsetX + effectX, w.Y + y + offsetY);
    }

    private static (double, double) ClassicVertex(double tx, double ty, double tt, Rect w, Rect d)
    {
        double m = Math.Clamp(tt * (1 + Spread) - (1 - ty) * Spread, 0, 1);
        double e = m * m * (3 - 2 * m);
        double idY = w.Y + w.H * ty;
        double y = idY + (d.Y - idY) * e;
        double width = w.W + (d.W - w.W) * e;
        double cx0 = w.X + w.W * 0.5;
        double cx = cx0 + (d.X - cx0) * e;
        return (cx + (tx - 0.5) * width, y);
    }
}
