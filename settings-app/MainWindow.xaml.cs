// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using MacWin.Settings.Controls;
using MacWin.Settings.Pages;

namespace MacWin.Settings;

public sealed class NavItem
{
    /// <summary>Stable name for "--page" (titles are translated).</summary>
    public required string Id { get; init; }
    public required string TitleKey { get; init; }
    public required Brush Tint { get; init; }
    public required Geometry Icon { get; init; }
    public required Func<UserControl> Create { get; init; }

    public string Title => Loc.T(TitleKey);

    /// <summary>Search words: the translated ones, plus the English ones, which always work.</summary>
    public string Keywords => string.Join(' ', Loc.T(TitleKey + ".keywords"), Loc.InEnglish(TitleKey),
                                          Loc.InEnglish(TitleKey + ".keywords"), Id);
}

public partial class MainWindow : Window
{
    private readonly SettingsStore _store = new();
    /// <summary>For the pages that work on the file itself (General → Your settings).</summary>
    internal SettingsStore Store => _store;
    private readonly List<NavItem> _items;
    private readonly Dictionary<NavItem, UserControl> _pages = new();
    private readonly ICollectionView _view;
    private readonly DispatcherTimer _statusPoll = new() { Interval = TimeSpan.FromSeconds(1.5) };
    private readonly string? _snapshotDir;
    private readonly WindowShadow _shadow;
    private DateTime _updateStamp;
    private bool _updateOffered;   // once per window: "Not now" means until it opens again

