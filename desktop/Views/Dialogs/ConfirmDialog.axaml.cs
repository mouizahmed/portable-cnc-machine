using Avalonia.Controls;
using Avalonia.Interactivity;

namespace PortableCncApp.Views.Dialogs;

public partial class ConfirmDialog : Window
{
    public ConfirmDialog()
    {
        InitializeComponent();
    }

    public ConfirmDialog(string title, string message) : this()
    {
        Title            = title;
        TitleText.Text   = title;
        MessageText.Text = message;
    }

    private void OnConfirmClick(object? sender, RoutedEventArgs e) => Close(true);
    private void OnCancelClick(object? sender, RoutedEventArgs e)  => Close(false);
}
