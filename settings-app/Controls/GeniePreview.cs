using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using System.Windows.Shapes;

namespace MacWin.Settings.Controls;

/// <summary>
/// Live preview: a miniature desktop where a window genies into its taskbar icon and back,
/// using the same curves as the real renderer (a textured mesh in an orthographic
/// Viewport3D). It follows the chosen style and duration as they change. Clicking turns the
/// animation around mid-flight, exactly like MacWin does on fast clicks.
/// </summary>
public sealed class GeniePreview : Grid
{
    public static readonly DependencyProperty GenieStyleProperty = DependencyProperty.Register(
        nameof(GenieStyle), typeof(int), typeof(GeniePreview), new PropertyMetadata(0));
    public static readonly DependencyProperty DurationMsProperty = DependencyProperty.Register(
        nameof(DurationMs), typeof(double), typeof(GeniePreview), new PropertyMetadata(450.0));
    public static readonly DependencyProperty TilesProperty = DependencyProperty.Register(
        nameof(Tiles), typeof(double), typeof(GeniePreview), new PropertyMetadata(48.0));
    public static readonly DependencyProperty CornerRadiusPxProperty = DependencyProperty.Register(
        nameof(CornerRadiusPx), typeof(double), typeof(GeniePreview), new PropertyMetadata(15.0, OnCornersChanged));
    public static readonly DependencyProperty CornerSmoothingProperty = DependencyProperty.Register(
        nameof(CornerSmoothing), typeof(double), typeof(GeniePreview), new PropertyMetadata(100.0, OnCornersChanged));

    public int GenieStyle { get => (int)GetValue(GenieStyleProperty); set => SetValue(GenieStyleProperty, value); }
    public double DurationMs { get => (double)GetValue(DurationMsProperty); set => SetValue(DurationMsProperty, value); }
    public double Tiles { get => (double)GetValue(TilesProperty); set => SetValue(TilesProperty, value); }
    /// <summary>Corner radius of a real window (px at 100 %), drawn scaled to the miniature.</summary>
    public double CornerRadiusPx { get => (double)GetValue(CornerRadiusPxProperty); set => SetValue(CornerRadiusPxProperty, value); }
    /// <summary>Corner smoothing in percent (0 = Windows 11, 100 = macOS squircle).</summary>
    public double CornerSmoothing { get => (double)GetValue(CornerSmoothingProperty); set => SetValue(CornerSmoothingProperty, value); }

    private static void OnCornersChanged(DependencyObject d, DependencyPropertyChangedEventArgs e) =>
        ((GeniePreview)d).ApplyTheme();

    // The scene is H units tall; its width follows the card's aspect ratio so the
    // miniature desktop always fills it.
    private const double H = 250;
    private const double TaskbarHeight = 32;
    private const double TaskbarTop = H - TaskbarHeight;
    private const double IconSize = 20, IconGap = 9;
    private const int IconCount = 5, TargetIcon = 2;
    private const double WinW = 224, WinH = 150;
    private GenieMath.Rect _winRect;
    private double _dockX;

    private enum Phase { Shown, Minimizing, Hidden, Restoring }

    private readonly Grid _scene = new() { Height = H };
    private readonly Viewport3D _view = new() { Height = H, IsHitTestVisible = false };
    private readonly OrthographicCamera _camera = new() { LookDirection = new Vector3D(0, 0, -1), UpDirection = new Vector3D(0, 1, 0) };
    private readonly Canvas _icons = new();
    private readonly List<Border> _iconTiles = new();
    private readonly Border _wallpaper = new();
    private readonly Border _taskbar = new();
    private readonly Rectangle _indicator = new() { Width = 14, Height = 3, RadiusX = 1.5, RadiusY = 1.5 };
    private readonly MeshGeometry3D _mesh = new();
    private readonly ImageBrush _texture = new();
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private Phase _phase = Phase.Shown;
    private double _phaseStart;
    private double _progress;   // raw 0..1 within the current phase
    private int _meshTiles;
    // What the scene shows now: frames that change nothing are not drawn again (the
    // settings window is a layered window, every drawn frame costs a full-window copy).
    private double _shownT = double.NaN;
    private int _shownStyle = -1;
    private bool _running;
    private bool _started;

