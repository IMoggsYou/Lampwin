using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using MacWin.Settings.Pages;

namespace MacWin.Settings;

public sealed class NavItem
{
    public required string Title { get; init; }
    public required string Keywords { get; init; }
    public required Brush Tint { get; init; }
    public required Geometry Icon { get; init; }
    public required Func<UserControl> Create { get; init; }
}

public partial class MainWindow : Window
{
    public const string WindowTitle = "MacWin – ustawienia";

    private readonly SettingsStore _store = new();
    private readonly List<NavItem> _items;
    private readonly Dictionary<NavItem, UserControl> _pages = new();
    private readonly ICollectionView _view;
    private readonly DispatcherTimer _statusPoll = new() { Interval = TimeSpan.FromSeconds(1.5) };
    private readonly string? _snapshotDir;

    public MainWindow(string? snapshotDir, string? startPage)
    {
        InitializeComponent();
        Title = WindowTitle;
        _snapshotDir = snapshotDir;
        _store.Load();
        DataContext = _store.Model;
        _ = _store.Model.RefreshAutostartAsync();

        _items = new List<NavItem>
        {
            new()
            {
                Title = "Animacje okien", Keywords = "dżin genie minimalizacja przywracanie styl czas siatka pasek",
                Tint = Gradient("#FF2F7BFF", "#FF8A4FF0"), Icon = GenieIcon(), Create = () => new AnimationsPage(),
            },
            new()
            {
                Title = "Ogólne", Keywords = "włącz wyłącz start autostart uruchom zatrzymaj tryb testowy",
                Tint = Gradient("#FF9A9AA1", "#FF6F6F76"), Icon = SlidersIcon(), Create = () => new GeneralPage(),
            },
            new()
            {
                Title = "Wykluczenia", Keywords = "wyklucz programy gry anty-cheat lista",
                Tint = Gradient("#FFFF6A5F", "#FFE5352B"), Icon = BlockedIcon(), Create = () => new ExclusionsPage(),
            },
            new()
            {
                Title = "Informacje", Keywords = "wersja autorzy licencja log folder",
                Tint = Gradient("#FF7D7AFF", "#FF5856D6"), Icon = InfoIcon(), Create = () => new AboutPage(),
            },
        };
        _view = CollectionViewSource.GetDefaultView(_items);
        Nav.ItemsSource = _view;
        int start = _items.FindIndex(i => string.Equals(i.Title, startPage, StringComparison.OrdinalIgnoreCase));
        Nav.SelectedIndex = start >= 0 ? start : 0;

        SourceInitialized += (_, _) => Native.ApplyBackdrop(this, Theme.IsDark);
        Theme.Changed += () => Native.ApplyBackdrop(this, Theme.IsDark);
        // WindowChrome windows overhang the screen by the resize border when maximized.
        StateChanged += (_, _) => Root.Margin = WindowState == WindowState.Maximized ? new Thickness(7) : new Thickness(0);
        Loaded += (_, _) => PlayOpenAnimation();
        ContentRendered += (_, _) =>
        {
            if (_snapshotDir is not null) _ = RunSnapshots(_snapshotDir);
        };
        Closing += (_, _) => _store.Flush();

        _statusPoll.Tick += (_, _) => UpdateStatus();
        _statusPoll.Start();
        UpdateStatus();
    }

    private void UpdateStatus()
    {
        bool running = HostControl.IsRunning;
        StatusDot.SetResourceReference(Shape.FillProperty, running ? "Brush.Good" : "Brush.Bad");
        StatusText.Text = running ? "MacWin działa" : "MacWin nie działa · kliknij, aby uruchomić";
        StatusPill.Cursor = running ? null : System.Windows.Input.Cursors.Hand;
    }

    private void StatusPill_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (!HostControl.IsRunning) HostControl.Start();
    }

    // macOS-like appearance: a quick fade with a slight zoom.
    private void PlayOpenAnimation()
    {
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
        Close();
    }

    private async Task Capture(string dir, string name)
    {
        {
            var dpi = VisualTreeHelper.GetDpi(this);
            int w = (int)(Root.ActualWidth * dpi.DpiScaleX), h = (int)(Root.ActualHeight * dpi.DpiScaleY);
            // Root rendered directly (a VisualBrush would drop the 3D preview), then laid
            // over a plain color standing in for the Mica backdrop.
            var content = new RenderTargetBitmap(w, h, dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
            content.Render(Root);
            var visual = new DrawingVisual();
            using (var dc = visual.RenderOpen())
            {
                var rect = new Rect(0, 0, Root.ActualWidth, Root.ActualHeight);
                dc.DrawRectangle(new SolidColorBrush(Theme.IsDark ? Color.FromRgb(0x24, 0x24, 0x27) : Color.FromRgb(0xE8, 0xE8, 0xEC)), null, rect);
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

    private static Geometry InfoIcon()
    {
        var g = new GeometryGroup { FillRule = FillRule.Nonzero };
        g.Children.Add(new EllipseGeometry(new Point(8, 3.4), 1.7, 1.7));
        g.Children.Add(new RectangleGeometry(new Rect(6.5, 6.4, 3, 8.4), 1.2, 1.2));
        return g;
    }
}
