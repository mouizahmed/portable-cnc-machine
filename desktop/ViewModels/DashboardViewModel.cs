using System;
using System.Collections.Generic;
using System.Windows.Input;
using Avalonia.Threading;
using PortableCncApp.Services.Web;

namespace PortableCncApp.ViewModels;

public sealed class DashboardViewModel : PageViewModelBase
{
    private const double BasePlaybackLinesPerSecond = 12.0;
    private static readonly TimeSpan PlaybackTickInterval = TimeSpan.FromMilliseconds(16);

    private string _cameraPreset = "Iso";
    private bool _isViewerPanelOpen;
    private readonly DispatcherTimer _playbackTimer;
    private readonly RelayCommand _stopPlaybackCommand;
    private readonly RelayCommand _playReverseCommand;
    private readonly RelayCommand _playForwardCommand;
    private readonly RelayCommand _stepBackwardCommand;
    private readonly RelayCommand _stepForwardCommand;
    private PlaybackMode _playbackMode = PlaybackMode.Stopped;
    private double _playbackSpeed = 1.0;
    private int _playbackCursorIndex = -1;
    private int[] _playbackLineSequence = Array.Empty<int>();

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

    private bool _showGrid = true;
    public bool ShowGrid
    {
        get => _showGrid;
        set => SetProperty(ref _showGrid, value);
    }

    private bool _showToolpathPoints;
    public bool ShowToolpathPoints
    {
        get => _showToolpathPoints;
        set => SetProperty(ref _showToolpathPoints, value);
    }

    public double PlaybackSpeed
    {
        get => _playbackSpeed;
        set
        {
            double clamped = Math.Clamp(value, 0.1, 10.0);
            if (SetProperty(ref _playbackSpeed, clamped))
            {
                RaisePropertyChanged(nameof(PreviewPlaybackStepDurationMs));
            }
        }
    }

    private double _scrubberValue;
    public double ScrubberValue
    {
        get => _scrubberValue;
        set
        {
            double clamped = ClampLine(value);
            if (SetProperty(ref _scrubberValue, clamped))
            {
                PreviewLine = Math.Max(0, (int)Math.Round(clamped));
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
                RaisePropertyChanged(nameof(PlaybackStatusLabel));
            }
        }
    }

    public string ViewerTitle => "3D TOOLPATH";
    public string ViewerHint => "Wheel to zoom. Drag to orbit. Right-drag to pan. Live machine position overlays the loaded toolpath.";
    public bool HasLoadedToolpath => MainVm?.HasActiveGCodeDocument == true;

    public bool IsViewerPanelOpen
    {
        get => _isViewerPanelOpen;
        set
        {
            if (!HasLoadedToolpath && value)
            {
                value = false;
            }

            SetProperty(ref _isViewerPanelOpen, value);
        }
    }

    public string ScrubberLabel => $"Preview line {PreviewLine} of {MaxPreviewLine}";
    public string PlaybackStatusLabel => _playbackMode switch
    {
        PlaybackMode.Reverse => "Playing reverse",
        PlaybackMode.Forward => "Playing forward",
        _ => "Stopped"
    };
    public string PreviewPlaybackMode => _playbackMode switch
    {
        PlaybackMode.Reverse => "reverse",
        PlaybackMode.Forward => "forward",
        _ => "stopped"
    };
    public double PreviewPlaybackStepDurationMs
        => _playbackMode == PlaybackMode.Stopped ? 0 : (1000.0 / (BasePlaybackLinesPerSecond * _playbackSpeed));

    public ICommand ResetViewCommand { get; }
    public ICommand SetCameraPresetCommand { get; }
    public ICommand StopPlaybackCommand => _stopPlaybackCommand;
    public ICommand PlayReverseCommand => _playReverseCommand;
    public ICommand PlayForwardCommand => _playForwardCommand;
    public ICommand StepBackwardCommand => _stepBackwardCommand;
    public ICommand StepForwardCommand => _stepForwardCommand;

