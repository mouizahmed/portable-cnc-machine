using System;
using Avalonia.Controls;
using PortableCncApp.ViewModels;
using PortableCncApp.Views.Dialogs;

namespace PortableCncApp.Views.Pages;

public partial class FilesView : UserControl
{
    private FilesViewModel? _viewModel;
    private bool _isShowingParseErrorDialog;

    public FilesView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (_viewModel != null)
        {
            _viewModel.ParseErrorDialogRequested -= OnParseErrorDialogRequested;
        }

        _viewModel = DataContext as FilesViewModel;
        if (_viewModel != null)
        {
            _viewModel.ParseErrorDialogRequested += OnParseErrorDialogRequested;
        }
    }

    private async void OnParseErrorDialogRequested(object? sender, ParseErrorDialogRequest request)
    {
        if (_isShowingParseErrorDialog)
        {
            return;
        }

        _isShowingParseErrorDialog = true;
        try
        {
            var owner = TopLevel.GetTopLevel(this) as Window;
            var dialog = new ParseErrorDialog(request.Title, request.Summary, request.Details);
            if (owner != null)
            {
                await dialog.ShowDialog(owner);
            }
            else
            {
                dialog.Show();
            }
        }
        finally
        {
            _isShowingParseErrorDialog = false;
        }
    }
}
