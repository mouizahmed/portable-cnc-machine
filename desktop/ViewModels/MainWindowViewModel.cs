using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows.Input;
using Avalonia.Media;
using Avalonia.Threading;
using PortableCncApp.Services;
using PortableCncApp.Services.GCode;
using PortableCncApp.Services.Web;

namespace PortableCncApp.ViewModels;

public sealed class MainWindowViewModel : ViewModelBase, IDisposable
{
    // ════════════════════════════════════════════════════════════════
    // SERIAL SERVICE
    // ════════════════════════════════════════════════════════════════

    public SerialService Serial { get; } = new();
    public SettingsService Settings { get; } = new();
    private DispatcherTimer? _pollTimer;

    // ════════════════════════════════════════════════════════════════
    // CONNECTION STATUS (USB CDC Serial to Pico 2W)
    // ════════════════════════════════════════════════════════════════
    
    private ConnectionStatus _piConnectionStatus = ConnectionStatus.Disconnected;
    /// <summary>
    /// Connection status to the Pico 2W (Safety Supervisor) via USB CDC Serial
    /// </summary>
    public ConnectionStatus PiConnectionStatus
    {
        get => _piConnectionStatus;
        set
        {
            if (SetProperty(ref _piConnectionStatus, value))
            {
                RaisePropertyChanged(nameof(ConnectionStatusText));
                RaisePropertyChanged(nameof(ConnectionStatusBrush));
                RaisePropertyChanged(nameof(IsConnected));
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanPause));
                RaisePropertyChanged(nameof(CanStop));
                RaisePropertyChanged(nameof(CanHome));
                RaisePropertyChanged(nameof(CanJog));
            }
        }
    }

    private ConnectionStatus _teensyConnectionStatus = ConnectionStatus.Disconnected;
    /// <summary>
    /// Connection status to the Teensy 4.1 (Motion Controller) via Pico relay
    /// </summary>
    public ConnectionStatus TeensyConnectionStatus
    {
        get => _teensyConnectionStatus;
        set
        {
            if (SetProperty(ref _teensyConnectionStatus, value))
            {
                RaisePropertyChanged(nameof(ConnectionStatusText));
                RaisePropertyChanged(nameof(ConnectionStatusBrush));
                RaisePropertyChanged(nameof(IsConnected));
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanPause));
                RaisePropertyChanged(nameof(CanStop));
                RaisePropertyChanged(nameof(CanHome));
                RaisePropertyChanged(nameof(CanJog));
            }
        }
    }

    public bool IsConnected => PiConnectionStatus == ConnectionStatus.Connected && 
                               TeensyConnectionStatus == ConnectionStatus.Connected;

    public string ConnectionStatusText
    {
        get
        {
            if (IsConnected) return "CONNECTED";
            if (PiConnectionStatus == ConnectionStatus.Connecting || 
                TeensyConnectionStatus == ConnectionStatus.Connecting)
                return "CONNECTING...";
            return "DISCONNECTED";
        }
    }

    public IBrush ConnectionStatusBrush => IsConnected 
        ? new SolidColorBrush(Color.Parse("#3BB273")) 
        : new SolidColorBrush(Color.Parse("#808080"));

    // ════════════════════════════════════════════════════════════════
    // MOTION CONTROLLER STATE (Teensy FSM)
    // ════════════════════════════════════════════════════════════════
    
    private MotionState _motionState = MotionState.PowerUp;
    public MotionState MotionState
    {
        get => _motionState;
        set
        {
            if (SetProperty(ref _motionState, value))
            {
                RaisePropertyChanged(nameof(MotionStateLabel));
                RaisePropertyChanged(nameof(MotionStateBrush));
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanPause));
                RaisePropertyChanged(nameof(CanStop));
                RaisePropertyChanged(nameof(CanHome));
                RaisePropertyChanged(nameof(CanJog));
            }
        }
    }

    public string MotionStateLabel => MotionState switch
    {
        MotionState.PowerUp => "POWER UP",
        MotionState.Idle => "IDLE",
        MotionState.Homing => "HOMING",
        MotionState.Jog => "JOG",
        MotionState.RunProgram => "RUNNING",
        MotionState.FeedHold => "PAUSED",
        MotionState.Fault => "FAULT",
        MotionState.EStopLatched => "E-STOP",
        _ => "UNKNOWN"
    };

    public IBrush MotionStateBrush => MotionState switch
    {
        MotionState.Idle => new SolidColorBrush(Color.Parse("#3BB273")),        // Green
        MotionState.Homing => new SolidColorBrush(Color.Parse("#5B9BD5")),      // Blue
        MotionState.Jog => new SolidColorBrush(Color.Parse("#5B9BD5")),         // Blue
        MotionState.RunProgram => new SolidColorBrush(Color.Parse("#3BB273")),  // Green
        MotionState.FeedHold => new SolidColorBrush(Color.Parse("#E0A100")),    // Yellow/Amber
        MotionState.Fault => new SolidColorBrush(Color.Parse("#D83B3B")),       // Red
        MotionState.EStopLatched => new SolidColorBrush(Color.Parse("#D83B3B")),// Red
        _ => new SolidColorBrush(Color.Parse("#3A3A3A"))                         // Gray
    };

    // ════════════════════════════════════════════════════════════════
    // SAFETY SUPERVISOR STATE (Raspberry Pi FSM)
    // ════════════════════════════════════════════════════════════════
    
    private SafetyState _safetyState = SafetyState.SafeIdle;
    public SafetyState SafetyState
    {
        get => _safetyState;
        set
        {
            if (SetProperty(ref _safetyState, value))
            {
                RaisePropertyChanged(nameof(SafetyStateLabel));
                RaisePropertyChanged(nameof(SafetyStateBrush));
                RaisePropertyChanged(nameof(HasSafetyWarning));
            }
        }
    }

    public string SafetyStateLabel => SafetyState switch
    {
        SafetyState.SafeIdle => "SAFE",
        SafetyState.Monitoring => "MONITORING",
        SafetyState.Warning => "WARNING",
        SafetyState.EStopActive => "E-STOP",
        SafetyState.ShutdownSequence => "SHUTDOWN",
        _ => "UNKNOWN"
    };

    public IBrush SafetyStateBrush => SafetyState switch
    {
        SafetyState.SafeIdle => new SolidColorBrush(Color.Parse("#3BB273")),
        SafetyState.Monitoring => new SolidColorBrush(Color.Parse("#3BB273")),
        SafetyState.Warning => new SolidColorBrush(Color.Parse("#E0A100")),
        SafetyState.EStopActive => new SolidColorBrush(Color.Parse("#D83B3B")),
        SafetyState.ShutdownSequence => new SolidColorBrush(Color.Parse("#D83B3B")),
        _ => new SolidColorBrush(Color.Parse("#3A3A3A"))
    };

    public bool HasSafetyWarning => SafetyState == SafetyState.Warning || 
                                    SafetyState == SafetyState.EStopActive;

    // ════════════════════════════════════════════════════════════════
    // MACHINE / WORK COORDINATES
    // ════════════════════════════════════════════════════════════════
    
    private double _machineX;
    public double MachineX
    {
        get => _machineX;
        set
        {
            if (SetProperty(ref _machineX, value))
            {
                RaisePropertyChanged(nameof(MachineCoordinatesText));
            }
        }
    }

    private double _machineY;
    public double MachineY
    {
        get => _machineY;
        set
        {
            if (SetProperty(ref _machineY, value))
            {
                RaisePropertyChanged(nameof(MachineCoordinatesText));
            }
        }
    }

    private double _machineZ;
    public double MachineZ
    {
        get => _machineZ;
        set
        {
            if (SetProperty(ref _machineZ, value))
            {
                RaisePropertyChanged(nameof(MachineCoordinatesText));
            }
        }
    }

    private double _workX;
    public double WorkX
    {
        get => _workX;
        set
        {
            if (SetProperty(ref _workX, value))
            {
                RaisePropertyChanged(nameof(PositionX));
                RaisePropertyChanged(nameof(WorkCoordinatesText));
            }
        }
    }

    private double _workY;
    public double WorkY
    {
        get => _workY;
        set
        {
            if (SetProperty(ref _workY, value))
            {
                RaisePropertyChanged(nameof(PositionY));
                RaisePropertyChanged(nameof(WorkCoordinatesText));
            }
        }
    }

    private double _workZ;
    public double WorkZ
    {
        get => _workZ;
        set
        {
            if (SetProperty(ref _workZ, value))
            {
                RaisePropertyChanged(nameof(PositionZ));
                RaisePropertyChanged(nameof(WorkCoordinatesText));
            }
        }
    }

    private double _workOffsetX;
    public double WorkOffsetX
    {
        get => _workOffsetX;
        set
        {
            _hasWorkOffset = true;
            SetProperty(ref _workOffsetX, value);
        }
    }

    private double _workOffsetY;
    public double WorkOffsetY
    {
        get => _workOffsetY;
        set
        {
            _hasWorkOffset = true;
            SetProperty(ref _workOffsetY, value);
        }
    }

    private double _workOffsetZ;
    public double WorkOffsetZ
    {
        get => _workOffsetZ;
        set
        {
            _hasWorkOffset = true;
            SetProperty(ref _workOffsetZ, value);
        }
    }

    private bool _hasWorkOffset;

    // Backward-compatible aliases for existing bindings that still expect one coordinate set.
    public double PositionX { get => WorkX; set => WorkX = value; }
    public double PositionY { get => WorkY; set => WorkY = value; }
    public double PositionZ { get => WorkZ; set => WorkZ = value; }

    public string WorkCoordinatesText => $"{WorkX:F3}, {WorkY:F3}, {WorkZ:F3}";
    public string MachineCoordinatesText => $"{MachineX:F3}, {MachineY:F3}, {MachineZ:F3}";

    // ════════════════════════════════════════════════════════════════
    // SPINDLE & FEED
    // ════════════════════════════════════════════════════════════════
    
    private double _spindleSpeed;
    public double SpindleSpeed
    {
        get => _spindleSpeed;
        set
        {
            if (SetProperty(ref _spindleSpeed, value))
            {
                RaisePropertyChanged(nameof(FeedSpindleSummary));
                RaisePropertyChanged(nameof(SpindleStatusText));
            }
        }
    }

    private bool _spindleOn;
    public bool SpindleOn
    {
        get => _spindleOn;
        set
        {
            if (SetProperty(ref _spindleOn, value))
                RaisePropertyChanged(nameof(SpindleStatusText));
        }
    }
    public string SpindleStatusText => SpindleOn ? $"ON ({SpindleSpeed:F0} RPM)" : "OFF";

    private double _feedRate;
    public double FeedRate
    {
        get => _feedRate;
        set
        {
            if (SetProperty(ref _feedRate, value))
            {
                RaisePropertyChanged(nameof(FeedSpindleSummary));
            }
        }
    }

    private int _feedOverride = 100;
    public int FeedOverride { get => _feedOverride; set => SetProperty(ref _feedOverride, value); }
    public string FeedSpindleSummary => $"{FeedRate:F0} / {SpindleSpeed:F0}";

    // ════════════════════════════════════════════════════════════════
    // ENVIRONMENTAL SENSORS
    // ════════════════════════════════════════════════════════════════
    
    private double _temperature;
    public double Temperature { get => _temperature; set => SetProperty(ref _temperature, value); }

    private bool _xHomed;
    public bool XHomed
    {
        get => _xHomed;
        set
        {
            if (SetProperty(ref _xHomed, value))
            {
                RaisePropertyChanged(nameof(AllAxesHomed));
                RaisePropertyChanged(nameof(HomingStatusText));
            }
        }
    }

    private bool _yHomed;
    public bool YHomed
    {
        get => _yHomed;
        set
        {
            if (SetProperty(ref _yHomed, value))
            {
                RaisePropertyChanged(nameof(AllAxesHomed));
                RaisePropertyChanged(nameof(HomingStatusText));
            }
        }
    }

    private bool _zHomed;
    public bool ZHomed
    {
        get => _zHomed;
        set
        {
            if (SetProperty(ref _zHomed, value))
            {
                RaisePropertyChanged(nameof(AllAxesHomed));
                RaisePropertyChanged(nameof(HomingStatusText));
            }
        }
    }

    public bool AllAxesHomed => XHomed && YHomed && ZHomed;

    public string HomingStatusText
    {
        get
        {
            if (AllAxesHomed) return "XYZ HOMED";

            var missing = new List<string>(3);
            if (!XHomed) missing.Add("X");
            if (!YHomed) missing.Add("Y");
            if (!ZHomed) missing.Add("Z");
            return $"HOME {string.Join("/", missing)}";
        }
    }

    private bool _xLimitTriggered;
    public bool XLimitTriggered
    {
        get => _xLimitTriggered;
        set
        {
            if (SetProperty(ref _xLimitTriggered, value))
            {
                RaisePropertyChanged(nameof(LimitsTriggered));
                RaisePropertyChanged(nameof(LimitsStatusText));
                RaisePropertyChanged(nameof(LimitSummaryText));
            }
        }
    }

    private bool _yLimitTriggered;
    public bool YLimitTriggered
    {
        get => _yLimitTriggered;
        set
        {
            if (SetProperty(ref _yLimitTriggered, value))
            {
                RaisePropertyChanged(nameof(LimitsTriggered));
                RaisePropertyChanged(nameof(LimitsStatusText));
                RaisePropertyChanged(nameof(LimitSummaryText));
            }
        }
    }

    private bool _zLimitTriggered;
    public bool ZLimitTriggered
    {
        get => _zLimitTriggered;
        set
        {
            if (SetProperty(ref _zLimitTriggered, value))
            {
                RaisePropertyChanged(nameof(LimitsTriggered));
                RaisePropertyChanged(nameof(LimitsStatusText));
                RaisePropertyChanged(nameof(LimitSummaryText));
            }
        }
    }

    public bool LimitsTriggered => XLimitTriggered || YLimitTriggered || ZLimitTriggered;
    public string LimitsStatusText => LimitsTriggered ? "TRIGGERED" : "OK";

    public string LimitSummaryText
    {
        get
        {
            if (!LimitsTriggered) return "XYZ CLEAR";

            var active = new List<string>(3);
            if (XLimitTriggered) active.Add("X");
            if (YLimitTriggered) active.Add("Y");
            if (ZLimitTriggered) active.Add("Z");
            return $"{string.Join("/", active)} LIMIT";
        }
    }

    // ════════════════════════════════════════════════════════════════
    // PROGRAM STATUS
    // ════════════════════════════════════════════════════════════════
    
    private string? _currentFileName;
    public string? CurrentFileName { get => _currentFileName; set => SetProperty(ref _currentFileName, value); }

    private int _currentLine;
    public int CurrentLine { get => _currentLine; set => SetProperty(ref _currentLine, value); }

    private int _totalLines;
    public int TotalLines { get => _totalLines; set => SetProperty(ref _totalLines, value); }

    private double _progress;
    public double Progress { get => _progress; set => SetProperty(ref _progress, value); }

    private GCodeDocument? _activeGCodeDocument;
    public GCodeDocument? ActiveGCodeDocument
    {
        get => _activeGCodeDocument;
        set
        {
            if (SetProperty(ref _activeGCodeDocument, value))
            {
                ToolpathWebViewerServer.Instance.UpdateScene(value);
                RaisePropertyChanged(nameof(HasActiveGCodeDocument));
                RaisePropertyChanged(nameof(ActiveToolpathUnits));
                RaisePropertyChanged(nameof(ActiveToolpathBounds));
                RaisePropertyChanged(nameof(ActiveToolpathDepth));
                RaisePropertyChanged(nameof(ActiveToolpathSegmentCount));
                RaisePropertyChanged(nameof(ActiveToolpathWarnings));
                DashboardVm.NotifyToolpathAvailabilityChanged();
            }
        }
    }

    public bool HasActiveGCodeDocument => ActiveGCodeDocument?.HasGeometry == true;
    public string ActiveToolpathUnits => ActiveGCodeDocument?.DisplayUnitsLabel ?? "--";
    public string ActiveToolpathBounds => ActiveGCodeDocument == null
        ? "--"
        : $"{ActiveGCodeDocument.WidthMm:F1} x {ActiveGCodeDocument.HeightMm:F1} mm";
    public string ActiveToolpathDepth => ActiveGCodeDocument == null
        ? "--"
        : $"{ActiveGCodeDocument.MinZ:F2} to {ActiveGCodeDocument.MaxZ:F2} mm";
    public string ActiveToolpathSegmentCount => ActiveGCodeDocument?.Segments.Length.ToString(CultureInfo.InvariantCulture) ?? "0";
    public string ActiveToolpathWarnings => ActiveGCodeDocument == null
        ? "Select a file to build the preview."
        : ActiveGCodeDocument.WarningCount == 0
            ? "No parse warnings."
            : $"{ActiveGCodeDocument.WarningCount} parse warnings.";

    // ════════════════════════════════════════════════════════════════
    // STATUS MESSAGES
    // ════════════════════════════════════════════════════════════════
    
    private string _statusMessage = "Not connected";
    public string StatusMessage { get => _statusMessage; set => SetProperty(ref _statusMessage, value); }

    private bool _isStatusError;
    public bool IsStatusError { get => _isStatusError; set => SetProperty(ref _isStatusError, value); }

    // ════════════════════════════════════════════════════════════════
    // COMMAND AVAILABILITY
    // ════════════════════════════════════════════════════════════════
    
    public bool CanStart => IsConnected && 
                           (MotionState == MotionState.Idle || MotionState == MotionState.FeedHold) &&
                           CurrentFileName != null;

    public bool CanPause => IsConnected && MotionState == MotionState.RunProgram;

    public bool CanStop => IsConnected && 
                          (MotionState == MotionState.RunProgram || 
                           MotionState == MotionState.FeedHold ||
                           MotionState == MotionState.Jog ||
                           MotionState == MotionState.Homing);

    public bool CanHome => IsConnected && MotionState == MotionState.Idle;

    public bool CanJog => IsConnected && MotionState == MotionState.Idle;

    // ════════════════════════════════════════════════════════════════
    // NAVIGATION
    // ════════════════════════════════════════════════════════════════
    
    private object? _currentPage;
    public object? CurrentPage
    {
        get => _currentPage;
        private set => SetProperty(ref _currentPage, value);
    }

    private string _currentPageName = "Dashboard";
    public string CurrentPageName
    {
        get => _currentPageName;
        private set => SetProperty(ref _currentPageName, value);
    }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════
    
    public ICommand GoDashboardCommand { get; }
    public ICommand GoManualControlCommand { get; }
    public ICommand GoFilesCommand { get; }
    public ICommand GoConnectCommand { get; }
    public ICommand GoSettingsCommand { get; }
    public ICommand GoDiagnosticsCommand { get; }

    public ICommand StartCommand { get; }
    public ICommand PauseCommand { get; }
    public ICommand StopCommand { get; }
    public ICommand HomeCommand { get; }
    public ICommand EStopCommand { get; }

    // ════════════════════════════════════════════════════════════════
    // PAGE VIEW MODELS
    // ════════════════════════════════════════════════════════════════
    
    public DashboardViewModel DashboardVm { get; }
    public ManualControlViewModel ManualControlVm { get; }
    public FilesViewModel FilesVm { get; }
    public ConnectViewModel ConnectVm { get; }
    public SettingsViewModel SettingsVm { get; }
    public DiagnosticsViewModel DiagnosticsVm { get; }

    // ════════════════════════════════════════════════════════════════
    // CONSTRUCTOR
    // ════════════════════════════════════════════════════════════════
    
    public MainWindowViewModel()
    {
        // Create page ViewModels
        DashboardVm = new DashboardViewModel();
        ManualControlVm = new ManualControlViewModel();
        FilesVm = new FilesViewModel();
        ConnectVm = new ConnectViewModel();
        SettingsVm = new SettingsViewModel();
        DiagnosticsVm = new DiagnosticsViewModel();

        // Wire up page ViewModels to main
        DashboardVm.SetMainViewModel(this);
        ManualControlVm.SetMainViewModel(this);
        FilesVm.SetMainViewModel(this);
        ConnectVm.SetMainViewModel(this);
        SettingsVm.SetMainViewModel(this);
        DiagnosticsVm.SetMainViewModel(this);

        // Navigation commands
        GoDashboardCommand = new RelayCommand(() => NavigateTo(DashboardVm, "Dashboard"));
        GoManualControlCommand = new RelayCommand(() => NavigateTo(ManualControlVm, "ManualControl"));
        GoFilesCommand = new RelayCommand(() => NavigateTo(FilesVm, "Files"));
        GoConnectCommand = new RelayCommand(() => NavigateTo(ConnectVm, "Connect"));
        GoSettingsCommand = new RelayCommand(() => NavigateTo(SettingsVm, "Settings"));
        GoDiagnosticsCommand = new RelayCommand(() => NavigateTo(DiagnosticsVm, "Diagnostics"));

        // Machine control commands
        StartCommand = new RelayCommand(ExecuteStart);
        PauseCommand = new RelayCommand(ExecutePause);
        StopCommand = new RelayCommand(ExecuteStop);
        HomeCommand = new RelayCommand(ExecuteHome);
        EStopCommand = new RelayCommand(ExecuteEStop);

        // Load persisted settings, apply to child VMs, attempt auto-connect
        Settings.Load();
        SettingsVm.ApplyFrom(Settings.Current);
        ConnectVm.TryAutoConnect();

        // Wire up serial service events
        Serial.LineReceived += OnGrblLine;
        Serial.ErrorOccurred += _ => OnDeviceLost();

        // Set initial page
        CurrentPage = DashboardVm;
    }

    private void NavigateTo(object page, string pageName)
    {
        CurrentPage = page;
        CurrentPageName = pageName;
    }

    private void ExecuteStart()
    {
        if (MotionState == MotionState.FeedHold)
        {
            MotionState = MotionState.RunProgram;
            StatusMessage = "Resuming program...";
        }
        else if (MotionState == MotionState.Idle)
        {
            MotionState = MotionState.RunProgram;
            SafetyState = SafetyState.Monitoring;
            StatusMessage = $"Running {CurrentFileName}...";
        }
    }

    private void ExecutePause()
    {
        if (MotionState == MotionState.RunProgram)
        {
            MotionState = MotionState.FeedHold;
            StatusMessage = "Program paused (Feed Hold)";
        }
    }

    private void ExecuteStop()
    {
        MotionState = MotionState.Idle;
        SafetyState = SafetyState.SafeIdle;
        Progress = 0;
        CurrentLine = 0;
        StatusMessage = "Program stopped";
    }

    private void ExecuteHome()
    {
        if (MotionState == MotionState.Idle)
        {
            MotionState = MotionState.Homing;
            StatusMessage = "Homing all axes...";
            SetAllAxesHomed(true);
            // TODO: Actual homing logic
        }
    }

    private void ExecuteEStop()
    {
        MotionState = MotionState.EStopLatched;
        SafetyState = SafetyState.EStopActive;
        SpindleOn = false;
        SpindleSpeed = 0;
        SetAllAxesHomed(false);
        ClearLimitStates();
        StatusMessage = "EMERGENCY STOP ACTIVATED";
        IsStatusError = true;
    }

    public void SetAllAxesHomed(bool homed)
    {
        XHomed = homed;
        YHomed = homed;
        ZHomed = homed;
    }

    public void SetAxisHomed(string axis, bool homed)
    {
        switch (axis.ToUpperInvariant())
        {
            case "X":
                XHomed = homed;
                break;
            case "Y":
                YHomed = homed;
                break;
            case "Z":
                ZHomed = homed;
                break;
        }
    }

    public void ClearLimitStates()
    {
        XLimitTriggered = false;
        YLimitTriggered = false;
        ZLimitTriggered = false;
    }

    private void ApplyPinState(string pins)
    {
        var upperPins = pins.ToUpperInvariant();
        XLimitTriggered = upperPins.Contains('X');
        YLimitTriggered = upperPins.Contains('Y');
        ZLimitTriggered = upperPins.Contains('Z');
    }

    // ════════════════════════════════════════════════════════════════
    // GRBL STATUS POLLING
    // ════════════════════════════════════════════════════════════════

    private DateTime _lastResponseTime = DateTime.MaxValue;
    private static readonly TimeSpan DisconnectTimeout = TimeSpan.FromSeconds(3);

    public void StartPolling()
    {
        _lastResponseTime = DateTime.UtcNow;
        _pollTimer?.Stop();
        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
        _pollTimer.Tick += (_, _) =>
        {
            if (DateTime.UtcNow - _lastResponseTime > DisconnectTimeout)
            {
                OnDeviceLost();
                return;
            }
            Serial.SendRealtime((byte)'?');
        };
        _pollTimer.Start();
    }

    public void StopPolling()
    {
        _pollTimer?.Stop();
        _pollTimer = null;
    }

    // ════════════════════════════════════════════════════════════════
    // GRBL RESPONSE PARSING
    // ════════════════════════════════════════════════════════════════

    private void OnDeviceLost()
    {
        StopPolling();
        Serial.Disconnect();

        PiConnectionStatus     = ConnectionStatus.Error;
        TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MotionState            = MotionState.PowerUp;
        SafetyState            = SafetyState.SafeIdle;
        SpindleOn              = false;
        SpindleSpeed           = 0;
        StatusMessage          = "Device disconnected";
        IsStatusError          = true;
        SetAllAxesHomed(false);
        ClearLimitStates();

        ConnectVm.ResetDeviceInfo();
    }

    private void OnGrblLine(string line)
    {
        _lastResponseTime = DateTime.UtcNow;
        if (line.StartsWith('<') && line.EndsWith('>'))
        {
            ParseStatusReport(line[1..^1]);
            if (TeensyConnectionStatus != ConnectionStatus.Connected)
                TeensyConnectionStatus = ConnectionStatus.Connected;
        }
        else if (line.StartsWith("Grbl "))
        {
            if (TeensyConnectionStatus != ConnectionStatus.Connected)
                TeensyConnectionStatus = ConnectionStatus.Connected;
        }
        else if (line == "ok")
        {
            if (TeensyConnectionStatus != ConnectionStatus.Connected)
                TeensyConnectionStatus = ConnectionStatus.Connected;
        }
        else if (line.StartsWith("[PICO:"))
        {
            // e.g. "[PICO:0.1.0]"
            ConnectVm.PicoFirmware = line[6..^1];
        }
        else if (line.StartsWith("[SN:"))
        {
            // e.g. "[SN:e6614c311b8a2c23]"
            ConnectVm.PicoSerialNumber = line[4..^1];
        }
        else if (line.StartsWith("[VER:"))
        {
            // e.g. "[VER:1.1h.20190825:]"
            var inner = line[5..^1].TrimEnd(':');
            ConnectVm.TeensyFirmware = inner;
        }
        else if (line.StartsWith("error:"))
        {
            StatusMessage = $"GRBL error {line[6..]}";
            IsStatusError = true;
        }
        else if (line.StartsWith("ALARM:"))
        {
            MotionState = MotionState.Fault;
            SpindleOn = false;
            SpindleSpeed = 0;
            SetAllAxesHomed(false);
            StatusMessage = $"GRBL alarm {line[6..]}";
            IsStatusError = true;
        }
    }

    private void ParseStatusReport(string report)
    {
        // Format: State|MPos:x,y,z|WPos:x,y,z|WCO:x,y,z|FS:feed,spindle|...
        var parts = report.Split('|');
        if (parts.Length == 0) return;

        // State (may be "Hold:0", "Hold:1" etc.)
        var stateStr = parts[0];
        var colonIdx = stateStr.IndexOf(':');
        if (colonIdx >= 0) stateStr = stateStr[..colonIdx];

        var newState = stateStr switch
        {
            "Idle"  => MotionState.Idle,
            "Run"   => MotionState.RunProgram,
            "Hold"  => MotionState.FeedHold,
            "Jog"   => MotionState.Jog,
            "Alarm" => MotionState.Fault,
            "Home"  => MotionState.Homing,
            _       => MotionState
        };
        if (MotionState != newState) MotionState = newState;

        double? machineX = null;
        double? machineY = null;
        double? machineZ = null;
        double? workX = null;
        double? workY = null;
        double? workZ = null;
        double? workOffsetX = null;
        double? workOffsetY = null;
        double? workOffsetZ = null;
        var sawPinState = false;

        for (int i = 1; i < parts.Length; i++)
        {
            var sep = parts[i].IndexOf(':');
            if (sep < 0) continue;
            var key = parts[i][..sep];
            var val = parts[i][(sep + 1)..];

            switch (key)
            {
                case "MPos":
                    if (TryParseCoordinateTriplet(val, out var parsedMachineX, out var parsedMachineY, out var parsedMachineZ))
                    {
                        machineX = parsedMachineX;
                        machineY = parsedMachineY;
                        machineZ = parsedMachineZ;
                    }
                    break;
                case "WPos":
                    if (TryParseCoordinateTriplet(val, out var parsedWorkX, out var parsedWorkY, out var parsedWorkZ))
                    {
                        workX = parsedWorkX;
                        workY = parsedWorkY;
                        workZ = parsedWorkZ;
                    }
                    break;
                case "WCO":
                    if (TryParseCoordinateTriplet(val, out var parsedOffsetX, out var parsedOffsetY, out var parsedOffsetZ))
                    {
                        workOffsetX = parsedOffsetX;
                        workOffsetY = parsedOffsetY;
                        workOffsetZ = parsedOffsetZ;
                        _hasWorkOffset = true;
                    }
                    break;
                case "FS":
                {
                    var fs = val.Split(',');
                    if (fs.Length >= 2)
                    {
                        if (double.TryParse(fs[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var f)) FeedRate = f;
                        if (double.TryParse(fs[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var s))
                        {
                            SpindleSpeed = s;
                            SpindleOn = s > 0;
                        }
                    }
                    break;
                }
                case "F":
                    if (double.TryParse(val, NumberStyles.Float, CultureInfo.InvariantCulture, out var feed)) FeedRate = feed;
                    break;
                case "Pn":
                    sawPinState = true;
                    ApplyPinState(val);
                    break;
            }
        }

        if (!sawPinState)
            ClearLimitStates();

        if (workOffsetX.HasValue && workOffsetY.HasValue && workOffsetZ.HasValue)
        {
            WorkOffsetX = workOffsetX.Value;
            WorkOffsetY = workOffsetY.Value;
            WorkOffsetZ = workOffsetZ.Value;
        }

        if (!workX.HasValue && !workY.HasValue && !workZ.HasValue &&
            machineX.HasValue && machineY.HasValue && machineZ.HasValue &&
            _hasWorkOffset)
        {
            workX = machineX.Value - WorkOffsetX;
            workY = machineY.Value - WorkOffsetY;
            workZ = machineZ.Value - WorkOffsetZ;
        }

        if (!machineX.HasValue && !machineY.HasValue && !machineZ.HasValue &&
            workX.HasValue && workY.HasValue && workZ.HasValue &&
            _hasWorkOffset)
        {
            machineX = workX.Value + WorkOffsetX;
            machineY = workY.Value + WorkOffsetY;
            machineZ = workZ.Value + WorkOffsetZ;
        }

        if (machineX.HasValue && machineY.HasValue && machineZ.HasValue)
        {
            MachineX = machineX.Value;
            MachineY = machineY.Value;
            MachineZ = machineZ.Value;
        }

        if (workX.HasValue && workY.HasValue && workZ.HasValue)
        {
            WorkX = workX.Value;
            WorkY = workY.Value;
            WorkZ = workZ.Value;
        }
    }

    private static bool TryParseCoordinateTriplet(string value, out double x, out double y, out double z)
    {
        x = 0;
        y = 0;
        z = 0;

        var coords = value.Split(',');
        return coords.Length >= 3
            && double.TryParse(coords[0], NumberStyles.Float, CultureInfo.InvariantCulture, out x)
            && double.TryParse(coords[1], NumberStyles.Float, CultureInfo.InvariantCulture, out y)
            && double.TryParse(coords[2], NumberStyles.Float, CultureInfo.InvariantCulture, out z);
    }

    public void Dispose()
    {
        StopPolling();
        Serial.Dispose();
    }

}
