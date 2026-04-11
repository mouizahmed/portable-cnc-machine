using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Styling;
using PortableCncApp.Services;
using PortableCncApp.ViewModels;
using PortableCncApp.Views;

namespace PortableCncApp;

public partial class App : Application
{
    public static void ApplyThemeMode(string? themeMode)
    {
        if (Application.Current is not App app)
            return;

        app.RequestedThemeVariant = NormalizeThemeMode(themeMode) switch
        {
            "light" => ThemeVariant.Light,
            "dark" => ThemeVariant.Dark,
            _ => ThemeVariant.Default
        };
    }

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var settings = new SettingsService();
            settings.Load();
            ApplyThemeMode(settings.Current.ThemeMode);

            // Create the main ViewModel which contains all page ViewModels
            var mainWindowVm = new MainWindowViewModel();

            desktop.MainWindow = new MainWindow
            {
                DataContext = mainWindowVm
            };
        }

        base.OnFrameworkInitializationCompleted();
    }

    private static string NormalizeThemeMode(string? themeMode)
        => themeMode?.Trim().ToLowerInvariant() switch
        {
            "light" => "light",
            "dark" => "dark",
            _ => "system"
        };
}
