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
                ConnectionStatus.Connected => ThemeResources.Brush("SuccessBrush", "#3BB273"),
                ConnectionStatus.Connecting => ThemeResources.Brush("WarningBrush", "#E0A100"),
                ConnectionStatus.Error => ThemeResources.Brush("DangerBrush", "#D83B3B"),
                _ => ThemeResources.Brush("NeutralStateBrush", "#808080")
            };
        }
        return ThemeResources.Brush("NeutralStateBrush", "#808080");
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
                ? ThemeResources.Brush("SuccessBrush", "#3BB273")
                : ThemeResources.Brush("DangerBrush", "#D83B3B");
        }
        return ThemeResources.Brush("NeutralStateBrush", "#808080");
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
                ? ThemeResources.Brush("DangerBrush", "#D83B3B")
                : ThemeResources.Brush("NeutralIndicatorBrush", "#444444");
        }
        return ThemeResources.Brush("NeutralIndicatorBrush", "#444444");
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}

public class StringEqualsConverter : IValueConverter
{
    public static readonly StringEqualsConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value?.ToString() == parameter?.ToString();

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}

public class BoolToWarningHighlightBrushConverter : IValueConverter
{
    public static readonly BoolToWarningHighlightBrushConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is bool hasWarning && hasWarning
            ? ThemeResources.Brush("WarningHighlightBrush", "#3A2A14")
            : Brushes.Transparent;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}

public class BoolToWarningForegroundConverter : IValueConverter
{
    public static readonly BoolToWarningForegroundConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is bool hasWarning && hasWarning
            ? ThemeResources.Brush("WarningLineForegroundBrush", "#FFD27A")
            : ThemeResources.Brush("TextSecondaryBrush", "#A0A0A0");

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotImplementedException();
}
