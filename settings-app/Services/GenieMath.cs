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

    // ---- Shatter and fragments ------------------------------------------------------------
    // The same pieces as ShatterVS in src/host/render/genie.hlsl (the same hash, grid,
    // motion and light), drawn by the live preview.

    /// <summary>Pieces take this much longer than the duration set (kPiecesPace in the host).</summary>
    public const double PiecesPace = 1.35;

    // ---- Zoom (as in genie_effect.cpp) ------------------------------------------------------

    /// <summary>The window at 8 % of its size (kZoomEndScale), centred on the point.</summary>
    public static Rect ZoomEnd(Rect w, double cx, double cy) =>
        new(cx - w.W * 0.04, cy - w.H * 0.04, w.W * 0.08, w.H * 0.08);

    /// <summary>Time -> progress: closing eases in and out; opening goes out fast, a touch
    /// past the window's size, and settles (easeOutBack).</summary>
    public static double ZoomEase(bool opening, double raw)
    {
        raw = Math.Clamp(raw, 0, 1);
        if (!opening) return Ease(Modern, raw);
        const double c = 0.75;
        double x = raw - 1;
        return 1 + (c + 1) * x * x * x + c * x * x;
    }

    public static double ZoomFade(double t) => 1 - Smoothstep(0.65, 1, t);

    /// <summary>The crack grid (ShatterGrid in the host): 9 cells along the longer side for
    /// shatter, 5 for fragments, never fewer than 3 along the other.</summary>
    public static (int Cols, int Rows) ShatterGrid(double w, double h, bool fragments)
    {
        int cells = fragments ? 5 : 9;
        double aspect = h > 0 ? w / h : 1;
        int cols = aspect >= 1 ? cells : (int)Math.Round(cells * aspect, MidpointRounding.AwayFromZero);
        int rows = aspect >= 1 ? (int)Math.Round(cells / aspect, MidpointRounding.AwayFromZero) : cells;
        return (Math.Max(cols, 3), Math.Max(rows, 3));
    }

    private static uint Hash(uint x)
    {
        unchecked
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }
    }

    private static double Rand(uint item, uint salt, uint seed) =>
        (Hash(unchecked(item * 0x9E3779B1u + salt * 0x85EBCA77u + seed)) & 0xFFFFFFu) / 16777216.0;

    private static (double U, double V) ShatterPoint(int px, int py, int cols, int rows, uint seed)
    {
        uint id = (uint)(py * (cols + 1) + px);
        double jx = px == 0 || px == cols ? 0 : Rand(id, 1, seed) - 0.5;
        double jy = py == 0 || py == rows ? 0 : Rand(id, 2, seed) - 0.5;
        return ((px + jx * 0.72) / cols, (py + jy * 0.72) / rows);
    }

    /// <summary>How a piece looks at a moment: opacity, light (1 = as at rest), the glint as
    /// it faces the light, and the glow of its edges while the glass cracks.</summary>
    public readonly record struct ShardLook(double Alpha, double Light, double Glint, double Glow);

    private static (double X, double Y, double Z) Turn((double X, double Y, double Z) v, (double X, double Y, double Z) a, double angle)
    {
        double s = Math.Sin(angle), c = Math.Cos(angle);
        double dot = a.X * v.X + a.Y * v.Y + a.Z * v.Z;
        (double X, double Y, double Z) cross = (a.Y * v.Z - a.Z * v.Y, a.Z * v.X - a.X * v.Z, a.X * v.Y - a.Y * v.X);
        return (v.X * c + cross.X * s + a.X * dot * (1 - c),
                v.Y * c + cross.Y * s + a.Y * dot * (1 - c),
                v.Z * c + cross.Z * s + a.Z * dot * (1 - c));
    }

    /// <summary>Piece `tri` at progress t (0..1): its three corners on screen and in the
    /// window's texture, and how it looks. `big`: fragments (each its own way). `impact`:
    /// where the glass breaks first, in window uv.</summary>
    public static ShardLook ShatterTriangle(int tri, int cols, int rows, uint seed, double t, Rect w, bool big,
                                           (double U, double V) impact,
                                           Span<(double X, double Y)> pos, Span<(double U, double V)> uv)
    {
        int cell = tri / 2;
        int cx = cell % cols, cy = cell / cols;
        bool flip = Rand((uint)cell, 3, seed) > 0.5;
        int[] k = tri % 2 == 0 ? (flip ? [0, 1, 3] : [0, 1, 2]) : (flip ? [0, 3, 2] : [1, 3, 2]);
        for (int i = 0; i < 3; i++)
            uv[i] = ShatterPoint(cx + (k[i] & 1), cy + (k[i] >> 1), cols, rows, seed);
        double mu = (uv[0].U + uv[1].U + uv[2].U) / 3, mv = (uv[0].V + uv[1].V + uv[2].V) / 3;
        double R(uint salt) => Rand((uint)tri, salt, seed);

        double rx = (mu - impact.U) * w.W, ry = (mv - impact.V) * w.H;
        double fx = Math.Max(impact.U, 1 - impact.U) * w.W, fy = Math.Max(impact.V, 1 - impact.V) * w.H;
        double reach = Math.Clamp(Math.Sqrt(rx * rx + ry * ry) / Math.Max(Math.Sqrt(fx * fx + fy * fy), 1), 0, 1);
        double angle = Math.Atan2(ry, rx);
        double axis = Math.Round(angle / (Math.PI / 2), MidpointRounding.AwayFromZero) * (Math.PI / 2);
        angle = big ? angle + (R(4) - 0.5) * 1.1 : angle + (axis - angle) * 0.7 + (R(4) - 0.5) * 0.35;

        double delay = 0.04 + (big ? 0.1 : 0.13) * reach + 0.05 * R(5);
        double crack = Smoothstep(0, delay, t);
        double u = Math.Clamp((t - delay) / (1 - delay), 0, 1);
        double move = 1 - Math.Pow(1 - u, 2.4);
        double diag = Math.Sqrt(w.W * w.W + w.H * w.H);
        double fly = ((big ? 0.24 : 0.17) + 0.12 * (1 - reach)) * (0.7 + 0.6 * R(6));
        double dist = diag * (0.01 * crack + fly * move);

        var ax = (X: R(9) - 0.5, Y: R(10) - 0.5, Z: 0.35 * (R(11) - 0.5) + 1e-4);
        double len = Math.Sqrt(ax.X * ax.X + ax.Y * ax.Y + ax.Z * ax.Z);
        ax = (ax.X / len, ax.Y / len, ax.Z / len);
        double tumble = (R(7) * 2 - 1) * (0.02 * crack + (big ? 2.4 : 3.4) * move);
        double shrink = 1 - (big ? 0.15 : 0.3) * move;
        var normal = Turn((0, 0, 1), ax, tumble);

        double focal = 1.6 * diag;
        double depth0 = (R(12) * 2 - 1) * diag * (big ? 0.35 : 0.25) * move;
        double fall = diag * (big ? 0.2 : 0.14) * u * u;
        double midX = w.X + 0.5 * w.W, midY = w.Y + 0.5 * w.H;
        double px = w.X + mu * w.W + Math.Cos(angle) * dist, py = w.Y + mv * w.H + Math.Sin(angle) * dist + fall;
        for (int i = 0; i < 3; i++)
        {
            var v = Turn(((uv[i].U - mu) * w.W * shrink, (uv[i].V - mv) * w.H * shrink, 0), ax, tumble);
            double persp = focal / Math.Max(focal - (depth0 + v.Z), focal * 0.3);
            pos[i] = (midX + (px + v.X - midX) * persp, midY + (py + v.Y - midY) * persp);
        }

        var light = (X: -0.35, Y: -0.55, Z: 0.76);
        double ll = Math.Sqrt(light.X * light.X + light.Y * light.Y + light.Z * light.Z);
        light = (light.X / ll, light.Y / ll, light.Z / ll);
        var half = (X: light.X, Y: light.Y, Z: light.Z + 1);
        double hl = Math.Sqrt(half.X * half.X + half.Y * half.Y + half.Z * half.Z);
        double nl = Math.Abs(normal.X * light.X + normal.Y * light.Y + normal.Z * light.Z);
        double nh = Math.Abs(normal.X * half.X + normal.Y * half.Y + normal.Z * half.Z) / hl;
        return new ShardLook(1 - Smoothstep(0.3, 1, u),
                             (0.7 + 0.3 * nl) / (0.7 + 0.3 * light.Z),
                             Math.Pow(Math.Clamp(nh, 0, 1), 48) * 0.5 * Smoothstep(0, 0.25, u) * (1 - u),
                             crack * (1 - Smoothstep(0, 0.4, u)));
    }
}
