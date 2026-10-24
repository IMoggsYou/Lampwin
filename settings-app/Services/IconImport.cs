// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Buffers.Binary;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace MacWin.Settings;

/// <summary>
/// Turns an icon file into a Windows icon: macOS .icns, any image Windows can read (PNG,
/// JPEG, BMP, GIF, TIFF, WebP, ICO, ...) or the icon of a program (.exe, .dll). The image is
/// cut to the icon itself - macOS icons sit in a wide transparent margin with a soft shadow
/// under them, while Windows icons fill their square - so it is as big as other programs'
/// icons; then drawn at every size Windows asks for and stored as one .ico with PNG images,
/// plus a 256 px preview.
/// </summary>
internal static class IconImport
{
    public static readonly int[] Sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
    public static readonly string[] Extensions =
    [
        ".icns", ".png", ".ico", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".webp", ".heic", ".heif",
        ".avif", ".jxr", ".exe", ".dll",
    ];

    // Bigger files are refused: no icon is that big, and decoding it would cost seconds.
    public const long MaxBytes = 64L * 1024 * 1024;
    // Decoded images larger than this are scaled down first: 256 px is the largest size
    // kept, so the rest of the work stays fast however big the file is.
    private const int WorkingSize = 1024;
    // What counts as the icon when it is cut out: its solid body, not the faint shadow
    // macOS icons cast below them.
    private const byte BodyAlpha = 128;

    public static bool IsSupported(string path) =>
        Extensions.Contains(Path.GetExtension(path).ToLowerInvariant());

    /// <summary>Writes `icoPath` and `pngPath` from `source`. Throws on a file that holds
    /// no usable image (the caller reports it).</summary>
    public static void Convert(string source, string icoPath, string pngPath)
    {
        BitmapSource square = Square(Trim(Shrink(Load(source))));
        var images = Sizes.Select(s => Png(Scale(square, s))).ToList();
        WriteIco(icoPath, images);
        WriteAtomically(pngPath, images[^1]);
    }

    // A temporary file of its own, then moved into place: a half-written file is never
    // seen, and two imports of the same name never write into each other.
    private static void WriteAtomically(string path, byte[] data)
    {
        string tmp = $"{path}.{Guid.NewGuid():N}.tmp";
        File.WriteAllBytes(tmp, data);
        File.Move(tmp, path, overwrite: true);
    }

    /// <summary>The largest image in the file.</summary>
    public static BitmapSource Load(string path)
    {
        var info = new FileInfo(path);
        if (info.Length > MaxBytes) throw new InvalidDataException("The file is too big for an icon.");
        string ext = info.Extension.ToLowerInvariant();
        if (ext is ".exe" or ".dll") return ProgramIcon(path);
        byte[] data = File.ReadAllBytes(path);
        if (ext == ".icns") data = LargestIcnsPng(data) ?? throw new InvalidDataException("No PNG image in the .icns file.");
        using var stream = new MemoryStream(data);
        var decoder = BitmapDecoder.Create(stream, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        BitmapFrame best = decoder.Frames.OrderByDescending(f => f.PixelWidth * f.PixelHeight).First();
        var bgra = new FormatConvertedBitmap(best, PixelFormats.Bgra32, null, 0);
        bgra.Freeze();
        return bgra;
    }

    /// <summary>A program's own icon, as large as it has it (up to 256 px).</summary>
    private static BitmapSource ProgramIcon(string path)
    {
        var icons = new IntPtr[1];
        if (PrivateExtractIcons(path, 0, 256, 256, icons, null, 1, 0) < 1 || icons[0] == IntPtr.Zero)
            throw new InvalidDataException("The program has no icon.");
        try
        {
            var source = System.Windows.Interop.Imaging.CreateBitmapSourceFromHIcon(icons[0], Int32Rect.Empty,
                                                                                    BitmapSizeOptions.FromEmptyOptions());
            var bgra = new FormatConvertedBitmap(source, PixelFormats.Bgra32, null, 0);
            bgra.Freeze();
            return bgra;
        }
        finally
        {
            DestroyIcon(icons[0]);
        }
    }

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern uint PrivateExtractIcons(string file, int index, int cx, int cy, IntPtr[] icons, int[]? ids,
                                                   uint count, uint flags);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr icon);

    /// <summary>A very large image scaled down to the working size first.</summary>
    private static BitmapSource Shrink(BitmapSource image)
    {
        int side = Math.Max(image.PixelWidth, image.PixelHeight);
        if (side <= WorkingSize) return image;
        double k = (double)WorkingSize / side;
        var small = new FormatConvertedBitmap(new TransformedBitmap(image, new ScaleTransform(k, k)), PixelFormats.Bgra32, null, 0);
        small.Freeze();
        return small;
    }

