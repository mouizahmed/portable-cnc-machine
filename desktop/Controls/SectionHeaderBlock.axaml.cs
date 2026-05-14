using Avalonia;
using Avalonia.Controls;

namespace PortableCncApp.Controls;

public partial class SectionHeaderBlock : UserControl
{
    public static readonly StyledProperty<string?> TitleProperty =
        AvaloniaProperty.Register<SectionHeaderBlock, string?>(nameof(Title));

    public static readonly StyledProperty<string?> SubtitleProperty =
        AvaloniaProperty.Register<SectionHeaderBlock, string?>(nameof(Subtitle));

    public static readonly StyledProperty<object?> ActionsProperty =
        AvaloniaProperty.Register<SectionHeaderBlock, object?>(nameof(Actions));

    public string? Title
    {
        get => GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public string? Subtitle
    {
        get => GetValue(SubtitleProperty);
        set => SetValue(SubtitleProperty, value);
    }

    public object? Actions
    {
        get => GetValue(ActionsProperty);
        set => SetValue(ActionsProperty, value);
    }

    public SectionHeaderBlock()
    {
        InitializeComponent();

        TitleProperty.Changed.AddClassHandler<SectionHeaderBlock>((control, _) => control.UpdateHeader());
        SubtitleProperty.Changed.AddClassHandler<SectionHeaderBlock>((control, _) => control.UpdateHeader());
        ActionsProperty.Changed.AddClassHandler<SectionHeaderBlock>((control, _) => control.UpdateHeader());

        UpdateHeader();
    }

    private void UpdateHeader()
    {
        TitleText.Text = Title ?? string.Empty;
        SubtitleText.Text = Subtitle ?? string.Empty;
        SubtitleText.IsVisible = !string.IsNullOrWhiteSpace(Subtitle);
        ActionsHost.Content = Actions;
        ActionsHost.IsVisible = Actions is not null;
    }
}
