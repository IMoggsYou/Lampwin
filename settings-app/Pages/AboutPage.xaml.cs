using System.Windows;
using System.Windows.Controls;

namespace MacWin.Settings.Pages;

public partial class AboutPage : UserControl
{
    public AboutPage()
    {
        InitializeComponent();
        VersionText.Text = Loc.F("settings.about.version", Project.Version);
        AuthorText.Text = Loc.F("settings.about.author", Project.Author);
    }

    private void Author_Click(object sender, RoutedEventArgs e) => HostControl.OpenUrl("https://github.com/" + Project.Author);

    private void Repo_Click(object sender, RoutedEventArgs e) => HostControl.OpenUrl(Project.RepoUrl);

    private void Changelog_Click(object sender, RoutedEventArgs e) => HostControl.OpenUrl(Project.ChangelogUrl);

    private void Issues_Click(object sender, RoutedEventArgs e) => HostControl.OpenUrl(Project.IssuesUrl);

    private void License_Click(object sender, RoutedEventArgs e) => HostControl.OpenUrl(Project.LicenseUrl);

    private void OpenLog_Click(object sender, RoutedEventArgs e) => HostControl.Open(HostControl.LogPath);

    private void OpenFolder_Click(object sender, RoutedEventArgs e) => HostControl.Open(SettingsStore.Folder);
}
