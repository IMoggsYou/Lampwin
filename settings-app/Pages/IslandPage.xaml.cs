// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

/// <summary>The island at the top of the screen (shown by MacWin.exe; this page only edits
/// settings.json). Options for hardware this PC doesn't have are left out.</summary>
public partial class IslandPage : UserControl
{
    public IslandPage()
    {
        InitializeComponent();
        bool battery = IslandCapabilities.Battery, wireless = IslandCapabilities.Wireless,
             cellular = IslandCapabilities.Cellular;
        BatteryRows.Visibility = battery ? Visibility.Visible : Visibility.Collapsed;
        AirplaneRow.Visibility = wireless ? Visibility.Visible : Visibility.Collapsed;
        MobileDataRow.Visibility = cellular ? Visibility.Visible : Visibility.Collapsed;
        // The note only where something was left out.
        HardwareNote.Visibility = battery && wireless && cellular ? Visibility.Collapsed : Visibility.Visible;
    }
}
