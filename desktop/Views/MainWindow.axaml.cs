using System;
using Avalonia.Controls;
using PortableCncApp.ViewModels;
using PortableCncApp.Views.Dialogs;

namespace PortableCncApp.Views;

public partial class MainWindow : Window
{
    private MainWindowViewModel? _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (_viewModel != null)
            _viewModel.FilesVm.LoadReplaceRequested -= OnLoadReplaceRequested;

        _viewModel = DataContext as MainWindowViewModel;

        if (_viewModel != null)
            _viewModel.FilesVm.LoadReplaceRequested += OnLoadReplaceRequested;
    }

    private async void OnLoadReplaceRequested(object? sender, LoadReplaceDialogRequest request)
    {
        if (_viewModel == null)
            return;

        var dialog = new ConfirmDialog(
            "Replace Loaded Job",
            $"'{request.CurrentLoadedFile}' is currently loaded.\nDo you want to replace it with '{request.RequestedFile}'?");

        bool? result = await dialog.ShowDialog<bool?>(this);
        if (result == true) _viewModel.FilesVm.ConfirmLoadReplace();
        else                _viewModel.FilesVm.CancelLoadReplace();
    }
}
