using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace MacWin.Settings.Pages;

public partial class GeneralPage : UserControl
{
    private readonly DispatcherTimer _poll = new() { Interval = TimeSpan.FromSeconds(1) };
    private bool _busy;

    public GeneralPage()
    {
        InitializeComponent();
        _poll.Tick += (_, _) => UpdateStatus();
        Loaded += (_, _) =>
        {
            UpdateStatus();
            _poll.Start();
        };
        Unloaded += (_, _) => _poll.Stop();
    }

    private void UpdateStatus()
    {
        bool running = HostControl.IsRunning;
        StatusRow.Description = _busy ? "Chwileczkę…"
            : !running ? "MacWin nie działa."
            : HostControl.IsElevated
                ? "Działa z uprawnieniami administratora – animuje też programy uruchomione jako administrator."
                : "Działa bez uprawnień administratora – programy uruchomione jako administrator mają zwykłe animacje.";
        StartButton.Visibility = running ? Visibility.Collapsed : Visibility.Visible;
        RestartButton.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        StopButton.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        StartButton.IsEnabled = RestartButton.IsEnabled = StopButton.IsEnabled = !_busy;
    }

    private async Task Run(Func<Task> action)
    {
        _busy = true;
        UpdateStatus();
        try
        {
            await action();
            await Task.Delay(600);
        }
        finally
        {
            _busy = false;
            UpdateStatus();
        }
    }

    private async void Start_Click(object sender, RoutedEventArgs e) => await Run(() =>
    {
        HostControl.Start();
        return Task.CompletedTask;
    });

    private async void Restart_Click(object sender, RoutedEventArgs e) => await Run(HostControl.RestartAsync);

    private async void Stop_Click(object sender, RoutedEventArgs e) => await Run(HostControl.StopAsync);
}
