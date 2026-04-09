using System;
using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class DashboardViewModel : PageViewModelBase
{
    private string _cameraPreset = "Iso";
    public string CameraPreset
    {
        get => _cameraPreset;
        set => SetProperty(ref _cameraPreset, value);
    }

    private int _resetViewToken;
    public int ResetViewToken
    {
        get => _resetViewToken;
        set => SetProperty(ref _resetViewToken, value);
    }

    private bool _showRapids = true;
    public bool ShowRapids
    {
        get => _showRapids;
        set => SetProperty(ref _showRapids, value);
    }

    private bool _showCuts = true;
    public bool ShowCuts
    {
        get => _showCuts;
        set => SetProperty(ref _showCuts, value);
    }

    private bool _showArcs = true;
    public bool ShowArcs
    {
        get => _showArcs;
        set => SetProperty(ref _showArcs, value);
    }

    private bool _showPlunges = true;
    public bool ShowPlunges
    {
        get => _showPlunges;
        set => SetProperty(ref _showPlunges, value);
    }

    private bool _showCompletedPath = true;
    public bool ShowCompletedPath
    {
        get => _showCompletedPath;
        set => SetProperty(ref _showCompletedPath, value);
    }

    private bool _showRemainingPath = true;
    public bool ShowRemainingPath
    {
        get => _showRemainingPath;
        set => SetProperty(ref _showRemainingPath, value);
    }

    private bool _showStockBox;
    public bool ShowStockBox
    {
        get => _showStockBox;
        set => SetProperty(ref _showStockBox, value);
    }

    private double _scrubberValue;
    public double ScrubberValue
    {
        get => _scrubberValue;
        set
        {
            if (SetProperty(ref _scrubberValue, value))
            {
                PreviewLine = Math.Max(0, (int)Math.Round(value));
            }
        }
    }

    private int _previewLine;
    public int PreviewLine
    {
        get => _previewLine;
        set
        {
            if (SetProperty(ref _previewLine, value))
            {
                RaisePropertyChanged(nameof(ScrubberLabel));
            }
        }
    }

    public string ViewerTitle => "3D TOOLPATH";
    public string ViewerHint => "Wheel to zoom. Drag to orbit. Right-drag to pan. Live machine position overlays the loaded toolpath.";

    public string ScrubberLabel => $"Preview line {PreviewLine}";

    public ICommand ResetViewCommand { get; }
    public ICommand SetCameraPresetCommand { get; }

    public DashboardViewModel()
    {
        ResetViewCommand = new RelayCommand(() => ResetViewToken++);
        SetCameraPresetCommand = new RelayCommand<string>(preset =>
        {
            if (string.IsNullOrWhiteSpace(preset))
            {
                return;
            }

            CameraPreset = preset;
            ResetViewToken++;
        });
    }
}
