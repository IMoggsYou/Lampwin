using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;

namespace MacWin.Settings.Controls;

/// <summary>Editable list of program names (matched like the mod: full path, file name, or
/// file name without ".exe"; always lowercase).</summary>
public partial class ProgramList : UserControl
{
    public static readonly DependencyProperty ItemsProperty = DependencyProperty.Register(
        nameof(Items), typeof(ObservableCollection<string>), typeof(ProgramList),
        new PropertyMetadata(null, (d, e) => ((ProgramList)d).OnItemsChanged(e)));

    public ObservableCollection<string>? Items
    {
        get => (ObservableCollection<string>?)GetValue(ItemsProperty);
        set => SetValue(ItemsProperty, value);
    }

    public ProgramList()
    {
        InitializeComponent();
    }

    private void OnItemsChanged(DependencyPropertyChangedEventArgs e)
    {
        if (e.OldValue is INotifyCollectionChanged oldList) oldList.CollectionChanged -= OnCollectionChanged;
        if (e.NewValue is INotifyCollectionChanged newList) newList.CollectionChanged += OnCollectionChanged;
        UpdateEmpty();
    }

    private void OnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e) => UpdateEmpty();

    private void UpdateEmpty() => EmptyText.Visibility = Items is { Count: > 0 } ? Visibility.Collapsed : Visibility.Visible;

    private void Add(string raw)
    {
        if (Items is null) return;
        string name = raw.Trim().Trim('"').ToLowerInvariant();
        if (name.Length == 0) return;
        if (!Items.Contains(name)) Items.Add(name);
        List.SelectedItem = name;
        List.ScrollIntoView(name);
    }

    private void AddName_Click(object sender, RoutedEventArgs e)
    {
        Add(NameBox.Text);
        NameBox.Clear();
    }

    private void NameBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        Add(NameBox.Text);
        NameBox.Clear();
        e.Handled = true;
    }

    private void AddFromFile_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "Wybierz program",
            Filter = "Programy (*.exe)|*.exe",
            InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
        };
        if (dialog.ShowDialog(Window.GetWindow(this)) == true) Add(Path.GetFileName(dialog.FileName));
    }

    private void Remove_Click(object sender, RoutedEventArgs e) => RemoveSelected();

    private void List_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is Key.Delete or Key.Back) RemoveSelected();
    }

    private void RemoveSelected()
    {
        if (Items is null || List.SelectedItem is not string name) return;
        int index = List.SelectedIndex;
        Items.Remove(name);
        if (Items.Count > 0) List.SelectedIndex = Math.Min(index, Items.Count - 1);
    }

    private void List_SelectionChanged(object sender, SelectionChangedEventArgs e) =>
        RemoveButton.IsEnabled = List.SelectedItem is not null;
}
