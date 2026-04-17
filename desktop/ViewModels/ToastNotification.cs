using System;
using System.Windows.Input;
using Avalonia.Media;

namespace PortableCncApp.ViewModels;

public enum ToastKind { Info, Warning, Error }

public sealed class ToastNotification
{
    public Guid   Id      { get; } = Guid.NewGuid();
    public string Message { get; }
    public ToastKind Kind { get; }
    public ICommand DismissCommand { get; }

    public IBrush AccentBrush => Kind switch
    {
        ToastKind.Error   => ThemeResources.Brush("DangerBrush",  "#D83B3B"),
        ToastKind.Warning => ThemeResources.Brush("WarningBrush", "#E0A100"),
        _                 => ThemeResources.Brush("InfoBrush",    "#5B9BD5"),
    };

    public ToastNotification(string message, ToastKind kind, Action<Guid> dismiss)
    {
        Message        = message;
        Kind           = kind;
        DismissCommand = new RelayCommand(() => dismiss(Id));
    }
}
