// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace MacWin.Settings.Pages;

/// <summary>Hot corners, the window overview and the light / dark schedule (all of them done
/// by MacWin.exe; this page only edits settings.json).</summary>
public partial class DesktopPage : UserControl
{
    private const string OverviewMessage = "MacWin_Overview_v1";

    public DesktopPage()
    {
        InitializeComponent();
        foreach (var box in new[] { TopLeftBox, TopRightBox, BottomLeftBox, BottomRightBox })
        {
            foreach (string action in SettingsModel.CornerActionNames)
                box.Items.Add(new ComboBoxItem { Content = Loc.T($"settings.desktop.action.{action}") });
        }
    }

    private SettingsModel? Model => DataContext as SettingsModel;

    private void ShowOverview_Click(object sender, RoutedEventArgs e)
    {
        if (!HostControl.Post(OverviewMessage, 0)) HostControl.Start();
    }

    private void ChooseDay_Click(object sender, RoutedEventArgs e)
    {
        if (Model is { } m && PickPicture(m.DayWallpaper) is string path) m.DayWallpaper = path;
    }

    private void ChooseNight_Click(object sender, RoutedEventArgs e)
    {
        if (Model is { } m && PickPicture(m.NightWallpaper) is string path) m.NightWallpaper = path;
    }

    // The picture formats Windows takes as a wallpaper.
    private string? PickPicture(string current)
    {
        var dialog = new OpenFileDialog
        {
            Filter = $"{Loc.T("settings.desktop.wallpaper.filter")}|*.jpg;*.jpeg;*.png;*.bmp",
            InitialDirectory = current.Length > 0 ? System.IO.Path.GetDirectoryName(current) : null,
        };
        return dialog.ShowDialog(Window.GetWindow(this)) == true ? dialog.FileName : null;
    }
}