    public GeniePreview()
    {
        ClipToBounds = true;
        Cursor = Cursors.Hand;
        ToolTip = "Kliknij w trakcie animacji, żeby ją odwrócić";

        _scene.Children.Add(_wallpaper);

        _taskbar.Height = TaskbarHeight;
        _taskbar.VerticalAlignment = VerticalAlignment.Bottom;
        string[][] palette =
        {
            new[] { "#FF5AC8FA", "#FF007AFF" }, new[] { "#FFFFD60A", "#FFFF9F0A" }, new[] { "#FFE5F1FF", "#FF8EC5FF" },
            new[] { "#FF7BE495", "#FF30B850" }, new[] { "#FFD0A2FF", "#FF8E5CF7" },
        };
        for (int i = 0; i < IconCount; i++)
        {
            var icon = new Border
            {
                Width = IconSize,
                Height = IconSize,
                CornerRadius = new CornerRadius(5),
                Background = new LinearGradientBrush(Hex(palette[i][0]), Hex(palette[i][1]), 90),
            };
            Canvas.SetTop(icon, (TaskbarHeight - IconSize) / 2 - 1);
            _icons.Children.Add(icon);
            _iconTiles.Add(icon);
        }
        Canvas.SetTop(_indicator, TaskbarHeight - 4);
        _icons.Children.Add(_indicator);
        _taskbar.Child = _icons;
        _scene.Children.Add(_taskbar);

        _view.Camera = _camera;
        RenderOptions.SetBitmapScalingMode(_texture, BitmapScalingMode.HighQuality);
        var material = new DiffuseMaterial(_texture);
        _view.Children.Add(new ModelVisual3D { Content = new AmbientLight(Colors.White) });
        _view.Children.Add(new ModelVisual3D { Content = new GeometryModel3D(_mesh, material) { BackMaterial = material } });
        _scene.Children.Add(_view);

        Children.Add(new Viewbox { Stretch = Stretch.Uniform, Child = _scene });
        Layout(460);
        SizeChanged += (_, _) =>
        {
            if (ActualHeight > 0) Layout(Math.Max(380, ActualWidth * H / ActualHeight));
        };

        ApplyTheme();
        Theme.Changed += ApplyTheme;
        MouseLeftButtonUp += (_, _) => Reverse();
        IsVisibleChanged += (_, _) => UpdateRunning();
        Loaded += (_, _) => UpdateRunning();
        Unloaded += (_, _) => UpdateRunning(force: false);
    }

    private void Layout(double w)
    {
        _scene.Width = w;
        _view.Width = w;
        _camera.Position = new Point3D(w / 2, H / 2, 10);
        _camera.Width = w;
        double startX = (w - (IconCount * IconSize + (IconCount - 1) * IconGap)) / 2;
        for (int i = 0; i < _iconTiles.Count; i++) Canvas.SetLeft(_iconTiles[i], startX + i * (IconSize + IconGap));
        _dockX = startX + TargetIcon * (IconSize + IconGap) + IconSize / 2;
        _winRect = new GenieMath.Rect((w - WinW) / 2 - 20, 24, WinW, WinH);
        _shownT = double.NaN;   // the mesh depends on the layout
        Canvas.SetLeft(_indicator, _dockX - _indicator.Width / 2);
    }

    private void UpdateRunning(bool force = true)
    {
        bool run = force && IsLoaded && IsVisible;
        if (run == _running) return;
        _running = run;
        if (run) _started = false;   // coming back to the page restarts the loop
        if (run) CompositionTarget.Rendering += OnFrame;
        else CompositionTarget.Rendering -= OnFrame;
    }

    /// <summary>Turns the genie around from where it is (or starts it when at rest).</summary>
    private void Reverse()
    {
        double now = _clock.Elapsed.TotalMilliseconds;
        switch (_phase)
        {
            case Phase.Shown: Enter(Phase.Minimizing, now, 0); break;
            case Phase.Hidden: Enter(Phase.Restoring, now, 0); break;
            case Phase.Minimizing: Enter(Phase.Restoring, now, 1 - _progress); break;
            case Phase.Restoring: Enter(Phase.Minimizing, now, 1 - _progress); break;
        }
    }

