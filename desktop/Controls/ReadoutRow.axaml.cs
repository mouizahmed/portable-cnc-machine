using Avalonia;
using Avalonia.Controls;

namespace PortableCncApp.Controls;

public partial class ReadoutRow : UserControl
{
    public static readonly StyledProperty<string?> LabelProperty =
        AvaloniaProperty.Register<ReadoutRow, string?>(nameof(Label));

    public static readonly StyledProperty<string?> ValueProperty =
        AvaloniaProperty.Register<ReadoutRow, string?>(nameof(Value));

    public static readonly StyledProperty<double> LabelWidthProperty =
        AvaloniaProperty.Register<ReadoutRow, double>(nameof(LabelWidth), 32);

    public static readonly StyledProperty<bool> IsMutedProperty =
        AvaloniaProperty.Register<ReadoutRow, bool>(nameof(IsMuted));

    public string? Label
    {
        get => GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public string? Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public double LabelWidth
    {
        get => GetValue(LabelWidthProperty);
        set => SetValue(LabelWidthProperty, value);
    }

    public bool IsMuted
    {
        get => GetValue(IsMutedProperty);
        set => SetValue(IsMutedProperty, value);
    }

    public ReadoutRow()
    {
        InitializeComponent();

        LabelProperty.Changed.AddClassHandler<ReadoutRow>((control, _) => control.UpdateRow());
        ValueProperty.Changed.AddClassHandler<ReadoutRow>((control, _) => control.UpdateRow());
        LabelWidthProperty.Changed.AddClassHandler<ReadoutRow>((control, _) => control.UpdateRow());
        IsMutedProperty.Changed.AddClassHandler<ReadoutRow>((control, _) => control.UpdateRow());

        UpdateRow();
    }

    private void UpdateRow()
    {
        LabelText.Text = Label ?? string.Empty;
        LabelText.Width = LabelWidth;
        ValueText.Text = Value ?? string.Empty;
        ValueText.Classes.Set("Muted", IsMuted);
    }
}
