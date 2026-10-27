// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows.Media.Imaging;
using MacWin.Settings;

// Turns every icon in a folder (macOS .icns, large PNGs, ...) into the PNG that ships with
// the app: cut to the icon itself and scaled to 256 px with the settings app's own code, so
// importing it later changes nothing. The source file is replaced by <same name>.png; the
// name keeps the program in it, which is how the app gives the icon to its program.
//   dotnet run --project tools/bundle-icons -- assets/app-icons
internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 1 || !Directory.Exists(args[0]))
        {
            Console.Error.WriteLine("usage: bundle-icons <folder>");
            return 2;
        }
        int failed = 0;
        foreach (string file in Directory.GetFiles(args[0]).Where(IconImport.IsSupported).Order())
        {
            string target = Path.ChangeExtension(file, ".png");
            try
            {
                var image = IconImport.Scale(IconImport.Square(IconImport.Trim(IconImport.Load(file))), 256);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(image));
                using (var stream = new MemoryStream())
                {
                    encoder.Save(stream);
                    // The PNG of an icon already shipped as one is written over it.
                    File.WriteAllBytes(target, stream.ToArray());
                }
                if (!string.Equals(file, target, StringComparison.OrdinalIgnoreCase)) File.Delete(file);
                Console.WriteLine($"{Path.GetFileName(target),-70} {new FileInfo(target).Length / 1024,4} KB");
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException or NotSupportedException)
            {
                failed++;
                Console.Error.WriteLine($"{Path.GetFileName(file)}: {ex.Message}");
            }
        }
        return failed == 0 ? 0 : 1;
    }
}