    private void Enter(Phase phase, double now, double startProgress)
    {
        _phase = phase;
        _phaseStart = now - startProgress * DurationMs;
    }

    private void OnFrame(object? sender, EventArgs e)
    {
        double now = _clock.Elapsed.TotalMilliseconds;
        if (!_started)
        {
            // The loop starts with the first frame actually shown, not at construction.
            _started = true;
            Enter(Phase.Shown, now, 0);
        }
        double elapsed = now - _phaseStart;
        double duration = Math.Max(50, DurationMs);
        int style = GenieStyle;
        double t;
        // The loop waits at rest while the window is in the background; a click still
        // turns the genie around.
        bool loop = Window.GetWindow(this)?.IsActive ?? true;
        switch (_phase)
        {
            case Phase.Shown:
                t = 0;
                if (elapsed > 1200 && loop) Enter(Phase.Minimizing, now, 0);
                break;
            case Phase.Minimizing:
                _progress = Math.Min(1, elapsed / duration);
                t = GenieMath.Ease(style, _progress);
                if (_progress >= 1) Enter(Phase.Hidden, now, 0);
                break;
            case Phase.Hidden:
                t = 1;
                if (elapsed > 750 && loop) Enter(Phase.Restoring, now, 0);
                break;
            default:
                _progress = Math.Min(1, elapsed / duration);
                t = 1 - GenieMath.Ease(style, _progress);
                if (_progress >= 1) Enter(Phase.Shown, now, 0);
                break;
        }

        int tiles = (int)Math.Clamp(Tiles, 8, 32);
        if (tiles != _meshTiles)
        {
            BuildMesh(tiles);
            _shownT = double.NaN;
        }
        if (t != _shownT || style != _shownStyle)
        {
            _shownT = t;
            _shownStyle = style;
            UpdateMesh(style, t);
            _texture.Opacity = _phase == Phase.Hidden ? 0 : GenieMath.Fade(style, t);
        }

        // The running-app dash under the icon is wide while the window is on screen.
        double target = t < 0.5 ? 14 : 6;
        if (_indicator.Width != target)
        {
            double w = _indicator.Width + (target - _indicator.Width) * 0.18;
            _indicator.Width = Math.Abs(target - w) < 0.05 ? target : w;
            Canvas.SetLeft(_indicator, _dockX - _indicator.Width / 2);
        }
    }

    private void BuildMesh(int n)
    {
        _meshTiles = n;
        var tex = new PointCollection((n + 1) * (n + 1));
        for (int y = 0; y <= n; y++)
            for (int x = 0; x <= n; x++)
                tex.Add(new Point((double)x / n, (double)y / n));
        var idx = new Int32Collection(n * n * 6);
        for (int y = 0; y < n; y++)
        {
            for (int x = 0; x < n; x++)
            {
                int a = y * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
                idx.Add(a); idx.Add(c); idx.Add(b);
                idx.Add(b); idx.Add(c); idx.Add(d);
            }
        }
        tex.Freeze();
        idx.Freeze();
        _mesh.TextureCoordinates = tex;
        _mesh.TriangleIndices = idx;
    }

    private void UpdateMesh(int style, double t)
    {
        var dock = style == 1
            ? new GenieMath.Rect(_dockX, H, 6, 0)
            : new GenieMath.Rect(_dockX - 9, TaskbarTop, 18, 24);
        int n = _meshTiles;
        var pts = new Point3DCollection((n + 1) * (n + 1));
        for (int y = 0; y <= n; y++)
        {
            for (int x = 0; x <= n; x++)
            {
                var (px, py) = GenieMath.Vertex(style, (double)x / n, (double)y / n, t, _winRect, dock);
                pts.Add(new Point3D(px, H - py, 0));
            }
        }
        pts.Freeze();
        _mesh.Positions = pts;
    }

