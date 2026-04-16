using System;
using Avalonia.Controls;
using PortableCncApp.ViewModels;
using PortableCncApp.Views.Dialogs;

namespace PortableCncApp.Views.Pages;

public partial class FilesView : UserControl
{
    private FilesViewModel? _viewModel;
    private bool _isShowingDialog;

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
            _viewModel.UploadFileExistsRequested -= OnUploadFileExistsRequested;
        }

        _viewModel = DataContext as FilesViewModel;

        if (_viewModel != null)
        {
            _viewModel.ParseErrorDialogRequested += OnParseErrorDialogRequested;
            _viewModel.UploadFileExistsRequested += OnUploadFileExistsRequested;
        }
    }

    private async void OnParseErrorDialogRequested(object? sender, ParseErrorDialogRequest request)
    {
        if (_isShowingDialog) return;
        _isShowingDialog = true;
        try
        {
            var owner  = TopLevel.GetTopLevel(this) as Window;
            var dialog = new ParseErrorDialog(request.Title, request.Summary, request.Details);
            if (owner != null) await dialog.ShowDialog(owner);
            else               dialog.Show();
        }
        finally { _isShowingDialog = false; }
    }

    private async void OnUploadFileExistsRequested(object? sender, string filename)
    {
        if (_isShowingDialog || _viewModel == null) return;
        _isShowingDialog = true;
        try
        {
            var owner  = TopLevel.GetTopLevel(this) as Window;
            var dialog = new ConfirmDialog(
                "File Already Exists",
                $"'{filename}' already exists on the device.\nDo you want to overwrite it?");

            bool? result = owner != null
                ? await dialog.ShowDialog<bool?>(owner)
                : null;

            if (result == true) _viewModel.ConfirmOverwrite();
            else                _viewModel.CancelOverwrite();
        }
        finally { _isShowingDialog = false; }
    }
}