    public DashboardViewModel()
    {
        _playbackTimer = new DispatcherTimer
        {
            Interval = PlaybackTickInterval
        };
        _playbackTimer.Tick += (_, _) => AdvancePlayback();

        _stopPlaybackCommand = new RelayCommand(StopPlaybackAndGoToStart, () => HasLoadedToolpath);
        _playReverseCommand = new RelayCommand(() => StartPlayback(PlaybackMode.Reverse), () => HasLoadedToolpath);
        _playForwardCommand = new RelayCommand(() => StartPlayback(PlaybackMode.Forward), () => HasLoadedToolpath);
        _stepBackwardCommand = new RelayCommand(() => StepBy(-1), () => HasLoadedToolpath);
        _stepForwardCommand = new RelayCommand(() => StepBy(1), () => HasLoadedToolpath);

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

    public void NotifyToolpathAvailabilityChanged()
    {
        RaisePropertyChanged(nameof(HasLoadedToolpath));
        RaisePropertyChanged(nameof(ScrubberLabel));
        if (!HasLoadedToolpath)
        {
            StopPlayback();
            PreviewLine = 0;
            ScrubberValue = 0;
            IsViewerPanelOpen = false;
            _playbackLineSequence = Array.Empty<int>();
        }
        else
        {
            RebuildPlaybackLineSequence();
            ScrubberValue = ClampLine(ScrubberValue);
        }

        RaiseTransportCanExecuteChanged();
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        if (propertyName is nameof(MainWindowViewModel.ActiveGCodeDocument) or nameof(MainWindowViewModel.TotalLines))
        {
            RebuildPlaybackLineSequence();
            RaisePropertyChanged(nameof(ScrubberLabel));
            RaiseTransportCanExecuteChanged();
        }
    }

    private double MaxPreviewLine => MainVm?.ActiveGCodeDocument?.TotalLines ?? MainVm?.TotalLines ?? 0;

    private void StartPlayback(PlaybackMode mode)
    {
        if (!HasLoadedToolpath)
        {
            return;
        }

        ScrubberValue = PreviewLine;
        _playbackMode = mode;
        _playbackCursorIndex = ResolvePlaybackStartIndex(mode, PreviewLine);
        ToolpathWebViewerServer.Instance.ClearReportedPosition();
        _playbackTimer.Start();
        RaisePropertyChanged(nameof(PlaybackStatusLabel));
        RaisePropertyChanged(nameof(PreviewPlaybackMode));
        RaisePropertyChanged(nameof(PreviewPlaybackStepDurationMs));
        RaiseTransportCanExecuteChanged();
    }

    private void StopPlayback()
    {
        _playbackMode = PlaybackMode.Stopped;
        _playbackCursorIndex = -1;
        _playbackTimer.Stop();
        RaisePropertyChanged(nameof(PlaybackStatusLabel));
        RaisePropertyChanged(nameof(PreviewPlaybackMode));
        RaisePropertyChanged(nameof(PreviewPlaybackStepDurationMs));
        RaiseTransportCanExecuteChanged();
    }

    private void StopPlaybackAndGoToStart()
    {
        if (!HasLoadedToolpath)
        {
            return;
        }

        StopPlayback();
        ScrubberValue = MaxPreviewLine >= 1 ? 1 : 0;
    }

    private void StepBy(int delta)
    {
        if (!HasLoadedToolpath || _playbackLineSequence.Length == 0)
        {
            return;
        }

        StopPlayback();
        int currentIndex = FindKeyframeIndex(PreviewLine);
        int targetIndex = currentIndex + delta;

        if (targetIndex < 0)
        {
            SetPlaybackLine(0);
        }
        else if (targetIndex >= _playbackLineSequence.Length)
        {
            SetPlaybackLine(_playbackLineSequence[_playbackLineSequence.Length - 1]);
        }
        else
        {
            SetPlaybackLine(_playbackLineSequence[targetIndex]);
        }
    }

    private int FindKeyframeIndex(int line)
    {
        int lo = 0, hi = _playbackLineSequence.Length - 1, result = -1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (_playbackLineSequence[mid] <= line)
            {
                result = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return result;
    }

    private void AdvancePlayback()
    {
        if (_playbackMode == PlaybackMode.Stopped)
        {
            return;
        }

        if (_playbackLineSequence.Length == 0)
        {
            StopPlayback();
            return;
        }

        // JS is the single playback clock. Read the position it has reported back
        // via the HTTP polling endpoint and sync the scrubber to it.
        var server = ToolpathWebViewerServer.Instance;
        int reportedKf = server.ReportedKeyframeIndex;

        // reportedKf is JS keyframe index minus 1 (JS has an extra initial-position
        // keyframe at index 0 that has no counterpart in _playbackLineSequence).
        if (reportedKf >= 0 && reportedKf < _playbackLineSequence.Length && reportedKf != _playbackCursorIndex)
        {
            _playbackCursorIndex = reportedKf;
            SetPlaybackLine(_playbackLineSequence[_playbackCursorIndex]);
        }

        if (server.ReportedPlaybackDone)
        {
            StopPlayback();
        }
    }

    private double ClampLine(double value)
        => Math.Clamp(value, 0, MaxPreviewLine);

    private int ClampLineNumber(int value)
        => (int)Math.Clamp(value, 0, MaxPreviewLine);

    private void SetPlaybackLine(int line)
    {
        int clampedLine = ClampLineNumber(line);
        PreviewLine = clampedLine;
        ScrubberValue = clampedLine;
    }

    private void RebuildPlaybackLineSequence()
    {
        var document = MainVm?.ActiveGCodeDocument;
        if (document == null || document.Segments.Length == 0)
        {
            _playbackLineSequence = Array.Empty<int>();
            _playbackCursorIndex = -1;
            return;
        }

        var lines = new List<int>(document.Segments.Length);
        int lastLine = int.MinValue;
        foreach (var segment in document.Segments)
        {
            if (segment.SourceLine == lastLine)
            {
                continue;
            }

            lines.Add(segment.SourceLine);
            lastLine = segment.SourceLine;
        }

        _playbackLineSequence = lines.ToArray();
        _playbackCursorIndex = -1;
    }

    private int ResolvePlaybackStartIndex(PlaybackMode mode, int currentLine)
    {
        if (_playbackLineSequence.Length == 0)
        {
            return -1;
        }

        if (mode is PlaybackMode.Forward)
        {
            for (int i = _playbackLineSequence.Length - 1; i >= 0; i--)
            {
                if (_playbackLineSequence[i] <= currentLine)
                {
                    return i;
                }
            }

            return -1;
        }

        for (int i = 0; i < _playbackLineSequence.Length; i++)
        {
            if (_playbackLineSequence[i] >= currentLine)
            {
                return i;
            }
        }

        return _playbackLineSequence.Length;
    }

    private void RaiseTransportCanExecuteChanged()
    {
        _stopPlaybackCommand.RaiseCanExecuteChanged();
        _playReverseCommand.RaiseCanExecuteChanged();
        _playForwardCommand.RaiseCanExecuteChanged();
        _stepBackwardCommand.RaiseCanExecuteChanged();
        _stepForwardCommand.RaiseCanExecuteChanged();
    }

    private enum PlaybackMode
    {
        Stopped,
        Reverse,
        Forward
    }
}
