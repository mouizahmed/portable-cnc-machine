using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class DashboardViewModel : PageViewModelBase
{
    // The Dashboard primarily displays data from MainWindowViewModel
    // It provides quick access to Start/Pause/Stop controls and shows:
    // - G-code visualization preview
    // - Current position (DRO)
    // - Spindle/Feed info
    // - Program progress
    // - Environmental data

    private string _gcodePreview = "";
    public string GcodePreview
    {
        get => _gcodePreview;
        set => SetProperty(ref _gcodePreview, value);
    }

    // Visualization mode toggle
    private bool _show3DView = true;
    public bool Show3DView
    {
        get => _show3DView;
        set => SetProperty(ref _show3DView, value);
    }

    public ICommand Toggle2D3DCommand { get; }

    public DashboardViewModel()
    {
        Toggle2D3DCommand = new RelayCommand(() => Show3DView = !Show3DView);
    }
}
