namespace MacWin.Settings;

/// <summary>
/// The same curves the MacWin renderer runs on the GPU (src/host/render/genie_effect.cpp),
/// used by the live preview. Modern = Potassiumuncher's lamp curve, classic = the original
/// funnel of the "MacOS Minimize Animation" mod.
/// </summary>
internal static class GenieMath
{
    public readonly record struct Rect(double X, double Y, double W, double H);

    private const double Spread = 0.65;

    public static double Ease(int style, double raw)
    {
        raw = Math.Clamp(raw, 0, 1);
        return style == 1 ? raw : raw * raw * (3 - 2 * raw);
    }

    public static double Fade(int style, double t) =>
        style != 1 || t <= 0.8 ? 1 : Math.Clamp((1 - t) / 0.2, 0, 1);

    /// <summary>Modern: icon = taskbar icon rect. Classic: icon.X = dock x, icon.Y = dock y,
    /// icon.W = neck width.</summary>
    public static (double X, double Y) Vertex(int style, double tx, double ty, double p, Rect w, Rect i) =>
        style == 1 ? Classic(tx, ty, p, w, i) : Lamp(tx, ty, p, w, i);

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

    private static (double, double) Classic(double tx, double ty, double tt, Rect w, Rect d)
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
