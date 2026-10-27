// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

public partial class AnimationsPage : UserControl
{
    private SettingsModel? _model;

    public AnimationsPage()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            _model = DataContext as SettingsModel;
            if (_model is not null) _model.PropertyChanged += Model_PropertyChanged;
        };
        Unloaded += (_, _) =>
        {
            if (_model is not null) _model.PropertyChanged -= Model_PropertyChanged;
            _model = null;
        };
    }

    // Choosing Shatter, Fragments or Zoom shows it right away in the preview: it can't be
    // guessed from a word.
    private void Model_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (_model is null || _model.SuppressSideEffects) return;
        bool opening = e.PropertyName == nameof(SettingsModel.OpenEffect);
        if (!opening && e.PropertyName != nameof(SettingsModel.CloseEffect)) return;
        switch (SettingsModel.EffectNames[opening ? _model.OpenEffect : _model.CloseEffect])
        {
            case "shatter": Preview.PlayShatter(fragments: false, join: opening); break;
            case "fragments": Preview.PlayShatter(fragments: true, join: opening); break;
            case "zoom": Preview.PlayZoom(open: opening); break;
        }
    }

    private void TryReal_Click(object sender, RoutedEventArgs e)
    {
        if (EnsureRunning()) HostControl.TryOnRealWindow();
    }

    private void TryOpenClose_Click(object sender, RoutedEventArgs e)
    {
        if (EnsureRunning()) HostControl.TryOpenClose();
    }

    private bool EnsureRunning()
    {
        if (HostControl.IsRunning) return true;
        MessageBox.Show(Window.GetWindow(this), Loc.T("settings.animations.notRunning"), "MacWin",
                        MessageBoxButton.OK, MessageBoxImage.Information, MessageBoxResult.OK,
                        Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign : MessageBoxOptions.None);
        return false;
    }
}
