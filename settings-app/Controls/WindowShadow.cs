// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/IMoggsYou/MacWin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace MacWin.Settings.Controls;

/// <summary>
/// The settings window's shadow. That window is transparent (layered) and cut to a
/// squircle, which leaves it without DWM's shadow; this click-through window, kept right
/// under it in the z-order, draws a macOS-like one instead: a broad soft shadow and a tight
/// contact shadow. Being a separate window, a click on the shadow reaches whatever is
/// behind it. Hidden while the window is minimized or maximized.
/// </summary>
public sealed class WindowShadow : Window
{
    private const double Spread = 60;   // DIPs around the window for the blur
    private readonly Window _target;
    private readonly Grid _layers = new() { Margin = new Thickness(Spread) };
    private readonly Path _broad = new(), _tight = new();
    private readonly DropShadowEffect _broadFx = new()
    {
        BlurRadius = 56, ShadowDepth = 18, Direction = 270, Color = Colors.Black, RenderingBias = RenderingBias.Quality,
    };
    private readonly DropShadowEffect _tightFx = new()
    {
        BlurRadius = 7, ShadowDepth = 1.5, Direction = 270, Color = Colors.Black, RenderingBias = RenderingBias.Quality,
    };
    private readonly DispatcherTimer _waitShown = new() { Interval = TimeSpan.FromMilliseconds(15) };
    private double _radius, _smoothing;
    private IntPtr _hwnd, _targetHwnd;
    private DateTime _waitUntil;
    private Size _shapeSize;

    public WindowShadow(Window target, double cornerRadius, double smoothing)
    {
        _target = target;
        _radius = cornerRadius;
        _smoothing = smoothing;
        WindowStyle = WindowStyle.None;
        AllowsTransparency = true;
        Background = Brushes.Transparent;
        ResizeMode = ResizeMode.NoResize;
        ShowInTaskbar = false;
        ShowActivated = false;
        Focusable = false;
        IsHitTestVisible = false;
        Title = "";
        _broad.Effect = _broadFx;
        _tight.Effect = _tightFx;
        _layers.Children.Add(_broad);
        _layers.Children.Add(_tight);
        Content = _layers;
        SourceInitialized += (_, _) =>
        {
            _hwnd = new WindowInteropHelper(this).Handle;
            Native.MakeOverlay(_hwnd);
        };
        _waitShown.Tick += (_, _) => ShowWhenTargetIsOnScreen();
        ApplyTheme();
        Theme.Changed += ApplyTheme;
    }

    /// <summary>Shows the shadow with the window (fading in over `fade`) once it has a handle.</summary>
    public void Attach(TimeSpan fade)
    {
        _targetHwnd = new WindowInteropHelper(_target).Handle;
        if (!ShouldShow()) return;
        _layers.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, fade));
        Show();
        Sync();
    }

    /// <summary>New corners (radius in DIPs, smoothing 0..1): the shadow takes their shape.</summary>
    public void SetCorners(double radius, double smoothing)
    {
        _radius = radius;
        _smoothing = smoothing;
        _shapeSize = default;
        Sync();
    }

    /// <summary>Follows the window's position, size and z-order (call on every change).</summary>
    public void Sync()
    {
        if (_hwnd == IntPtr.Zero || _targetHwnd == IntPtr.Zero || !IsVisible) return;
        if (!Native.GetWindowRect(_targetHwnd, out var r)) return;
        var size = new Size(_target.ActualWidth, _target.ActualHeight);
        if (size != _shapeSize && size.Width > 0)
        {
            _shapeSize = size;
            var shape = Squircle.Create(size.Width, size.Height, _radius, _smoothing);
            _broad.Data = shape;
            _tight.Data = shape;
        }
        int m = (int)Math.Round(Spread * VisualTreeHelper.GetDpi(_target).DpiScaleX);
        Native.PlaceUnder(_hwnd, _targetHwnd, new Native.RECT
        {
            Left = r.Left - m, Top = r.Top - m, Right = r.Right + m, Bottom = r.Bottom + m,
        });
    }

    /// <summary>After a window state change: hide, or show again once the window really is
    /// on screen (MacWin keeps a restored window cloaked until its genie has landed).</summary>
    public void UpdateVisibility()
    {
        _waitShown.Stop();
        if (!ShouldShow())
        {
            Hide();
            return;
        }
        _waitUntil = DateTime.UtcNow.AddSeconds(3);
        ShowWhenTargetIsOnScreen();
    }

    private void ShowWhenTargetIsOnScreen()
    {
        if (!ShouldShow())
        {
            _waitShown.Stop();
            return;
        }
        if (Native.IsCloaked(_targetHwnd) && DateTime.UtcNow < _waitUntil)
        {
            _waitShown.Start();
            return;
        }
        _waitShown.Stop();
        if (!IsVisible) Show();
        Sync();
    }

    private bool ShouldShow() =>
        _targetHwnd != IntPtr.Zero && _target.IsVisible && _target.WindowState == WindowState.Normal;

    private void ApplyTheme()
    {
        bool dark = Theme.IsDark;
        var fill = new SolidColorBrush(Theme.Color("WindowBase"));
        fill.Freeze();
        _broad.Fill = fill;
        _tight.Fill = fill;
        _broadFx.Opacity = dark ? 0.55 : 0.28;
        _tightFx.Opacity = dark ? 0.6 : 0.22;
    }
}
