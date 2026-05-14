using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using PortableCncApp.ViewModels;

namespace PortableCncApp.Views.Pages;

public partial class ManualControlView : UserControl
{
    private Button? _activeContinuousJogButton;

    public ManualControlView()
    {
        InitializeComponent();
        AddHandler(PointerPressedEvent, OnJogPadPointerPressed, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerReleasedEvent, OnJogPadPointerReleased, RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerCaptureLostEvent, OnJogPadPointerCaptureLost, RoutingStrategies.Tunnel, handledEventsToo: true);
    }

    private void OnJogPadPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if ((DataContext as ManualControlViewModel)?.ContinuousJog != true)
            return;

        var button = FindJogPadButton(e.Source);
        if (button == null)
            return;

        _activeContinuousJogButton = button;
        e.Pointer.Capture(this);
        (DataContext as ManualControlViewModel)?.JogPadPointerPressed(button.Tag as string);
        e.Handled = true;
    }

    private void OnJogPadPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if ((DataContext as ManualControlViewModel)?.ContinuousJog != true)
            return;

        var button = _activeContinuousJogButton ?? FindJogPadButton(e.Source);
        if (button == null)
            return;

        (DataContext as ManualControlViewModel)?.JogPadPointerReleased(button.Tag as string);
        _activeContinuousJogButton = null;
        e.Pointer.Capture(null);
        e.Handled = true;
    }

    private void OnJogPadPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        (DataContext as ManualControlViewModel)?.JogPadPointerCaptureLost();
        _activeContinuousJogButton = null;
    }

    private static Button? FindJogPadButton(object? source)
    {
        if (source is Button sourceButton && sourceButton.Classes.Contains("JogPadButton"))
            return sourceButton;

        if (source is Control control)
        {
            Control? current = control;
            while (current != null)
            {
                if (current is Button button && button.Classes.Contains("JogPadButton"))
                    return button;

                current = current.Parent as Control;
            }
        }

        return null;
    }
}
