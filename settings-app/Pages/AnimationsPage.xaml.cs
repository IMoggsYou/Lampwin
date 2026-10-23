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
        MessageBox.Show(Window.GetWindow(this), Loc.T("settings.animations.notRunning"), "MacWin",
                        MessageBoxButton.OK, MessageBoxImage.Information, MessageBoxResult.OK,
                        Loc.IsRightToLeft ? MessageBoxOptions.RtlReading | MessageBoxOptions.RightAlign : MessageBoxOptions.None);
        return false;
    }
}
