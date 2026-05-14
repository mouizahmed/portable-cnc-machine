using Avalonia;
using Avalonia.Controls;

namespace PortableCncApp.Controls;

public partial class SectionCard : UserControl
{
    public static readonly StyledProperty<string?> TitleProperty =
        AvaloniaProperty.Register<SectionCard, string?>(nameof(Title));

    public static readonly StyledProperty<string?> SubtitleProperty =
        AvaloniaProperty.Register<SectionCard, string?>(nameof(Subtitle));

    public static readonly StyledProperty<object?> ActionsProperty =
        AvaloniaProperty.Register<SectionCard, object?>(nameof(Actions));

    public static readonly StyledProperty<object?> BodyProperty =
        AvaloniaProperty.Register<SectionCard, object?>(nameof(Body));

    public static readonly StyledProperty<bool> IsLooseProperty =
        AvaloniaProperty.Register<SectionCard, bool>(nameof(IsLoose));

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

    public object? Body
    {
        get => GetValue(BodyProperty);
        set => SetValue(BodyProperty, value);
    }

    public bool IsLoose
    {
        get => GetValue(IsLooseProperty);
        set => SetValue(IsLooseProperty, value);
    }

    public SectionCard()
    {
        InitializeComponent();

        TitleProperty.Changed.AddClassHandler<SectionCard>((control, _) => control.UpdateCard());
        SubtitleProperty.Changed.AddClassHandler<SectionCard>((control, _) => control.UpdateCard());
        ActionsProperty.Changed.AddClassHandler<SectionCard>((control, _) => control.UpdateCard());
        BodyProperty.Changed.AddClassHandler<SectionCard>((control, _) => control.UpdateCard());
        IsLooseProperty.Changed.AddClassHandler<SectionCard>((control, _) => control.UpdateCard());

        UpdateCard();
    }

    private void UpdateCard()
    {
        HeaderBlock.Title = Title;
        HeaderBlock.Subtitle = Subtitle;
        HeaderBlock.Actions = Actions;
        HeaderBlock.IsVisible = !string.IsNullOrWhiteSpace(Title) || !string.IsNullOrWhiteSpace(Subtitle) || Actions is not null;
        BodyHost.Content = Body;

        CardBorder.Classes.Set("Loose", IsLoose);
    }
}
