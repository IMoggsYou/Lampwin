// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using MacWin.Settings.Controls;

namespace MacWin.Settings;

/// <summary>
/// --record-preview &lt;dir&gt; [--style modern|classic|scale]: renders the settings window's
/// live preview, frame by frame on its own clock, to frame0000.png, frame0001.png, ...
/// (30 per second, two genie loops). tools/media/make-gif.ps1 turns them into
/// docs/media/demo.gif.
/// </summary>
internal static class PreviewRecorder
{
    private const double Width = 960, Height = 420, Scale = 1.5, Fps = 30;
    private const double DurationMs = 600;   // a bit slower than the default, easier to follow
    // One loop: shown 1.2 s, minimize, hidden 0.75 s, restore. Two loops and a little rest.
    private const double LengthMs = 2 * (1200 + DurationMs + 750 + DurationMs) + 600;

    public static void Run(string dir, int style)
    {
        Directory.CreateDirectory(dir);
        var preview = new GeniePreview
        {
            Width = Width,
            Height = Height,
            ManualClock = true,
            DurationMs = DurationMs,
            GenieStyle = style,
            Tiles = 32,
            CornerRadiusPx = 15,
            CornerSmoothing = 100,
        };
        preview.Measure(new Size(Width, Height));
        preview.Arrange(new Rect(0, 0, Width, Height));
        preview.UpdateLayout();

        int frames = (int)(LengthMs / 1000 * Fps);
        for (int i = 0; i < frames; i++)
        {
            preview.RenderFrame(i * 1000 / Fps);
            preview.UpdateLayout();
            var bmp = new RenderTargetBitmap((int)(Width * Scale), (int)(Height * Scale), 96 * Scale, 96 * Scale,
                                             PixelFormats.Pbgra32);
            bmp.Render(preview);
            var png = new PngBitmapEncoder();
            png.Frames.Add(BitmapFrame.Create(bmp));
            using var fs = File.Create(Path.Combine(dir, $"frame{i:0000}.png"));
            png.Save(fs);
        }
    }
}
