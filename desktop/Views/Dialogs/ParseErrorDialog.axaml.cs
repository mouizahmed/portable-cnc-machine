using Avalonia.Controls;
using Avalonia.Interactivity;

namespace PortableCncApp.Views.Dialogs;

public partial class ParseErrorDialog : Window
{
    public ParseErrorDialog()
    {
        InitializeComponent();
    }

    public ParseErrorDialog(string title, string summary, string details)
        : this()
    {
        Title = title;
        SummaryText.Text = summary;
        DetailsText.Text = details;
    }

    private void OnCloseClick(object? sender, RoutedEventArgs e)
    {
        Close();
    }
}