    private void ApplyTheme()
    {
        bool dark = Theme.IsDark;
        _wallpaper.Background = dark
            ? Gradient("#FF1C2B4D", "#FF3A2C5E", "#FF5C2F4F")
            : Gradient("#FF8DB9F5", "#FFB7A6EC", "#FFF3C4D6");
        _taskbar.Background = new SolidColorBrush(dark ? Hex("#D9202022") : Hex("#D9F4F4F6"));
        _indicator.Fill = new SolidColorBrush(dark ? Hex("#FFB0B0B5") : Hex("#FF6E6E73"));
        _texture.ImageSource = RenderWindow(dark, CornerRadiusPx * CornerScale, CornerSmoothing / 100);
    }

    // The miniature is about a quarter of a real window; corners at this scale read like
    // the real ones (Windows 11's 8 px used to be drawn as 9 here).
    private const double CornerScale = 0.75;

    // The window shown in the preview, rendered into a bitmap (2x for sharpness) whenever
    // the theme or the corner shape changes. The corners clip it like the renderer does.
    private static BitmapSource RenderWindow(bool dark, double radius, double smoothing)
    {
        double w = WinW, h = WinH;
        Geometry shape = Squircle.Create(w, h, radius, smoothing);
        var body = new Border
        {
            Width = w,
            Height = h,
            Background = new SolidColorBrush(dark ? Hex("#FF2B2B2E") : Colors.White),
            Clip = shape,
        };
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(24) });
        grid.RowDefinitions.Add(new RowDefinition());
        var bar = new Border
        {
            Background = new SolidColorBrush(dark ? Hex("#FF353538") : Hex("#FFF2F2F4")),
        };
        var title = new Grid();
        var dots = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(9, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        foreach (var c in new[] { "#FFFF5F57", "#FFFEBC2E", "#FF28C840" })
            dots.Children.Add(new Ellipse { Width = 8, Height = 8, Margin = new Thickness(0, 0, 5, 0), Fill = new SolidColorBrush(Hex(c)) });
        title.Children.Add(dots);
        title.Children.Add(new TextBlock
        {
            Text = "Notatnik",
            FontSize = 9.5,
            FontWeight = FontWeights.SemiBold,
            Foreground = new SolidColorBrush(dark ? Hex("#FFE5E5EA") : Hex("#FF3A3A3C")),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        });
        bar.Child = title;
        grid.Children.Add(bar);
        var lines = new StackPanel { Margin = new Thickness(14, 12, 14, 0) };
        foreach (double lw in new[] { 0.72, 0.9, 0.55, 0.84, 0.4, 0.78, 0.62 })
        {
            lines.Children.Add(new Rectangle
            {
                Height = 5,
                Width = (w - 28) * lw,
                RadiusX = 2.5,
                RadiusY = 2.5,
                HorizontalAlignment = HorizontalAlignment.Left,
                Margin = new Thickness(0, 0, 0, 8),
                Fill = new SolidColorBrush(dark ? Hex("#38FFFFFF") : Hex("#1C000000")),
            });
        }
        Grid.SetRow(lines, 1);
        grid.Children.Add(lines);
        body.Child = grid;
        var window = new Grid { Width = w, Height = h };
        window.Children.Add(body);
        window.Children.Add(new Path
        {
            Data = shape,
            Stroke = new SolidColorBrush(dark ? Hex("#40FFFFFF") : Hex("#26000000")),
            StrokeThickness = 1.2,   // clipped to its inner half, like the old 0.6 border
            Clip = shape,
        });

        window.Measure(new Size(w, h));
        window.Arrange(new Rect(0, 0, w, h));
        var bmp = new RenderTargetBitmap((int)(w * 2), (int)(h * 2), 192, 192, PixelFormats.Pbgra32);
        bmp.Render(window);
        bmp.Freeze();
        return bmp;
    }

    private static LinearGradientBrush Gradient(string a, string b, string c)
    {
        var g = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 1) };
        g.GradientStops.Add(new GradientStop(Hex(a), 0));
        g.GradientStops.Add(new GradientStop(Hex(b), 0.55));
        g.GradientStops.Add(new GradientStop(Hex(c), 1));
        g.Freeze();
        return g;
    }

    private static Color Hex(string s) => (Color)ColorConverter.ConvertFromString(s);
}
