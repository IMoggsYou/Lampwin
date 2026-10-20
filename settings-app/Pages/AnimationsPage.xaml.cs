using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

public partial class AnimationsPage : UserControl
{
    public AnimationsPage()
    {
        InitializeComponent();
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
        MessageBox.Show(Window.GetWindow(this), "MacWin nie działa. Uruchom go na karcie „Ogólne”.", "MacWin",
                        MessageBoxButton.OK, MessageBoxImage.Information);
        return false;
    }
}
