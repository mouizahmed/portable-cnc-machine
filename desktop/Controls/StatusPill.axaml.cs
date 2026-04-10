using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace PortableCncApp.Controls;

public partial class StatusPill : UserControl
{
    public static readonly StyledProperty<string?> PrefixProperty =
        AvaloniaProperty.Register<StatusPill, string?>(nameof(Prefix));

    public static readonly StyledProperty<string?> TextProperty =
        AvaloniaProperty.Register<StatusPill, string?>(nameof(Text));

    public static readonly StyledProperty<IBrush?> PillBackgroundProperty =
        AvaloniaProperty.Register<StatusPill, IBrush?>(nameof(PillBackground));

    public static readonly StyledProperty<IBrush?> LabelForegroundProperty =
        AvaloniaProperty.Register<StatusPill, IBrush?>(nameof(LabelForeground));

    public static readonly StyledProperty<IBrush?> TextForegroundProperty =
        AvaloniaProperty.Register<StatusPill, IBrush?>(nameof(TextForeground));

    public string? Prefix
    {
        get => GetValue(PrefixProperty);
        set => SetValue(PrefixProperty, value);
    }

    public string? Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }

    public IBrush? PillBackground
    {
        get => GetValue(PillBackgroundProperty);
        set => SetValue(PillBackgroundProperty, value);
    }

    public IBrush? LabelForeground
    {
        get => GetValue(LabelForegroundProperty);
        set => SetValue(LabelForegroundProperty, value);
    }

    public IBrush? TextForeground
    {
        get => GetValue(TextForegroundProperty);
        set => SetValue(TextForegroundProperty, value);
    }

    public StatusPill()
    {
        InitializeComponent();

        PrefixProperty.Changed.AddClassHandler<StatusPill>((control, _) => control.UpdatePill());
        TextProperty.Changed.AddClassHandler<StatusPill>((control, _) => control.UpdatePill());
        PillBackgroundProperty.Changed.AddClassHandler<StatusPill>((control, _) => control.UpdatePill());
        LabelForegroundProperty.Changed.AddClassHandler<StatusPill>((control, _) => control.UpdatePill());
        TextForegroundProperty.Changed.AddClassHandler<StatusPill>((control, _) => control.UpdatePill());

        UpdatePill();
    }

    private void UpdatePill()
    {
        PrefixText.Text = Prefix ?? string.Empty;
        PrefixText.IsVisible = !string.IsNullOrWhiteSpace(Prefix);
        ValueText.Text = Text ?? string.Empty;

        if (PillBackground is not null)
        {
            PillBorder.Background = PillBackground;
        }

        if (LabelForeground is not null)
        {
            PrefixText.Foreground = LabelForeground;
        }

        if (TextForeground is not null)
        {
            ValueText.Foreground = TextForeground;
        }
    }
}