    public MainWindow(string? snapshotDir, string? startPage)
    {
        InitializeComponent();
        _snapshotDir = snapshotDir;
        _store.Load();
        DataContext = _store.Model;
        _ = _store.Model.RefreshAutostartAsync();

        _items = new List<NavItem>
        {
            new()
            {
                Id = "animations", TitleKey = "settings.nav.animations",
                Tint = Gradient("#FF2F7BFF", "#FF8A4FF0"), Icon = GenieIcon(), Create = () => new AnimationsPage(),
            },
            new()
            {
                Id = "corners", TitleKey = "settings.nav.corners",
                Tint = Gradient("#FF34C759", "#FF1F9E46"), Icon = CornersIcon(), Create = () => new CornersPage(),
            },
            new()
            {
                Id = "widgets", TitleKey = "settings.nav.widgets",
                Tint = Gradient("#FF33B5F5", "#FF0A7AFF"), Icon = WidgetsIcon(), Create = () => new WidgetsPage(),
            },
            new()
            {
                Id = "icons", TitleKey = "settings.nav.icons",
                Tint = Gradient("#FFFF9F0A", "#FFFF5E3A"), Icon = AppGridIcon(), Create = () => new AppIconsPage(),
            },
            new()
            {
                Id = "general", TitleKey = "settings.nav.general",
                Tint = Gradient("#FF9A9AA1", "#FF6F6F76"), Icon = SlidersIcon(), Create = () => new GeneralPage(),
            },
            new()
            {
                Id = "exclusions", TitleKey = "settings.nav.exclusions",
                Tint = Gradient("#FFFF6A5F", "#FFE5352B"), Icon = BlockedIcon(), Create = () => new ExclusionsPage(),
            },
            new()
            {
                Id = "privacy", TitleKey = "settings.nav.privacy",
                Tint = Gradient("#FF4C9BFF", "#FF2B63E0"), Icon = HandIcon(), Create = () => new PrivacyPage(),
            },
            new()
            {
                Id = "about", TitleKey = "settings.nav.about",
                Tint = Gradient("#FF7D7AFF", "#FF5856D6"), Icon = InfoIcon(), Create = () => new AboutPage(),
            },
        };
        _view = CollectionViewSource.GetDefaultView(_items);
        Nav.ItemsSource = _view;
        ApplyLanguage();   // before a page exists: nothing to rebuild yet
        int start = PageIndex(startPage);
        Nav.SelectedIndex = start >= 0 ? start : 0;

        Loc.Changed += ApplyLanguage;
        Closed += (_, _) => Loc.Changed -= ApplyLanguage;
        _store.Model.PropertyChanged += (_, e) =>
        {
            // After the click that changed it has finished: the page holding the list is rebuilt.
            if (e.PropertyName == nameof(SettingsModel.Language))
                Dispatcher.BeginInvoke(DispatcherPriority.Background, () => Loc.Apply(_store.Model.Language));
        };

        // This window has the corners it sets (it draws them itself, see UpdateShape).
        _shadow = new WindowShadow(this, CornerRadius, CornerSmoothing);
        _store.Model.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName is not (nameof(SettingsModel.CornerRadius) or nameof(SettingsModel.CornerSmoothing))) return;
            UpdateShape();
            _shadow.SetCorners(CornerRadius, CornerSmoothing);
        };
        SourceInitialized += (_, _) =>
        {
            Native.ApplyWindowAttributes(this, Theme.IsDark);
            HwndSource.FromHwnd(new WindowInteropHelper(this).Handle)?.AddHook(WndProc);
        };
        Theme.Changed += () => Native.ApplyWindowAttributes(this, Theme.IsDark);
        StateChanged += (_, _) =>
        {
            UpdateShape();
            _shadow.UpdateVisibility();
        };
        Loaded += (_, _) =>
        {
            PlayOpenAnimation();
            if (_snapshotDir is null) _shadow.Attach(TimeSpan.FromMilliseconds(200));
        };
        Closed += (_, _) => _shadow.Close();
        ContentRendered += async (_, _) =>
        {
            if (_snapshotDir is not null)
            {
                _ = RunSnapshots(_snapshotDir);
                return;
            }
            await Task.Delay(450);   // after the window's own opening animation
            OfferUpdate();
            // Icons that came with MacWin (or with an update of it) go to their programs.
            _ = AppIconActions.EnsureBundledAsync(_store.Model);
        };
        UpdateSheet.Finished += install =>
        {
            if (UpdateSheet.DontShowAgain) _store.Model.UpdatePrompt = false;
            if (!install || !UpdateClient.Install()) return;
            // The General page shows the download and the installation.
            Search.Clear();
            Nav.SelectedIndex = PageIndex("general");
        };
        Closing += (_, _) => _store.Flush();

        _statusPoll.Tick += (_, _) => UpdateStatus();
        _statusPoll.Start();
        UpdateStatus();
    }

    /// <summary>Index of a page by its id ("general") or its title in any language.</summary>
    public int PageIndex(string? page)
    {
        if (string.IsNullOrEmpty(page)) return -1;
        return _items.FindIndex(i => string.Equals(i.Id, page, StringComparison.OrdinalIgnoreCase) ||
                                     string.Equals(i.Title, page, StringComparison.OrdinalIgnoreCase));
    }

    private static readonly uint ShowMessage = Native.RegisterWindowMessage(Native.ShowMessage);

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_WINDOWPOSCHANGED = 0x0047, WM_GETMINMAXINFO = 0x0024;
        if (msg == WM_GETMINMAXINFO) Native.FitMaximizedToWorkArea(hwnd, lParam);   // WPF still adds the min size
        else if (msg == WM_WINDOWPOSCHANGED) _shadow.Sync();   // moved, resized or z-order changed
        else if ((uint)msg == ShowMessage)
        {
            // Another copy was started (Start menu, tray): it hands over to this window.
            int page = (int)wParam - 1;
            if (page >= 0 && page < _items.Count)
            {
                Search.Clear();
                Nav.SelectedItem = _items[page];
            }
            Native.Activate(hwnd);
            handled = true;
        }
        return IntPtr.Zero;
    }

    // The language changed (or was set for the first time): every text, the reading
    // direction and the fonts follow. Pages are made again, so their texts are fresh.
    private void ApplyLanguage()
    {
        Title = Loc.T("settings.window.title");
        Frame.FlowDirection = Loc.FlowDirection;
        Language = Loc.XmlLanguage;
        // Western digits everywhere: with Arabic, WPF would treat them as Arabic numbers
        // and reorder "Windows 11" into "11 Windows".
        NumberSubstitution.SetCultureSource(this, NumberCultureSource.Override);
        NumberSubstitution.SetCultureOverride(this, System.Globalization.CultureInfo.GetCultureInfo("en-US"));
        NumberSubstitution.SetSubstitution(this, NumberSubstitutionMethod.European);
        Search.Tag = Loc.T("settings.search.placeholder");
        Nav.Items.Refresh();
        var current = Nav.SelectedItem as NavItem;
        _pages.Clear();
        if (current is not null)
        {
            Nav.SelectedItem = null;
            Nav.SelectedItem = current;
        }
        UpdateStatus();
    }

    private double CornerRadius => _store.Model.CornerRadius;
    private double CornerSmoothing => _store.Model.CornerSmoothing / 100;

    private void Root_SizeChanged(object sender, SizeChangedEventArgs e) => UpdateShape();

    // The corners from the settings: clip the whole window (sidebar, content, outline) to
    // its shape. A maximized window fills the work area and is square, like on macOS.
    private void UpdateShape()
    {
        Geometry? shape = WindowState == WindowState.Maximized || Root.ActualWidth <= 0
            ? null
            : Squircle.Create(Root.ActualWidth, Root.ActualHeight, CornerRadius, CornerSmoothing);
        Root.Clip = shape;
        Outline.Data = shape;
    }

    private void UpdateStatus()
    {
        bool running = HostControl.IsRunning;
        StatusDot.SetResourceReference(Shape.FillProperty, running ? "Brush.Good" : "Brush.Bad");
        StatusText.Text = Loc.T(running ? "settings.status.running" : "settings.status.stopped");
        StatusPill.Cursor = running ? null : System.Windows.Input.Cursors.Hand;
        if (IsLoaded) OfferUpdate();
    }

    // Without automatic installs, a version MacWin found is offered here: when the window
    // opens, or when it is found while the window is open.
    private void OfferUpdate()
    {
        var m = _store.Model;
        if (_updateOffered || _snapshotDir is not null || !m.UpdateCheck || m.UpdateAutoInstall || !m.UpdatePrompt ||
            !HostControl.IsRunning)
            return;
        DateTime stamp;
        try
        {
            stamp = File.GetLastWriteTimeUtc(UpdateClient.StatePath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return;
        }
        if (stamp == _updateStamp) return;
        _updateStamp = stamp;
        var update = UpdateClient.Read();
        if (update.Status != "available" || string.IsNullOrEmpty(update.Latest)) return;
        _updateOffered = true;
        UpdateSheet.Show(update);
    }

    /// <summary>The update sheet on request (General page, "What's new").</summary>
    internal void ShowUpdate(UpdateState update)
    {
        _updateOffered = true;
        if (!UpdateSheet.IsOpen) UpdateSheet.Show(update);
    }

    private void StatusPill_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (!HostControl.IsRunning) HostControl.Start();
    }

    // macOS-like appearance: a quick fade with a slight zoom.
    private void PlayOpenAnimation()
    {
        if (!Motion.On) return;
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        Root.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(200)));
        var zoom = new DoubleAnimation(0.965, 1, TimeSpan.FromMilliseconds(320)) { EasingFunction = ease };
        RootScale.BeginAnimation(ScaleTransform.ScaleXProperty, zoom);
        RootScale.BeginAnimation(ScaleTransform.ScaleYProperty, zoom);
    }

    private void Nav_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Nav.SelectedItem is not NavItem item) return;
        if (!_pages.TryGetValue(item, out var page))
        {
            page = item.Create();
            _pages[item] = page;
        }
        PageTitle.Text = item.Title;
        PageHost.Content = page;
        Scroller.ScrollToTop();

        // Page transition: fade in while sliding up a few pixels.
        if (!Motion.On) return;
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        PageHost.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220)));
        PageShift.BeginAnimation(TranslateTransform.YProperty,
                                 new DoubleAnimation(14, 0, TimeSpan.FromMilliseconds(360)) { EasingFunction = ease });
    }

    private void Search_TextChanged(object sender, TextChangedEventArgs e)
    {
        string q = Search.Text.Trim();
        _view.Filter = q.Length == 0
            ? null
            : o => o is NavItem i && (i.Title.Contains(q, StringComparison.CurrentCultureIgnoreCase) ||
                                      i.Keywords.Contains(q, StringComparison.CurrentCultureIgnoreCase));
        if (Nav.SelectedItem is null || !_view.Cast<object>().Contains(Nav.SelectedItem))
        {
            var first = _view.Cast<object>().FirstOrDefault();
            if (first is not null) Nav.SelectedItem = first;
        }
    }

    // --snapshot <dir>: renders every page of this window (only this window) to PNG, then
    // closes. Used to check the layout without looking at the desktop.
    private async Task RunSnapshots(string dir)
    {
        Directory.CreateDirectory(dir);
        await Task.Delay(300);
        for (int i = 0; i < _items.Count; i++)
        {
            Nav.SelectedIndex = i;
            await Task.Delay(i == 0 ? 400 : 1100);
            await Capture(dir, $"page{i}.png");
            if (Scroller.ScrollableHeight > 0)
            {
                Scroller.ScrollToEnd();   // SmoothScroll animates there
                await Task.Delay(900);
                await Capture(dir, $"page{i}-end.png");
            }
        }
        // Frames of the preview's genie (it starts minimizing 1.2 s after it appears).
        Nav.SelectedIndex = 0;
        double[] at = { 1330, 1440, 1550, 1640 };
        var clock = System.Diagnostics.Stopwatch.StartNew();
        for (int f = 0; f < at.Length; f++)
        {
            await Task.Delay(TimeSpan.FromMilliseconds(Math.Max(0, at[f] - clock.Elapsed.TotalMilliseconds)));
            await Capture(dir, $"genie{f}.png");
        }
        // The update sheet, folded and unfolded, with sample notes.
        UpdateSheet.Show(new UpdateState("available", Project.Version, "9.9.0", null, null, SampleNotes));
        await Task.Delay(700);
        await Capture(dir, "update.png");
        UpdateSheet.ShowNotes();
        await Task.Delay(700);
        await Capture(dir, "update-notes.png");
        Close();
    }

    // Release notes as GitHub has them (a CHANGELOG.md section), for --snapshot only.
    private const string SampleNotes = """
        ### Added

        - A third animation style, **Scale**: the window shrinks into its taskbar icon in one
          smooth glide and melts away, like the scale effect of macOS (Settings → Animations).

        ### Changed

        - The animations are smoother and more polished: the window starts and stops moving
          more gently and keeps its shadow all the way.
        - The settings feel smoother: a click on a slider's track glides the knob there.
        - The live preview shows every style the way the desktop does.

        ### Fixed

        - The live preview in **Settings → Animations** has rounded corners again, instead of
          square ones sticking out of its card.
        - After **Show desktop** (Win+D), a window that opened, closed or minimized was animated
          under the desktop widgets; the animation now covers them from the first frame.
        """;

    private async Task Capture(string dir, string name)
    {
        {
            var dpi = VisualTreeHelper.GetDpi(this);
            int w = (int)(Root.ActualWidth * dpi.DpiScaleX), h = (int)(Root.ActualHeight * dpi.DpiScaleY);
            // Root rendered directly (a VisualBrush would drop the 3D preview), then laid
            // over a plain color standing in for the desktop, so the corners show.
            var content = new RenderTargetBitmap(w, h, dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
            content.Render(Root);
            var visual = new DrawingVisual();
            using (var dc = visual.RenderOpen())
            {
                var rect = new Rect(0, 0, Root.ActualWidth, Root.ActualHeight);
                // --transparent: only the window (README screenshots); else on a desktop color.
                if (!Environment.GetCommandLineArgs().Contains("--transparent"))
                    dc.DrawRectangle(new SolidColorBrush(Color.FromRgb(0x5A, 0x6E, 0x96)), null, rect);
                // Right-to-left: Frame mirrors Root on screen; Root rendered alone is not.
                if (Loc.IsRightToLeft) dc.PushTransform(new MatrixTransform(-1, 0, 0, 1, rect.Width, 0));
                dc.DrawImage(content, rect);
            }
            var bmp = new RenderTargetBitmap(w, h, dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
            bmp.Render(visual);
            var png = new PngBitmapEncoder();
            png.Frames.Add(BitmapFrame.Create(bmp));
            await using var fs = File.Create(System.IO.Path.Combine(dir, name));
            png.Save(fs);
        }
    }

    private static Brush Gradient(string top, string bottom)
    {
        var b = new LinearGradientBrush((Color)ColorConverter.ConvertFromString(top),
                                        (Color)ColorConverter.ConvertFromString(bottom), 90);
        b.Freeze();
        return b;
    }

    private static Geometry GenieIcon() => Geometry.Parse(
        "M3,2 H13 A1.5,1.5 0 0 1 14.5,3.5 V7 H1.5 V3.5 A1.5,1.5 0 0 1 3,2 Z " +
        "M1.5,8.2 H14.5 C13,10.3 13.2,12.4 15,14.6 H12.6 C10,12.6 6.2,10 1.5,8.2 Z");

    // A window outline with one strongly rounded corner (top left).
    private static Geometry CornersIcon() => Geometry.Parse(
        "M8,2 H14 V14 H2 V8 A6,6 0 0 1 8,2 Z M8,3.6 A4.4,4.4 0 0 0 3.6,8 V12.4 H12.4 V3.6 Z");

    // Two small widgets above a wide one.
    private static Geometry WidgetsIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        g.Children.Add(new RectangleGeometry(new Rect(1.5, 1.5, 5.8, 5.8), 1.7, 1.7));
        g.Children.Add(new RectangleGeometry(new Rect(8.7, 1.5, 5.8, 5.8), 1.7, 1.7));
        g.Children.Add(new RectangleGeometry(new Rect(1.5, 8.7, 13, 5.8), 1.7, 1.7));
        return g;
    }

    // Four app icons in a grid.
    private static Geometry AppGridIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        foreach (var (x, y) in new[] { (1.5, 1.5), (8.7, 1.5), (1.5, 8.7), (8.7, 8.7) })
            g.Children.Add(new RectangleGeometry(new Rect(x, y, 5.8, 5.8), 1.9, 1.9));
        return g;
    }

    private static Geometry SlidersIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        g.Children.Add(Geometry.Parse("M1,3.3 H15 V4.7 H1 Z M1,7.3 H15 V8.7 H1 Z M1,11.3 H15 V12.7 H1 Z"));
        g.Children.Add(new EllipseGeometry(new Point(5, 4), 2.3, 2.3));
        g.Children.Add(new EllipseGeometry(new Point(11, 8), 2.3, 2.3));
        g.Children.Add(new EllipseGeometry(new Point(7, 12), 2.3, 2.3));
        return g;
    }

    private static Geometry BlockedIcon()
    {
        var ring = new CombinedGeometry(GeometryCombineMode.Exclude,
                                        new EllipseGeometry(new Point(8, 8), 7, 7),
                                        new EllipseGeometry(new Point(8, 8), 5.1, 5.1));
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        g.Children.Add(ring);
        g.Children.Add(Geometry.Parse("M3.7,4.9 L4.9,3.7 L12.3,11.1 L11.1,12.3 Z"));
        return g;
    }

    // A raised hand, like the privacy icon of macOS: four fingers, the thumb, the palm.
    private static Geometry HandIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        foreach (var (x, top) in new[] { (4.1, 3.6), (6.5, 1.6), (8.9, 1.2), (11.3, 2.6) })
            g.Children.Add(new RectangleGeometry(new Rect(x, top, 2.1, 9 - top), 1.05, 1.05));
        g.Children.Add(Geometry.Parse("M4.1,8 H13.4 V10.6 C13.4,13.2 11.6,15 9,15 H8.4 C6.6,15 5.4,14.2 4.5,12.8 " +
                                      "L1.6,8.6 C1.1,7.9 1.6,7 2.4,7 C2.9,7 3.3,7.3 3.6,7.7 L4.1,8.4 Z"));
        return g;
    }

    private static Geometry InfoIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        g.Children.Add(new EllipseGeometry(new Point(8, 3.4), 1.7, 1.7));
        g.Children.Add(new RectangleGeometry(new Rect(6.5, 6.4, 3, 8.4), 1.2, 1.2));
        return g;
    }
}
