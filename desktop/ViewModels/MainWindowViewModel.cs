using System;
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
    // MACHINE POSITION (DRO - Digital Readout)
    // ════════════════════════════════════════════════════════════════
    
    private double _positionX;
    public double PositionX { get => _positionX; set => SetProperty(ref _positionX, value); }

    private double _positionY;
    public double PositionY { get => _positionY; set => SetProperty(ref _positionY, value); }

    private double _positionZ;
    public double PositionZ { get => _positionZ; set => SetProperty(ref _positionZ, value); }

    // ════════════════════════════════════════════════════════════════
    // SPINDLE & FEED
    // ════════════════════════════════════════════════════════════════
    
    private double _spindleSpeed;
    public double SpindleSpeed { get => _spindleSpeed; set => SetProperty(ref _spindleSpeed, value); }

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
    public double FeedRate { get => _feedRate; set => SetProperty(ref _feedRate, value); }

    private int _feedOverride = 100;
    public int FeedOverride { get => _feedOverride; set => SetProperty(ref _feedOverride, value); }

    // ════════════════════════════════════════════════════════════════
    // ENVIRONMENTAL SENSORS
    // ════════════════════════════════════════════════════════════════
    
    private double _temperature;
    public double Temperature { get => _temperature; set => SetProperty(ref _temperature, value); }

    private double _humidity;
    public double Humidity { get => _humidity; set => SetProperty(ref _humidity, value); }

    private bool _limitsTriggered;
    public bool LimitsTriggered
    {
        get => _limitsTriggered;
        set
        {
            if (SetProperty(ref _limitsTriggered, value))
                RaisePropertyChanged(nameof(LimitsStatusText));
        }
    }
    public string LimitsStatusText => LimitsTriggered ? "TRIGGERED" : "OK";

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
    public ICommand GoJogCommand { get; }
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
    public JogViewModel JogVm { get; }
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
        JogVm = new JogViewModel();
        FilesVm = new FilesViewModel();
        ConnectVm = new ConnectViewModel();
        SettingsVm = new SettingsViewModel();
        DiagnosticsVm = new DiagnosticsViewModel();

        // Wire up page ViewModels to main
        DashboardVm.SetMainViewModel(this);
        JogVm.SetMainViewModel(this);
        FilesVm.SetMainViewModel(this);
        ConnectVm.SetMainViewModel(this);
        SettingsVm.SetMainViewModel(this);
        DiagnosticsVm.SetMainViewModel(this);

        // Navigation commands
        GoDashboardCommand = new RelayCommand(() => NavigateTo(DashboardVm, "Dashboard"));
        GoJogCommand = new RelayCommand(() => NavigateTo(JogVm, "Jog"));
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
            // TODO: Actual homing logic
        }
    }

    private void ExecuteEStop()
    {
        MotionState = MotionState.EStopLatched;
        SafetyState = SafetyState.EStopActive;
        SpindleOn = false;
        StatusMessage = "EMERGENCY STOP ACTIVATED";
        IsStatusError = true;
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
        StatusMessage          = "Device disconnected";
        IsStatusError          = true;

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
            StatusMessage = $"GRBL alarm {line[6..]}";
            IsStatusError = true;
        }
    }

    private void ParseStatusReport(string report)
    {
        // Format: State|MPos:x,y,z|FS:feed,spindle|...
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

        for (int i = 1; i < parts.Length; i++)
        {
            var sep = parts[i].IndexOf(':');
            if (sep < 0) continue;
            var key = parts[i][..sep];
            var val = parts[i][(sep + 1)..];

            switch (key)
            {
                case "MPos":
                case "WPos":
                {
                    var coords = val.Split(',');
                    if (coords.Length >= 3)
                    {
                        if (double.TryParse(coords[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) PositionX = x;
                        if (double.TryParse(coords[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var y)) PositionY = y;
                        if (double.TryParse(coords[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var z)) PositionZ = z;
                    }
                    break;
                }
                case "FS":
                {
                    var fs = val.Split(',');
                    if (fs.Length >= 2)
                    {
                        if (double.TryParse(fs[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var f)) FeedRate = f;
                        if (double.TryParse(fs[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var s)) SpindleSpeed = s;
                    }
                    break;
                }
                case "F":
                    if (double.TryParse(val, NumberStyles.Float, CultureInfo.InvariantCulture, out var feed)) FeedRate = feed;
                    break;
            }
        }
    }

    public void Dispose()
    {
        StopPolling();
        Serial.Dispose();
    }

}
