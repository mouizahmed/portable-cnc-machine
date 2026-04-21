using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace PortableCncApp.Controls;

public partial class FileStateCard : UserControl
{
    public static readonly StyledProperty<string?> HeaderProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(Header));

    public static readonly StyledProperty<string?> FileNameProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(FileName));

    public static readonly StyledProperty<string?> SizeTextProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(SizeText));

    public static readonly StyledProperty<string?> LinesTextProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(LinesText));

    public static readonly StyledProperty<string?> StatusTextProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(StatusText));

    public static readonly StyledProperty<IBrush?> StatusBackgroundProperty =
        AvaloniaProperty.Register<FileStateCard, IBrush?>(nameof(StatusBackground));

    public static readonly StyledProperty<string?> PrimaryActionTextProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(PrimaryActionText));

    public static readonly StyledProperty<ICommand?> PrimaryActionCommandProperty =
        AvaloniaProperty.Register<FileStateCard, ICommand?>(nameof(PrimaryActionCommand));

    public static readonly StyledProperty<bool> ShowPrimaryActionProperty =
        AvaloniaProperty.Register<FileStateCard, bool>(nameof(ShowPrimaryAction), true);

    public static readonly StyledProperty<bool> EmphasizePrimaryActionProperty =
        AvaloniaProperty.Register<FileStateCard, bool>(nameof(EmphasizePrimaryAction));

    public static readonly StyledProperty<string?> SecondaryActionTextProperty =
        AvaloniaProperty.Register<FileStateCard, string?>(nameof(SecondaryActionText));

    public static readonly StyledProperty<ICommand?> SecondaryActionCommandProperty =
        AvaloniaProperty.Register<FileStateCard, ICommand?>(nameof(SecondaryActionCommand));

    public static readonly StyledProperty<bool> ShowSecondaryActionProperty =
        AvaloniaProperty.Register<FileStateCard, bool>(nameof(ShowSecondaryAction));

    public string? Header
    {
        get => GetValue(HeaderProperty);
        set => SetValue(HeaderProperty, value);
    }

    public string? FileName
    {
        get => GetValue(FileNameProperty);
        set => SetValue(FileNameProperty, value);
    }

    public string? SizeText
    {
        get => GetValue(SizeTextProperty);
        set => SetValue(SizeTextProperty, value);
    }

    public string? LinesText
    {
        get => GetValue(LinesTextProperty);
        set => SetValue(LinesTextProperty, value);
    }

    public string? StatusText
    {
        get => GetValue(StatusTextProperty);
        set => SetValue(StatusTextProperty, value);
    }

    public IBrush? StatusBackground
    {
        get => GetValue(StatusBackgroundProperty);
        set => SetValue(StatusBackgroundProperty, value);
    }

    public string? PrimaryActionText
    {
        get => GetValue(PrimaryActionTextProperty);
        set => SetValue(PrimaryActionTextProperty, value);
    }

    public ICommand? PrimaryActionCommand
    {
        get => GetValue(PrimaryActionCommandProperty);
        set => SetValue(PrimaryActionCommandProperty, value);
    }

    public bool ShowPrimaryAction
    {
        get => GetValue(ShowPrimaryActionProperty);
        set => SetValue(ShowPrimaryActionProperty, value);
    }

    public bool EmphasizePrimaryAction
    {
        get => GetValue(EmphasizePrimaryActionProperty);
        set => SetValue(EmphasizePrimaryActionProperty, value);
    }

    public string? SecondaryActionText
    {
        get => GetValue(SecondaryActionTextProperty);
        set => SetValue(SecondaryActionTextProperty, value);
    }

    public ICommand? SecondaryActionCommand
    {
        get => GetValue(SecondaryActionCommandProperty);
        set => SetValue(SecondaryActionCommandProperty, value);
    }

    public bool ShowSecondaryAction
    {
        get => GetValue(ShowSecondaryActionProperty);
        set => SetValue(ShowSecondaryActionProperty, value);
    }

    public FileStateCard()
    {
        InitializeComponent();

        HeaderProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        FileNameProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        SizeTextProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        LinesTextProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        StatusTextProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        StatusBackgroundProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        PrimaryActionTextProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        PrimaryActionCommandProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        ShowPrimaryActionProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        EmphasizePrimaryActionProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        SecondaryActionTextProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        SecondaryActionCommandProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());
        ShowSecondaryActionProperty.Changed.AddClassHandler<FileStateCard>((control, _) => control.UpdateCard());

        UpdateCard();
    }

    private void UpdateCard()
    {
        HeaderText.Text = Header ?? string.Empty;
        HeaderText.IsVisible = !string.IsNullOrWhiteSpace(Header);

        SizeValueText.Text = string.IsNullOrWhiteSpace(SizeText) ? "--" : SizeText;
        LinesValueText.Text = string.IsNullOrWhiteSpace(LinesText) ? "--" : LinesText;
        InfoGrid.IsVisible = !string.IsNullOrWhiteSpace(FileName);

        StatusBadge.Text = FileName;
        StatusBadge.PillBackground = StatusBackground;
        StatusBadge.IsVisible = !string.IsNullOrWhiteSpace(FileName);

        ConfigureButton(PrimaryButton, PrimaryActionText, PrimaryActionCommand, ShowPrimaryAction, EmphasizePrimaryAction);
        ConfigureButton(SecondaryButton, SecondaryActionText, SecondaryActionCommand, ShowSecondaryAction, false);
        ActionsPanel.IsVisible = PrimaryButton.IsVisible || SecondaryButton.IsVisible;
    }

    private static void ConfigureButton(Button button, string? text, ICommand? command, bool isVisible, bool emphasize)
    {
        button.Content = text ?? string.Empty;
        button.Command = command;
        button.IsVisible = isVisible && !string.IsNullOrWhiteSpace(text) && command is not null;
        button.Classes.Set("Primary", emphasize);
    }
}
