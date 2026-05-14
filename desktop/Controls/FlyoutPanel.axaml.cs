using Avalonia;
using Avalonia.Controls;

namespace PortableCncApp.Controls;

public partial class FlyoutPanel : UserControl
{
    public static readonly StyledProperty<string?> TitleProperty =
        AvaloniaProperty.Register<FlyoutPanel, string?>(nameof(Title));

    public static readonly StyledProperty<object?> BodyProperty =
        AvaloniaProperty.Register<FlyoutPanel, object?>(nameof(Body));

    public string? Title
    {
        get => GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public object? Body
    {
        get => GetValue(BodyProperty);
        set => SetValue(BodyProperty, value);
    }

    public FlyoutPanel()
    {
        InitializeComponent();

        TitleProperty.Changed.AddClassHandler<FlyoutPanel>((control, _) => control.UpdatePanel());
        BodyProperty.Changed.AddClassHandler<FlyoutPanel>((control, _) => control.UpdatePanel());

        UpdatePanel();
    }

    private void UpdatePanel()
    {
        TitleText.Text = Title;
        TitleText.IsVisible = !string.IsNullOrWhiteSpace(Title);
        BodyHost.Content = Body;
    }
}
