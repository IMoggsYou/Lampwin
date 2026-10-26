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

    // ---- Shatter (closing only) ------------------------------------------------------------
    // The same shards as ShatterVS in src/host/render/genie.hlsl (the same hash, grid and
    // motion), drawn by the live preview.

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

    /// <summary>Shard `tri` at progress t (0..1): its three corners on screen and in the
    /// window's texture, and its opacity. `big`: fragments (each its own way).</summary>
    public static double ShatterTriangle(int tri, int cols, int rows, uint seed, double t, Rect w, bool big,
                                         Span<(double X, double Y)> pos, Span<(double U, double V)> uv)
    {
        int cell = tri / 2;
        int cx = cell % cols, cy = cell / cols;
        bool flip = Rand((uint)cell, 3, seed) > 0.5;
        int[] k = tri % 2 == 0 ? (flip ? [0, 1, 3] : [0, 1, 2]) : (flip ? [0, 3, 2] : [1, 3, 2]);
        for (int i = 0; i < 3; i++)
            uv[i] = ShatterPoint(cx + (k[i] & 1), cy + (k[i] >> 1), cols, rows, seed);
        double mu = (uv[0].U + uv[1].U + uv[2].U) / 3, mv = (uv[0].V + uv[1].V + uv[2].V) / 3;

        double rx = (mu - 0.5) * w.W, ry = (mv - 0.5) * w.H;
        double reach = Math.Clamp(Math.Sqrt((mu - 0.5) * (mu - 0.5) + (mv - 0.5) * (mv - 0.5)) * Math.Sqrt(2), 0, 1);
        double angle = Math.Atan2(ry, rx);
        double axis = Math.Round(angle / (Math.PI / 2), MidpointRounding.AwayFromZero) * (Math.PI / 2);
        angle = big ? angle + (Rand((uint)tri, 4, seed) - 0.5) * 1.1
                    : angle + (axis - angle) * 0.7 + (Rand((uint)tri, 4, seed) - 0.5) * 0.35;

        double delay = big ? 0.05 + 0.06 * (1 - reach) + 0.08 * Rand((uint)tri, 5, seed)
                           : 0.05 + 0.1 * (1 - reach) + 0.05 * Rand((uint)tri, 5, seed);
        double crack = Smoothstep(0, delay, t);
        double u = Math.Clamp((t - delay) / (1 - delay), 0, 1);
        double move = 1 - Math.Pow(1 - u, 2.6);
        double diag = Math.Sqrt(w.W * w.W + w.H * w.H);
        double fly = big ? (0.2 + 0.24 * reach) * (0.7 + 0.6 * Rand((uint)tri, 6, seed))
                         : (0.12 + 0.22 * reach) * (0.75 + 0.5 * Rand((uint)tri, 6, seed));
        double dist = diag * (0.012 * crack + fly * move);
        double spin = (Rand((uint)tri, 7, seed) * 2 - 1) * (0.03 * crack + (big ? 0.8 : 1.5) * move);
        double shrink = 1 - (big ? 0.25 : 0.4) * move;
        double sn = Math.Sin(spin), cs = Math.Cos(spin);
        double ox = w.X + mu * w.W + Math.Cos(angle) * dist, oy = w.Y + mv * w.H + Math.Sin(angle) * dist;
        for (int i = 0; i < 3; i++)
        {
            double vx = (uv[i].U - mu) * w.W * shrink, vy = (uv[i].V - mv) * w.H * shrink;
            pos[i] = (ox + vx * cs - vy * sn, oy + vx * sn + vy * cs);
        }
        return 1 - Smoothstep(0.35, 1, u);
    }
}