    /// <summary>The PNG data of the widest image in an .icns (macOS 10.7+ stores its icons
    /// as PNG: ic07..ic14); null when there is none (old JPEG 2000 / raw ones).</summary>
    public static byte[]? LargestIcnsPng(byte[] data)
    {
        if (data.Length < 8 || data[0] != 'i' || data[1] != 'c' || data[2] != 'n' || data[3] != 's') return null;
        int end = (int)Math.Min(data.Length, BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(4)));
        byte[]? best = null;
        int bestWidth = 0;
        for (int pos = 8; pos + 8 <= end;)
        {
            int length = (int)BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(pos + 4));
            if (length < 8 || pos + length > end) break;
            var body = data.AsSpan(pos + 8, length - 8);
            if (body.Length > 24 && body[0] == 0x89 && body[1] == 'P' && body[2] == 'N' && body[3] == 'G')
            {
                int width = (int)BinaryPrimitives.ReadUInt32BigEndian(body[16..]);   // IHDR
                if (width > bestWidth)
                {
                    bestWidth = width;
                    best = body.ToArray();
                }
            }
            pos += length;
        }
        return best;
    }

    /// <summary>Cut to the icon's solid body (its shadow and empty margin go).</summary>
    public static BitmapSource Trim(BitmapSource image)
    {
        int w = image.PixelWidth, h = image.PixelHeight, stride = w * 4;
        var pixels = new byte[stride * h];
        image.CopyPixels(pixels, stride, 0);
        int left = w, top = h, right = -1, bottom = -1;
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                if (pixels[y * stride + x * 4 + 3] < BodyAlpha) continue;
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
        }
        if (right < left) return image;   // nothing drawn: left as it is
        var crop = new CroppedBitmap(image, new Int32Rect(left, top, right - left + 1, bottom - top + 1));
        crop.Freeze();
        return crop;
    }

    /// <summary>Centred in a transparent square (a wide or tall image keeps its shape).</summary>
    public static BitmapSource Square(BitmapSource image)
    {
        int canvas = Math.Max(image.PixelWidth, image.PixelHeight);
        if (image.PixelWidth == image.PixelHeight) return image;
        return Draw(canvas, dc => dc.DrawImage(image, new Rect((canvas - image.PixelWidth) / 2.0,
                                                               (canvas - image.PixelHeight) / 2.0,
                                                               image.PixelWidth, image.PixelHeight)));
    }

    /// <summary>High-quality downscale: halved step by step (each step averages 2x2
    /// pixels), the last step straight to the size - sharp small sizes, no shimmer.</summary>
    public static BitmapSource Scale(BitmapSource image, int size)
    {
        BitmapSource current = image;
        while (current.PixelWidth / 2 >= size)
        {
            int half = current.PixelWidth / 2;
            var source = current;
            current = Draw(half, dc => dc.DrawImage(source, new Rect(0, 0, half, half)));
        }
        if (current.PixelWidth == size) return current;
        var last = current;
        return Draw(size, dc => dc.DrawImage(last, new Rect(0, 0, size, size)));
    }

    private static BitmapSource Draw(int size, Action<DrawingContext> draw)
    {
        var visual = new DrawingVisual();
        RenderOptions.SetBitmapScalingMode(visual, BitmapScalingMode.HighQuality);
        using (var dc = visual.RenderOpen()) draw(dc);
        var bitmap = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(visual);
        bitmap.Freeze();
        return bitmap;
    }

    private static byte[] Png(BitmapSource image)
    {
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(image));
        using var stream = new MemoryStream();
        encoder.Save(stream);
        return stream.ToArray();
    }

    /// <summary>An .ico of PNG images (Windows Vista and later read PNG at every size).</summary>
    public static void WriteIco(string path, IReadOnlyList<byte[]> pngs)
    {
        using var stream = new MemoryStream();
        using (var w = new BinaryWriter(stream, System.Text.Encoding.UTF8, leaveOpen: true))
        {
            w.Write((ushort)0);            // reserved
            w.Write((ushort)1);            // type: icon
            w.Write((ushort)pngs.Count);
            int offset = 6 + 16 * pngs.Count;
            foreach (byte[] png in pngs)
            {
                int size = (int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16));
                w.Write((byte)(size >= 256 ? 0 : size));   // width (0 = 256)
                w.Write((byte)(size >= 256 ? 0 : size));   // height
                w.Write((byte)0);          // colors
                w.Write((byte)0);          // reserved
                w.Write((ushort)1);        // planes
                w.Write((ushort)32);       // bits per pixel
                w.Write(png.Length);
                w.Write(offset);
                offset += png.Length;
            }
            foreach (byte[] png in pngs) w.Write(png);
        }
        WriteAtomically(path, stream.ToArray());
    }
}
