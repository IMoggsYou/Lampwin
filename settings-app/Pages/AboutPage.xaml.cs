using System.Reflection;
using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

public partial class AboutPage : UserControl
{
    public AboutPage()
    {
        InitializeComponent();
        var v = Assembly.GetExecutingAssembly().GetName().Version;
        VersionText.Text = $"Wersja {v?.Major}.{v?.Minor}.{v?.Build}";
    }

    private void OpenLog_Click(object sender, RoutedEventArgs e) => HostControl.Open(HostControl.LogPath);

    private void OpenFolder_Click(object sender, RoutedEventArgs e) => HostControl.Open(SettingsStore.Folder);
}
