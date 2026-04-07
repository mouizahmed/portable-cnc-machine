using System;
using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace PortableCncApp.ViewModels;

public class ConnectionStatusToBrushConverter : IValueConverter
{
    public static readonly ConnectionStatusToBrushConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is ConnectionStatus status)
        {
            return status switch
            {
                ConnectionStatus.Connected => new SolidColorBrush(Color.Parse("#3BB273")),
                ConnectionStatus.Connecting => new SolidColorBrush(Color.Parse("#E0A100")),
                ConnectionStatus.Error => new SolidColorBrush(Color.Parse("#D83B3B")),
                _ => new SolidColorBrush(Color.Parse("#666666"))
            };
        }
        return new SolidColorBrush(Color.Parse("#666666"));
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}

/// <summary>
/// Converts a boolean to a status indicator brush (green for true/OK, red for false/error)
/// </summary>
public class BoolToStatusBrushConverter : IValueConverter
{
    public static readonly BoolToStatusBrushConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is bool b)
        {
            return b 
                ? new SolidColorBrush(Color.Parse("#3BB273"))  // Green for OK/true
                : new SolidColorBrush(Color.Parse("#D83B3B")); // Red for error/false
        }
        return new SolidColorBrush(Color.Parse("#666666")); // Gray for unknown
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}

/// <summary>
/// Converts a boolean to a limit switch indicator brush (red when triggered, gray when not)
/// </summary>
public class BoolToLimitBrushConverter : IValueConverter
{
    public static readonly BoolToLimitBrushConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is bool triggered)
        {
            return triggered 
                ? new SolidColorBrush(Color.Parse("#D83B3B"))  // Red when triggered
                : new SolidColorBrush(Color.Parse("#444444")); // Dark gray when not triggered
        }
        return new SolidColorBrush(Color.Parse("#444444"));
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
