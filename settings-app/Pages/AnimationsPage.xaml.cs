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
        if (!HostControl.IsRunning)
        {
            MessageBox.Show(Window.GetWindow(this), "MacWin nie działa. Uruchom go na karcie „Ogólne”.", "MacWin",
                            MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        HostControl.TryOnRealWindow();
    }
}
