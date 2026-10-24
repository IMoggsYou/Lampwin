// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

using System.Collections.Specialized;
using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

public partial class ExclusionsPage : UserControl
{
    private SettingsModel? _model;

    public ExclusionsPage()
    {
        InitializeComponent();
        DataContextChanged += (_, _) => Watch(DataContext as SettingsModel);
        Loaded += (_, _) => Watch(DataContext as SettingsModel);
        Unloaded += (_, _) => Watch(null);
    }

    private void Watch(SettingsModel? model)
    {
        if (_model is not null) _model.NoInject.CollectionChanged -= NoInject_CollectionChanged;
        _model = model;
        if (_model is not null) _model.NoInject.CollectionChanged += NoInject_CollectionChanged;
        UpdateRestore();
    }

    private void NoInject_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e) => UpdateRestore();

    // Only while one of the protected games is missing from the list.
    private void UpdateRestore() => RestoreDefaults.IsEnabled = _model is not null && Missing(_model).Any();

    private static IEnumerable<string> Missing(SettingsModel model) =>
        SettingsStore.DefaultNoInject.Where(d => !model.NoInject.Contains(d, StringComparer.OrdinalIgnoreCase));

    // Adds the default entries that are missing; what the user added stays.
    private void RestoreDefaults_Click(object sender, RoutedEventArgs e)
    {
        if (_model is null) return;
        foreach (string entry in Missing(_model).ToList()) _model.NoInject.Add(entry);
    }
}
