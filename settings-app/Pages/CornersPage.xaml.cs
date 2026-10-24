// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MacWin.Settings.Controls;

namespace MacWin.Settings.Pages;

public partial class CornersPage : UserControl
{
    private SettingsModel? _model;

    public CornersPage()
    {
        InitializeComponent();
        DataContextChanged += (_, _) =>
        {
            if (_model is not null) _model.PropertyChanged -= Model_PropertyChanged;
            _model = DataContext as SettingsModel;
            if (_model is not null) _model.PropertyChanged += Model_PropertyChanged;
            UpdatePreview();
        };
        Unloaded += (_, _) =>
        {
            if (_model is not null) _model.PropertyChanged -= Model_PropertyChanged;
        };
        Loaded += (_, _) =>
        {
            if (_model is not null)
            {
                _model.PropertyChanged -= Model_PropertyChanged;
                _model.PropertyChanged += Model_PropertyChanged;
            }
            UpdatePreview();
        };
    }

    private void Model_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(SettingsModel.CornerRadius) or nameof(SettingsModel.CornerSmoothing)) UpdatePreview();
    }

    private void Preview_SizeChanged(object sender, SizeChangedEventArgs e) => UpdatePreview();

    // A window in the middle of the preview, with the chosen corners, title bar and shadow.
    private void UpdatePreview()
    {
        double w = PreviewBackdrop.ActualWidth, h = PreviewBackdrop.ActualHeight;
        if (w <= 0 || h <= 0 || _model is null) return;
        const double ww = 300, wh = 140, title = 30;
        double x = Math.Round((w - ww) / 2), y = Math.Round((h - wh) / 2);
        var shape = Squircle.Create(ww, wh, _model.CornerRadius, _model.CornerSmoothing / 100);
        var placed = shape.Clone();
        placed.Transform = new TranslateTransform(x, y);
        placed.Freeze();
        PreviewShadow.Data = placed;
        PreviewWindow.Data = placed;
        var bar = new CombinedGeometry(GeometryCombineMode.Intersect, shape, new RectangleGeometry(new Rect(0, 0, ww, title)))
        {
            Transform = new TranslateTransform(x, y),
        };
        bar.Freeze();
        PreviewTitle.Data = bar;
    }
}
