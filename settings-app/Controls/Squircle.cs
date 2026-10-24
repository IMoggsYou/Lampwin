// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Media;

namespace MacWin.Settings.Controls;

/// <summary>
/// Rounded rectangle with Figma-style corner smoothing (the construction of the
/// figma-squircle library: straight edge, cubic Bézier, circular arc, cubic Bézier).
/// Smoothing 0 is a plain rounded rectangle; 1 (100 %) leaves no arc at all - the macOS
/// squircle. The renderer draws the same shape (CornerShape in device.cpp).
/// </summary>
public static class Squircle
{
    public static Geometry Create(double width, double height, double radius, double smoothing)
    {
        double budget = Math.Min(width, height) / 2;
        double r = Math.Clamp(radius, 0, budget);
        if (r <= 0) return new RectangleGeometry(new Rect(0, 0, width, height));

        double s = Math.Clamp(smoothing, 0, 1);
        double p = (1 + s) * r;
        s = Math.Min(s, budget / r - 1);   // no room for the full smoothing: less of it
        p = Math.Min(p, budget);

        double arcMeasure = 90 * (1 - s);
        double arc = Math.Sin(Rad(arcMeasure / 2)) * r * Math.Sqrt(2);
        double alpha = (90 - arcMeasure) / 2;
        double p3ToP4 = r * Math.Tan(Rad(alpha / 2));
        double beta = 45 * s;
        double c = p3ToP4 * Math.Cos(Rad(beta));
        double d = c * Math.Tan(Rad(beta));
        double b = (p - arc - c - d) / 3;
        double a = 2 * b;

        var g = new StreamGeometry();
        using (var ctx = g.Open())
        {
            var at = new Point(width - p, 0);
            ctx.BeginFigure(at, true, true);
            // Each corner in its own frame: u along the edge it leaves, v into the next one.
            void Corner(Vector u, Vector v)
            {
                Point P(double du, double dv) => at + u * du + v * dv;
                ctx.BezierTo(P(a, 0), P(a + b, 0), P(a + b + c, d), true, true);
                at = P(a + b + c, d);
                if (arc > 1e-6)
                {
                    Point end = P(arc, arc);
                    ctx.ArcTo(end, new Size(r, r), 0, false, SweepDirection.Clockwise, true, true);
                    at = end;
                }
                ctx.BezierTo(P(d, c), P(d, b + c), P(d, a + b + c), true, true);
                at = P(d, a + b + c);
            }
            Corner(new Vector(1, 0), new Vector(0, 1));     // top right
            at = new Point(width, height - p);
            ctx.LineTo(at, true, true);
            Corner(new Vector(0, 1), new Vector(-1, 0));    // bottom right
            at = new Point(p, height);
            ctx.LineTo(at, true, true);
            Corner(new Vector(-1, 0), new Vector(0, -1));   // bottom left
            at = new Point(0, p);
            ctx.LineTo(at, true, true);
            Corner(new Vector(0, -1), new Vector(1, 0));    // top left
        }
        g.Freeze();
        return g;
    }

    private static double Rad(double degrees) => degrees * Math.PI / 180;
}
