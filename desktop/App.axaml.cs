using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using PortableCncApp.ViewModels;
using PortableCncApp.Views;

namespace PortableCncApp;

public partial class App : Application
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // Create all page ViewModels
            var runVm = new RunViewModel();
            var jogVm = new JogViewModel();
            var filesVm = new FilesViewModel();
            var connectVm = new ConnectViewModel();
            var settingsVm = new SettingsViewModel();
            var maintenanceVm = new MaintenanceViewModel();

            // Create and wire up the main window
            var mainWindowVm = new MainWindowViewModel(
                runVm, jogVm, filesVm, connectVm, settingsVm, maintenanceVm);

            desktop.MainWindow = new MainWindow
            {
                DataContext = mainWindowVm
            };
        }

        base.OnFrameworkInitializationCompleted();
    }
}
