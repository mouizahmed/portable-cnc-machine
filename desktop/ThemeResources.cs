using System;
using Avalonia;
using Avalonia.Media;

namespace PortableCncApp;

internal static class ThemeResources
{
    private static bool _isSubscribed;
    private static event EventHandler? ThemeChangedCore;

    public static event EventHandler? ThemeChanged
    {
        add
        {
            EnsureSubscribed();
            ThemeChangedCore += value;
        }
        remove => ThemeChangedCore -= value;
    }

    public static IBrush Brush(string key, string fallbackHex)
    {
        if (TryGetResource(key, out var resource) && resource is IBrush brush)
        {
            return brush;
        }

        return new SolidColorBrush(Color.Parse(fallbackHex));
    }

    private static void EnsureSubscribed()
    {
        if (_isSubscribed || Application.Current is null)
        {
            return;
        }

        Application.Current.ActualThemeVariantChanged += OnActualThemeVariantChanged;
        _isSubscribed = true;
    }

    private static void OnActualThemeVariantChanged(object? sender, EventArgs e)
        => ThemeChangedCore?.Invoke(null, EventArgs.Empty);

    private static bool TryGetResource(string key, out object? resource)
    {
        resource = null;

        var app = Application.Current;
        return app is not null && app.TryGetResource(key, app.ActualThemeVariant, out resource);
    }
}
