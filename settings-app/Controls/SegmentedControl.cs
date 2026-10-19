using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;

namespace MacWin.Settings.Controls;

/// <summary>macOS segmented control: the white "pill" slides to the chosen segment.</summary>
public sealed class SegmentedControl : Border
{
    public static readonly DependencyProperty SelectedIndexProperty = DependencyProperty.Register(
        nameof(SelectedIndex), typeof(int), typeof(SegmentedControl),
        new FrameworkPropertyMetadata(0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            (d, _) => ((SegmentedControl)d).UpdatePill(animate: true)));

    public static readonly DependencyProperty ItemsProperty = DependencyProperty.Register(
        nameof(Items), typeof(string), typeof(SegmentedControl),
        new PropertyMetadata("", (d, _) => ((SegmentedControl)d).Build()));

    private readonly Grid _grid = new();
    private readonly Border _pill;
    private readonly TranslateTransform _pillShift = new();
    private readonly List<TextBlock> _labels = new();

    public int SelectedIndex
    {
        get => (int)GetValue(SelectedIndexProperty);
        set => SetValue(SelectedIndexProperty, value);
    }

    /// <summary>Comma-separated segment titles.</summary>
    public string Items
    {
        get => (string)GetValue(ItemsProperty);
        set => SetValue(ItemsProperty, value);
    }

    public SegmentedControl()
    {
        CornerRadius = new CornerRadius(7);
        Padding = new Thickness(2);
        Height = 28;
        SetResourceReference(BackgroundProperty, "Brush.SegmentBg");
        _pill = new Border
        {
            CornerRadius = new CornerRadius(5.5),
            HorizontalAlignment = HorizontalAlignment.Left,
            RenderTransform = _pillShift,
            Effect = new DropShadowEffect { BlurRadius = 4, ShadowDepth = 1, Direction = 270, Opacity = 0.16 },
        };
        _pill.SetResourceReference(Border.BackgroundProperty, "Brush.SegmentPill");
        _grid.Children.Add(_pill);
        Child = _grid;
        SizeChanged += (_, _) => UpdatePill(animate: false);
    }

    private void Build()
    {
        foreach (var label in _labels) _grid.Children.Remove((UIElement)label.Parent);
        _labels.Clear();
        _grid.ColumnDefinitions.Clear();
        var titles = Items.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        for (int i = 0; i < titles.Length; i++)
        {
            _grid.ColumnDefinitions.Add(new ColumnDefinition());
            var text = new TextBlock
            {
                Text = titles[i],
                FontSize = 12.5,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
            };
            var hit = new Border { Background = Brushes.Transparent, Child = text, Cursor = Cursors.Hand };
            int index = i;
            hit.MouseLeftButtonUp += (_, _) => SelectedIndex = index;
            Grid.SetColumn(hit, i);
            _grid.Children.Add(hit);
            _labels.Add(text);
        }
        Grid.SetColumnSpan(_pill, Math.Max(1, titles.Length));
        UpdatePill(animate: false);
    }

    private void UpdatePill(bool animate)
    {
        int count = Math.Max(1, _labels.Count);
        double inner = ActualWidth - Padding.Left - Padding.Right;
        if (inner <= 0) return;
        double segment = inner / count;
        _pill.Width = segment;
        double x = segment * Math.Clamp(SelectedIndex, 0, count - 1);
        if (animate)
        {
            var anim = new DoubleAnimation(x, TimeSpan.FromMilliseconds(280))
            {
                EasingFunction = new QuarticEase { EasingMode = EasingMode.EaseOut },
            };
            _pillShift.BeginAnimation(TranslateTransform.XProperty, anim, HandoffBehavior.SnapshotAndReplace);
        }
        else
        {
            _pillShift.BeginAnimation(TranslateTransform.XProperty, null);
            _pillShift.X = x;
        }
        for (int i = 0; i < _labels.Count; i++)
        {
            _labels[i].SetResourceReference(TextBlock.ForegroundProperty, i == SelectedIndex ? "Brush.Text" : "Brush.TextSecondary");
            _labels[i].FontWeight = i == SelectedIndex ? FontWeights.SemiBold : FontWeights.Normal;
        }
    }
}
