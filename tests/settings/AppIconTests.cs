// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Buffers.Binary;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using MacWin.Settings;

namespace MacWin.Settings.Tests;

/// <summary>App icons: names, the program an icon is for, shortcuts' programs, and the
/// conversion of a macOS icon into a Windows one. Touches no real shortcut.</summary>
internal static class AppIconTests
{
    public static void RunAll(Action<string, Action> run, Action<bool, string> check, string temp)
    {
        run("IconNamesAreCleaned", () =>
        {
            (string, string)[] cases =
            [
                ("Discord__Dark___macOS_26.2___VGpMiSdUqI_icns-f048a3f6c5", "Discord (Dark) macOS 26.2"),
                ("Brave_OATsGHKcn2_icns-4677622932", "Brave"),
                ("Figma_GdeyafL4j1-4a53dc97aa", "Figma"),
                ("Counter-Strike_2_qaj0ACp4Mz_icns-948f1629ba", "Counter-Strike 2"),
                ("Visual_Studio_Code_o1GFnO5Z5v_icns-e0787d4c80", "Visual Studio Code"),
                ("Terminal__MacOS_Tahoe__yqUGh6O6LH_icns-37f531ff27", "Terminal (MacOS Tahoe)"),
                ("Spotify", "Spotify"),
            ];
            foreach (var (file, name) in cases)
                check(IconNames.Clean(file) == name, $"Clean({file}) = '{IconNames.Clean(file)}', wanted '{name}'");
        });

        run("ProgramsAreMatched", () =>
        {
            KnownProgram[] pc =
            [
                new("discord.exe", "Discord"),
                new("chrome.exe", "Google Chrome"),
                new("olk.exe", "Outlook"),
                new("code.exe", "Visual Studio Code"),
                new("windowsterminal.exe", "Terminal"),
                new("firefox.exe", "Firefox"),
                new("spotify.exe", "Spotify"),
                new("cs2.exe", "Counter-Strike 2"),
                new("robloxplayerbeta.exe", "Roblox Player"),
                new("robloxstudiobeta.exe", "Roblox Studio"),
            ];
            (string Icon, string? Exe)[] cases =
            [
                ("Discord (Dark) macOS 26.2", "discord.exe"),
                ("low res Google Chrome", "chrome.exe"),
                ("Microsoft Outlook", "olk.exe"),
                ("Visual Studio Code", "code.exe"),
                ("Terminal (MacOS Tahoe)", "windowsterminal.exe"),
                ("Spotify Liquid Glass", "spotify.exe"),
                ("Counter-Strike 2", "cs2.exe"),
                ("Roblox macOS 27 Icon", "robloxplayerbeta.exe"),
                ("low res Roblox Studio", "robloxstudiobeta.exe"),
                ("Tor Browser", null),        // runs as firefox.exe: never guessed
                ("hermes", null),             // no such program here: the icon waits
                ("Proton VPN", null),         // not installed here
            ];
            foreach (var (icon, exe) in cases)
            {
                string? got = IconNames.Match(icon, pc);
                check(got == exe, $"Match({icon}) = {got ?? "none"}, wanted {exe ?? "none"}");
            }
            check(IconNames.IsLowQuality("low res Google Chrome") && !IconNames.IsLowQuality("Google Chrome"), "low res");
        });

        run("ShortcutProgramIsFound", () =>
        {
            check(AppIconStore.ProgramOf(@"C:\Apps\Brave\brave.exe", "") == "brave.exe", "plain target");
            check(AppIconStore.ProgramOf(@"C:\Users\u\AppData\Local\Discord\Update.exe", "--processStart Discord.exe") == "discord.exe",
                  "Squirrel target");
            check(AppIconStore.ProgramOf(@"C:\x\Update.exe", "--processStart \"Slack.exe\" --process-start-args x") == "slack.exe",
                  "Squirrel quoted");
            check(AppIconStore.ProgramOf(@"C:\x\Update.exe", "") is null, "Update.exe alone");
            check(AppIconStore.ProgramOf(@"C:\x\readme.txt", "") is null, "not a program");
        });

        run("SetIsReadSafely", () =>
        {
            string set = Path.Combine(temp, "test" + AppIconStore.SetExtension);
            using (var zip = System.IO.Compression.ZipFile.Open(set, System.IO.Compression.ZipArchiveMode.Create))
            {
                void Add(string name, string text)
                {
                    using var w = new StreamWriter(zip.CreateEntry(name).Open());
                    w.Write(text);
                }
                Add("icons/Discord.ico", "ico");
                Add("icons/../../evil.ico", "x");
                Add("other/Spotify.ico", "x");
                Add("set.json", """{"Version":1,"Apps":{"Discord.exe":"Discord","spotify.exe":"Spotify","..\\x.exe":"Discord","notes.txt":"Discord"}}""");
            }
            string folder = Path.Combine(temp, "set");
            var apps = AppIconStore.ExtractSet(set, folder);
            check(apps.Count == 1 && apps.GetValueOrDefault("discord.exe") == "Discord", $"choices: {string.Join(", ", apps)}");
            var files = Directory.GetFiles(folder).Select(Path.GetFileName).ToList();
            check(files.SequenceEqual(["Discord.ico", "evil.ico"]) || files.SequenceEqual(["Discord.ico"]), $"files: {string.Join(", ", files)}");
            check(!File.Exists(Path.Combine(temp, "evil.ico")) && !File.Exists(Path.Combine(folder, "..", "..", "evil.ico")), "nothing outside");
        });

        run("IcnsBecomesIco", () => Sta(() =>
        {
            // A 512 px macOS-style icon: a square drawn in the middle 80 % of the canvas.
            byte[] png = Png(512, 52);
            byte[] icns = Icns(("ic07", Png(128, 13)), ("ic09", png), ("TOC ", new byte[8]));
            check(IconImport.LargestIcnsPng(icns) is byte[] best && best.SequenceEqual(png), "largest PNG picked");
            string source = Path.Combine(temp, "Test_App_aBcDeFgHiJ_icns-0123456789.icns");
            File.WriteAllBytes(source, icns);
            string ico = Path.Combine(temp, "test.ico"), preview = Path.Combine(temp, "test.png");
            IconImport.Convert(source, ico, preview);

            byte[] data = File.ReadAllBytes(ico);
            int count = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(4));
            check(count == IconImport.Sizes.Length, $"{count} images in the .ico");
            var frames = new IconBitmapDecoder(new MemoryStream(data), BitmapCreateOptions.None, BitmapCacheOption.OnLoad).Frames;
            check(frames.Max(f => f.PixelWidth) == 256, "a 256 px image");
            // Trimmed: the drawn square fills the whole icon (as Windows icons do), not 80 %.
            var big = frames.First(f => f.PixelWidth == 256);
            var pixels = new byte[256 * 256 * 4];
            new FormatConvertedBitmap(big, PixelFormats.Bgra32, null, 0).CopyPixels(pixels, 256 * 4, 0);
            int firstOpaque = Enumerable.Range(0, 256).First(x => pixels[(128 * 256 + x) * 4 + 3] > 128);
            check(firstOpaque <= 2, $"margin after trimming: {firstOpaque} px");
            check(File.Exists(preview), "preview written");
        }));
    }

    private static void Sta(Action work)
    {
        Exception? error = null;
        var thread = new Thread(() =>
        {
            try { work(); } catch (Exception ex) { error = ex; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (error is not null) throw error;
    }

    private static byte[] Png(int size, int margin)
    {
        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen())
            dc.DrawRectangle(Brushes.MediumPurple, null, new Rect(margin, margin, size - 2 * margin, size - 2 * margin));
        var bitmap = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(visual);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream = new MemoryStream();
        encoder.Save(stream);
        return stream.ToArray();
    }

    private static byte[] Icns(params (string Type, byte[] Data)[] entries)
    {
        using var stream = new MemoryStream();
        void Be(uint v)
        {
            Span<byte> b = stackalloc byte[4];
            BinaryPrimitives.WriteUInt32BigEndian(b, v);
            stream.Write(b);
        }
        stream.Write("icns"u8);
        Be((uint)(8 + entries.Sum(e => 8 + e.Data.Length)));
        foreach (var (type, data) in entries)
        {
            stream.Write(System.Text.Encoding.ASCII.GetBytes(type));
            Be((uint)(8 + data.Length));
            stream.Write(data);
        }
        return stream.ToArray();
    }
}
