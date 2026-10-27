// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using MacWin.Settings.Controls;

namespace MacWin.Settings;

/// <summary>"--export-icon file.ico": renders the vector AppIcon at the usual icon sizes and
/// packs them (as PNG entries) into an .ico used by MacWin.exe, this app and the installer.
/// "--export-icon file.png" writes the 256 px image alone (README, social preview).</summary>
internal static class IconExport
{
    private static readonly int[] Sizes = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };

    public static void Save(string path)
    {
        var images = Sizes.Select(Render).ToList();
        if (string.Equals(Path.GetExtension(path), ".png", StringComparison.OrdinalIgnoreCase))
        {
            File.WriteAllBytes(path, images[^1]);
            return;
        }

        using var fs = File.Create(path);
        using var w = new BinaryWriter(fs);
        w.Write((ushort)0);   // reserved
        w.Write((ushort)1);   // type: icon
        w.Write((ushort)images.Count);
        int offset = 6 + 16 * images.Count;
        for (int i = 0; i < images.Count; i++)
        {
            int size = Sizes[i];
            w.Write((byte)(size >= 256 ? 0 : size));
            w.Write((byte)(size >= 256 ? 0 : size));
            w.Write((byte)0);       // palette
            w.Write((byte)0);       // reserved
            w.Write((ushort)1);     // planes
            w.Write((ushort)32);    // bits per pixel
            w.Write(images[i].Length);
            w.Write(offset);
            offset += images[i].Length;
        }
        foreach (var image in images) w.Write(image);
    }

    private static byte[] Render(int size)
    {
        var icon = new AppIcon { Width = size, Height = size };
        icon.Measure(new Size(size, size));
        icon.Arrange(new Rect(0, 0, size, size));
        icon.UpdateLayout();
        var bmp = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        bmp.Render(icon);
        var png = new PngBitmapEncoder();
        png.Frames.Add(BitmapFrame.Create(bmp));
        using var ms = new MemoryStream();
        png.Save(ms);
        return ms.ToArray();
    }
}
