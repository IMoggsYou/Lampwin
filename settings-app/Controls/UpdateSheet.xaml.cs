// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace MacWin.Settings.Controls;

/// <summary>
/// "MacWin X is available": the new version, a few lines of what changed and, folded
/// away, the whole release notes; Update now / Not now / Don't show this again.
/// </summary>
public partial class UpdateSheet : UserControl
{
    private const int SummaryItems = 4;
    private const double MaxNotesHeight = 320;
    private static readonly TimeSpan Fold = TimeSpan.FromMilliseconds(340);

    private UpdateState? _update;
    private double _summaryHeight;

    /// <summary>The sheet went away: true when the user chose to update.</summary>
    public event Action<bool>? Finished;

    public bool IsOpen { get; private set; }

    public bool DontShowAgain => DontShow.IsChecked == true;

    public UpdateSheet()
    {
        InitializeComponent();
    }

    internal void Show(UpdateState update)
    {
        _update = update;
        var notes = ReleaseNotes.Parse(update.Notes);
        Heading.Text = Loc.F("settings.update.title", update.Latest);
        Subheading.Text = Loc.F("settings.update.subtitle", update.Current ?? Project.Version);
        FillSummary(notes);
        FillNotes(notes);
        NotesToggle.Visibility = notes.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        NotesToggle.IsChecked = false;
        NotesToggle.Content = Loc.T("settings.update.showNotes");
        SummaryBox.BeginAnimation(HeightProperty, null);
        NotesBox.BeginAnimation(HeightProperty, null);
        NotesBox.BeginAnimation(OpacityProperty, null);
        DontShow.IsChecked = false;

        IsOpen = true;
        Visibility = Visibility.Visible;
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        Dim.BeginAnimation(OpacityProperty, new DoubleAnimation(0, Theme.IsDark ? 0.38 : 0.18, TimeSpan.FromMilliseconds(240)));
        Sheet.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(180)));
        SheetShift.BeginAnimation(TranslateTransform.YProperty,
                                  new DoubleAnimation(-30, 0, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease });
        var grow = new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease };
        SheetScale.BeginAnimation(ScaleTransform.ScaleXProperty, grow);
        SheetScale.BeginAnimation(ScaleTransform.ScaleYProperty, grow);
        Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Input, () => InstallButton.Focus());
    }

    /// <summary>Opens the full changelog (also for --snapshot).</summary>
    public void ShowNotes() => NotesToggle.IsChecked = true;

    private void Close(bool install)
    {
        if (!IsOpen) return;
        IsOpen = false;
        var ease = new CubicEase { EasingMode = EasingMode.EaseIn };
        var duration = TimeSpan.FromMilliseconds(200);
        var fade = new DoubleAnimation(0, duration);
        fade.Completed += (_, _) =>
        {
            if (!IsOpen) Visibility = Visibility.Collapsed;
        };
        Dim.BeginAnimation(OpacityProperty, new DoubleAnimation(0, duration));
        Sheet.BeginAnimation(OpacityProperty, fade);
        SheetShift.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-20, duration) { EasingFunction = ease });
        var shrink = new DoubleAnimation(0.97, duration) { EasingFunction = ease };
        SheetScale.BeginAnimation(ScaleTransform.ScaleXProperty, shrink);
        SheetScale.BeginAnimation(ScaleTransform.ScaleYProperty, shrink);
        Finished?.Invoke(install);
    }

    private void FillSummary(ReleaseNotes notes)
    {
        SummaryList.Children.Clear();
        if (notes.Count == 0)
        {
            SummaryList.Children.Add(new TextBlock
            {
                Text = Loc.T("settings.update.noNotes"),
                Style = (Style)FindResource("Text.Secondary"),
                FontSize = 12,
            });
            return;
        }
        foreach (string item in notes.Items.Take(SummaryItems))
            SummaryList.Children.Add(Bullet(new TextBlock { Text = ReleaseNotes.Gist(item) }, 2));
        if (notes.Count > SummaryItems)
        {
            SummaryList.Children.Add(new TextBlock
            {
                Text = Loc.F("settings.update.more", notes.Count - SummaryItems),
                Style = (Style)FindResource("Text.Secondary"),
                FontSize = 12,
                Margin = new Thickness(0, 4, 0, 0),
            });
        }
    }

    private void FillNotes(ReleaseNotes notes)
    {
        NotesList.Children.Clear();
        foreach (var section in notes.Sections)
        {
            if (section.Heading.Length > 0)
            {
                NotesList.Children.Add(new TextBlock
                {
                    Text = SectionName(section.Heading),
                    FontWeight = FontWeights.SemiBold,
                    FontSize = 12.5,
                    Margin = new Thickness(0, 10, 0, 3),
                });
            }
            foreach (string item in section.Items)
            {
                var text = new TextBlock { LineHeight = 17 };
                foreach (var (span, bold) in ReleaseNotes.Spans(item))
                    text.Inlines.Add(new Run(span) { FontWeight = bold ? FontWeights.SemiBold : FontWeights.Normal });
                NotesList.Children.Add(Bullet(text, 4));
            }
        }
    }

    // Keep a Changelog's headings in the reader's language; anything else as written.
    private static string SectionName(string heading)
    {
        string key = "settings.update.section." + heading.ToLowerInvariant();
        return heading.ToLowerInvariant() is "added" or "changed" or "deprecated" or "fixed" or "removed" or "security"
            ? Loc.T(key)
            : heading;
    }

    private static Grid Bullet(TextBlock text, double gap)
    {
        text.FontSize = 12;
        text.TextWrapping = TextWrapping.Wrap;
        var row = new Grid { Margin = new Thickness(0, gap / 2, 0, gap / 2) };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(14) });
        row.ColumnDefinitions.Add(new ColumnDefinition());
        var dot = new TextBlock { Text = "•", FontSize = 12 };
        dot.SetResourceReference(TextBlock.ForegroundProperty, "Brush.TextSecondary");
        Grid.SetColumn(text, 1);
        row.Children.Add(dot);
        row.Children.Add(text);
        return row;
    }

    // The summary folds away as the full list unfolds (and back), in one movement; the
    // list gets the room the window has under the sheet, at most MaxNotesHeight.
    private void NotesToggle_Changed(object sender, RoutedEventArgs e)
    {
        bool open = NotesToggle.IsChecked == true;
        NotesToggle.Content = Loc.T(open ? "settings.update.hideNotes" : "settings.update.showNotes");
        var ease = new QuinticEase { EasingMode = EasingMode.EaseOut };
        if (open)
        {
            UpdateLayout();
            _summaryHeight = SummaryBox.ActualHeight;
            double width = Math.Max(1, NotesScroller.ActualWidth > 0 ? NotesScroller.ActualWidth : Sheet.Width - 46);
            NotesList.Measure(new Size(width, double.PositiveInfinity));
            double natural = NotesList.DesiredSize.Height + NotesBox.Padding.Top + 2;
            double room = ActualHeight - Sheet.Margin.Top - 24 - (Sheet.ActualHeight - _summaryHeight);
            double target = Math.Min(natural, Math.Min(MaxNotesHeight, Math.Max(room, 120)));
            SummaryBox.BeginAnimation(HeightProperty, new DoubleAnimation(_summaryHeight, 0, Fold) { EasingFunction = ease });
            NotesBox.BeginAnimation(HeightProperty, new DoubleAnimation(0, target, Fold) { EasingFunction = ease });
            NotesBox.BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(260)));
            NotesScroller.ScrollToTop();
        }
        else
        {
            var back = new DoubleAnimation(0, _summaryHeight, Fold) { EasingFunction = ease };
            // Back to its own height once there (the texts may change with the language).
            back.Completed += (_, _) =>
            {
                if (NotesToggle.IsChecked != true) SummaryBox.BeginAnimation(HeightProperty, null);
            };
            SummaryBox.BeginAnimation(HeightProperty, back);
            NotesBox.BeginAnimation(HeightProperty, new DoubleAnimation(0, Fold) { EasingFunction = ease });
            NotesBox.BeginAnimation(OpacityProperty, new DoubleAnimation(0, TimeSpan.FromMilliseconds(200)));
        }
    }

    private void Sheet_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!IsOpen) return;
        if (e.Key == Key.Escape)
        {
            Close(false);
            e.Handled = true;
        }
        else if (e.Key == Key.Enter && Keyboard.FocusedElement is not ButtonBase)
        {
            Close(true);
            e.Handled = true;
        }
    }

    private void Install_Click(object sender, RoutedEventArgs e) => Close(true);

    private void Later_Click(object sender, RoutedEventArgs e) => Close(false);

    private void ReleasePage_Click(object sender, MouseButtonEventArgs e) =>
        HostControl.OpenUrl(_update?.NotesUrl ?? Project.ReleasesUrl);
}
