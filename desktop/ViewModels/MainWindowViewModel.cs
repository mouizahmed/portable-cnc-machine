using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows.Input;
using Avalonia.Media;
using Avalonia.Threading;
using PortableCncApp.Services;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.ViewModels;

public sealed class MainWindowViewModel : ViewModelBase, IDisposable
{
    private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan HeartbeatTimeout = TimeSpan.FromSeconds(8);

    // ════════════════════════════════════════════════════════════════
    // SERVICES
    // ════════════════════════════════════════════════════════════════

    public SerialService Serial { get; } = new();
    public SettingsService Settings { get; } = new();

    /// <summary>
    /// Typed @-protocol wrapper around SerialService.
    /// Child ViewModels use this for all outbound commands (Phase 3-5).
    /// </summary>
    public PicoProtocolService Protocol { get; }
    private readonly DispatcherTimer _heartbeatTimer;
    private DateTime _lastPongUtc = DateTime.MinValue;
    private DateTime _lastPingUtc = DateTime.MinValue;
    private DateTime _serialFaultGraceUntilUtc = DateTime.MinValue;
    private bool _heartbeatFaulted;
    private bool _hasReceivedMachineState;

    // ════════════════════════════════════════════════════════════════
    // CONNECTION STATUS
    // ════════════════════════════════════════════════════════════════

    private ConnectionStatus _piConnectionStatus = ConnectionStatus.Disconnected;
    /// <summary>Connection status for the desktop → Pico USB CDC link.</summary>
    public ConnectionStatus PiConnectionStatus
    {
        get => _piConnectionStatus;
        set
        {
            if (SetProperty(ref _piConnectionStatus, value))
            {
                if (value == ConnectionStatus.Connected)
                {
                    _heartbeatFaulted = false;
                    _lastPingUtc = DateTime.MinValue;
                    _serialFaultGraceUntilUtc = DateTime.UtcNow.AddSeconds(5);
                    if (_lastPongUtc == DateTime.MinValue)
                        _lastPongUtc = DateTime.UtcNow;
                }
                else if (value != ConnectionStatus.Connecting)
                {
                    _lastPingUtc = DateTime.MinValue;
                    _lastPongUtc = DateTime.MinValue;
                    _serialFaultGraceUntilUtc = DateTime.MinValue;
                    _hasReceivedMachineState = false;
                }

                RaisePropertyChanged(nameof(ConnectionStatusText));
                RaisePropertyChanged(nameof(ConnectionStatusBrush));
                RaisePropertyChanged(nameof(IsConnected));
                RaisePropertyChanged(nameof(MotionStateLabel));
                RaisePropertyChanged(nameof(MotionStateBrush));
                RaisePropertyChanged(nameof(SafetyStateLabel));
                RaisePropertyChanged(nameof(SafetyStateBrush));
                RaisePropertyChanged(nameof(FeedSpindleSummary));
                RaisePropertyChanged(nameof(SpindleStatusText));
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanPause));
                RaisePropertyChanged(nameof(CanStop));
                RaisePropertyChanged(nameof(CanHome));
                RaisePropertyChanged(nameof(CanJog));
            }
        }
    }

    private ConnectionStatus _teensyConnectionStatus = ConnectionStatus.Disconnected;
    /// <summary>Teensy link status — updated via @EVENT TEENSY_CONNECTED / TEENSY_DISCONNECTED.</summary>
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
            if (PiConnectionStatus == ConnectionStatus.Connected)
                return "CONTROLLER LINKED";
            return "DISCONNECTED";
        }
    }

    public IBrush ConnectionStatusBrush
    {
        get
        {
            if (IsConnected)
                return ThemeResources.Brush("SuccessBrush", "#3BB273");

            if (PiConnectionStatus == ConnectionStatus.Connecting ||
                TeensyConnectionStatus == ConnectionStatus.Connecting)
                return ThemeResources.Brush("WarningBrush", "#E0A100");

            if (PiConnectionStatus == ConnectionStatus.Connected)
                return ThemeResources.Brush("WarningBrush", "#E0A100");

            return ThemeResources.Brush("NeutralStateBrush", "#808080");
        }
    }

    // ════════════════════════════════════════════════════════════════
    // MACHINE OPERATION STATE  (driven by @STATE from Pico)
    // ════════════════════════════════════════════════════════════════

    private MachineOperationState _machineState;
    /// <summary>
    /// Unified 13-state FSM. Set exclusively by PicoProtocolService.StateChanged.
    /// Never written locally.
    /// </summary>
    public MachineOperationState MachineState
    {
        get => _machineState;
        private set
        {
            if (SetProperty(ref _machineState, value))
            {
#pragma warning disable CS0618
                RaisePropertyChanged(nameof(MotionState));     // compat adapter
#pragma warning restore CS0618
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

    public string MotionStateLabel => MachineState switch
    {
        _ when PiConnectionStatus != ConnectionStatus.Connected => "DISCONNECTED",
        _ when !_hasReceivedMachineState => "SYNCING",
        MachineOperationState.Booting            => "BOOTING",
        MachineOperationState.TeensyDisconnected => "MOTION DISC.",
        MachineOperationState.Syncing            => "SYNCING",
        MachineOperationState.Idle               => "IDLE",
        MachineOperationState.Homing             => "HOMING",
        MachineOperationState.Jog                => "JOG",
        MachineOperationState.Starting           => "STARTING",
        MachineOperationState.Running            => "RUNNING",
        MachineOperationState.Hold               => "PAUSED",
        MachineOperationState.Fault              => "FAULT",
        MachineOperationState.Estop              => "E-STOP",
        MachineOperationState.CommsFault         => "COMMS FAULT",
        MachineOperationState.Uploading          => "UPLOADING",
        _                                        => "UNKNOWN"
    };

    public IBrush MotionStateBrush => MachineState switch
    {
        _ when PiConnectionStatus != ConnectionStatus.Connected => ThemeResources.Brush("NeutralStateBrush", "#808080"),
        _ when !_hasReceivedMachineState => ThemeResources.Brush("WarningBrush", "#E0A100"),
        MachineOperationState.Idle      => ThemeResources.Brush("SuccessBrush",      "#3BB273"),
        MachineOperationState.Homing    => ThemeResources.Brush("InfoBrush",         "#5B9BD5"),
        MachineOperationState.Jog       => ThemeResources.Brush("InfoBrush",         "#5B9BD5"),
        MachineOperationState.Starting  => ThemeResources.Brush("InfoBrush",         "#5B9BD5"),
        MachineOperationState.Running   => ThemeResources.Brush("SuccessBrush",      "#3BB273"),
        MachineOperationState.Hold      => ThemeResources.Brush("WarningBrush",      "#E0A100"),
        MachineOperationState.Fault     => ThemeResources.Brush("DangerBrush",       "#D83B3B"),
        MachineOperationState.Estop     => ThemeResources.Brush("DangerBrush",       "#D83B3B"),
        MachineOperationState.Uploading => ThemeResources.Brush("InfoBrush",         "#5B9BD5"),
        _                               => ThemeResources.Brush("NeutralStateBrush", "#808080")
    };

    // ── Backward-compat adapter — remove after Phase 4 (ManualControlVm / DiagnosticsVm rework) ──

    [Obsolete("Use MachineState. This adapter is kept so Phase 3-5 child VMs still compile.")]
    public MotionState MotionState
    {
        get => MachineState switch
        {
            MachineOperationState.Idle     => MotionState.Idle,
            MachineOperationState.Homing   => MotionState.Homing,
            MachineOperationState.Jog      => MotionState.Jog,
            MachineOperationState.Running  => MotionState.RunProgram,
            MachineOperationState.Hold     => MotionState.FeedHold,
            MachineOperationState.Fault    => MotionState.Fault,
            MachineOperationState.Estop    => MotionState.EStopLatched,
            _                              => MotionState.PowerUp
        };
        // No-op setter — MachineState is read-only, driven by @STATE from Pico.
        [Obsolete("No-op. State is driven by @STATE from Pico. Remove after Phase 4.")]
        set { }
    }

    // ════════════════════════════════════════════════════════════════
    // SAFETY LEVEL  (driven by @SAFETY from Pico)
    // ════════════════════════════════════════════════════════════════

    private SafetyLevel _safetyLevel;
    /// <summary>Orthogonal safety supervision level. Set by PicoProtocolService.SafetyChanged.</summary>
    public SafetyLevel SafetyLevel
    {
        get => _safetyLevel;
        private set
        {
            if (SetProperty(ref _safetyLevel, value))
            {
#pragma warning disable CS0618
                RaisePropertyChanged(nameof(SafetyState));     // compat adapter
#pragma warning restore CS0618
                RaisePropertyChanged(nameof(SafetyStateLabel));
                RaisePropertyChanged(nameof(SafetyStateBrush));
                RaisePropertyChanged(nameof(HasSafetyWarning));
            }
        }
    }

    public string SafetyStateLabel => SafetyLevel switch
    {
        _ when PiConnectionStatus != ConnectionStatus.Connected => "--",
        SafetyLevel.Safe       => "SAFE",
        SafetyLevel.Monitoring => "MONITORING",
        SafetyLevel.Warning    => "WARNING",
        SafetyLevel.Critical   => "CRITICAL",
        _                      => "UNKNOWN"
    };

    public IBrush SafetyStateBrush => SafetyLevel switch
    {
        _ when PiConnectionStatus != ConnectionStatus.Connected => ThemeResources.Brush("NeutralStateBrush", "#808080"),
        SafetyLevel.Safe       => ThemeResources.Brush("SuccessBrush",      "#3BB273"),
        SafetyLevel.Monitoring => ThemeResources.Brush("SuccessBrush",      "#3BB273"),
        SafetyLevel.Warning    => ThemeResources.Brush("WarningBrush",      "#E0A100"),
        SafetyLevel.Critical   => ThemeResources.Brush("DangerBrush",       "#D83B3B"),
        _                      => ThemeResources.Brush("NeutralStateBrush", "#808080")
    };

    public bool HasSafetyWarning => SafetyLevel == SafetyLevel.Warning ||
                                    SafetyLevel == SafetyLevel.Critical;

    // ── Backward-compat adapter — remove after Phase 4 ──

    [Obsolete("Use SafetyLevel. Kept so Phase 3-5 child VMs still compile.")]
    public SafetyState SafetyState
    {
        get => SafetyLevel switch
        {
            SafetyLevel.Safe       => SafetyState.SafeIdle,
            SafetyLevel.Monitoring => SafetyState.Monitoring,
            SafetyLevel.Warning    => SafetyState.Warning,
            SafetyLevel.Critical   => SafetyState.EStopActive,
            _                      => SafetyState.SafeIdle
        };
        [Obsolete("No-op. Safety level is driven by @SAFETY from Pico. Remove after Phase 4.")]
        set { }
    }

    // ════════════════════════════════════════════════════════════════
    // CAPABILITY FLAGS  (driven by @CAPS from Pico)
    // ════════════════════════════════════════════════════════════════

    private CapsFlags _caps;
    /// <summary>Per-action capability flags. Set by PicoProtocolService.CapsChanged.</summary>
    public CapsFlags Caps
    {
        get => _caps;
        private set
        {
            if (SetProperty(ref _caps, value))
            {
                RaisePropertyChanged(nameof(CanStart));
                RaisePropertyChanged(nameof(CanPause));
                RaisePropertyChanged(nameof(CanStop));
                RaisePropertyChanged(nameof(CanHome));
                RaisePropertyChanged(nameof(CanJog));
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // MACHINE / WORK COORDINATES  (driven by @POS from Pico)
    // ════════════════════════════════════════════════════════════════

    private double _machineX;
    public double MachineX
    {
        get => _machineX;
        private set
        {
            if (SetProperty(ref _machineX, value))
                RaisePropertyChanged(nameof(MachineCoordinatesText));
        }
    }

    private double _machineY;
    public double MachineY
    {
        get => _machineY;
        private set
        {
            if (SetProperty(ref _machineY, value))
                RaisePropertyChanged(nameof(MachineCoordinatesText));
        }
    }

    private double _machineZ;
    public double MachineZ
    {
        get => _machineZ;
        private set
        {
            if (SetProperty(ref _machineZ, value))
                RaisePropertyChanged(nameof(MachineCoordinatesText));
        }
    }

    private double _workX;
    public double WorkX
    {
        get => _workX;
        set   // still settable for Phase 4 compat (ManualControlVm writes WorkX = 0)
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

    // Phase 4: remove WorkOffset properties once ManualControlVm stops writing them.
    public double WorkOffsetX { get; set; }
    public double WorkOffsetY { get; set; }
    public double WorkOffsetZ { get; set; }

    // Backward-compatible aliases for bindings that still expect one coordinate set.
    public double PositionX { get => WorkX; set => WorkX = value; }
    public double PositionY { get => WorkY; set => WorkY = value; }
    public double PositionZ { get => WorkZ; set => WorkZ = value; }

    public string WorkCoordinatesText    => $"{WorkX:F3}, {WorkY:F3}, {WorkZ:F3}";
    public string MachineCoordinatesText => $"{MachineX:F3}, {MachineY:F3}, {MachineZ:F3}";

    // ════════════════════════════════════════════════════════════════
    // SPINDLE & FEED  (display only — not yet driven by protocol)
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

    public string SpindleStatusText => PiConnectionStatus != ConnectionStatus.Connected
        ? "DISCONNECTED"
        : SpindleOn ? $"ON ({SpindleSpeed:F0} RPM)" : "OFF";

    private double _feedRate;
    public double FeedRate
    {
        get => _feedRate;
        set
        {
            if (SetProperty(ref _feedRate, value))
                RaisePropertyChanged(nameof(FeedSpindleSummary));
        }
    }

    private int _feedOverride = 100;
    public int FeedOverride { get => _feedOverride; set => SetProperty(ref _feedOverride, value); }

    public string FeedSpindleSummary => PiConnectionStatus != ConnectionStatus.Connected
        ? "-- / --"
        : $"{FeedRate:F0} / {SpindleSpeed:F0}";

    // ════════════════════════════════════════════════════════════════
    // ENVIRONMENTAL
    // ════════════════════════════════════════════════════════════════

    private double _temperature;
    public double Temperature { get => _temperature; set => SetProperty(ref _temperature, value); }

    // Homed / limit state — updated by @EVENT LIMIT.
    // Individual homed axes are not yet in the protocol; kept for Phase 4 compat.
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
        private set
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
        private set
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
        private set
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
    public string? CurrentFileName
    {
        get => _currentFileName;
        set
        {
            if (SetProperty(ref _currentFileName, value))
            {
                RaisePropertyChanged(nameof(CanStart));
            }
        }
    }

    private int _currentLine;
    public int CurrentLine { get => _currentLine; private set => SetProperty(ref _currentLine, value); }

    private int _totalLines;
    public int TotalLines { get => _totalLines; set => SetProperty(ref _totalLines, value); }

    private double _progress;
    public double Progress { get => _progress; private set => SetProperty(ref _progress, value); }

    private GCodeDocument? _activeGCodeDocument;
    public GCodeDocument? ActiveGCodeDocument
    {
        get => _activeGCodeDocument;
        set
        {
            if (SetProperty(ref _activeGCodeDocument, value))
            {
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
    public string ActiveToolpathUnits   => ActiveGCodeDocument?.DisplayUnitsLabel ?? "--";

    public string ActiveToolpathBounds => ActiveGCodeDocument == null
        ? "--"
        : $"{ActiveGCodeDocument.WidthMm:F1} x {ActiveGCodeDocument.HeightMm:F1} mm";

    public string ActiveToolpathDepth => ActiveGCodeDocument == null
        ? "--"
        : $"{ActiveGCodeDocument.MinZ:F2} to {ActiveGCodeDocument.MaxZ:F2} mm";

    public string ActiveToolpathSegmentCount
        => ActiveGCodeDocument?.Segments.Length.ToString(CultureInfo.InvariantCulture) ?? "0";

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
    // COMMAND AVAILABILITY  (derived from Caps — no local re-derivation)
    // ════════════════════════════════════════════════════════════════

    public bool CanStart => PiConnectionStatus == ConnectionStatus.Connected &&
                            (Caps.JobStart || Caps.JobResume) &&
                            CurrentFileName != null;

    public bool CanPause => PiConnectionStatus == ConnectionStatus.Connected && Caps.JobPause;

    public bool CanStop  => PiConnectionStatus == ConnectionStatus.Connected && Caps.JobAbort;

    public bool CanHome  => PiConnectionStatus == ConnectionStatus.Connected && Caps.Motion;

    public bool CanJog   => PiConnectionStatus == ConnectionStatus.Connected && Caps.Motion;

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

    public ICommand GoDashboardCommand    { get; }
    public ICommand GoManualControlCommand { get; }
    public ICommand GoFilesCommand        { get; }
    public ICommand GoConnectCommand      { get; }
    public ICommand GoSettingsCommand     { get; }
    public ICommand GoDiagnosticsCommand  { get; }

    public ICommand StartCommand  { get; }
    public ICommand PauseCommand  { get; }
    public ICommand StopCommand   { get; }
    public ICommand HomeCommand   { get; }
    public ICommand EStopCommand  { get; }

    // ════════════════════════════════════════════════════════════════
    // PAGE VIEW MODELS
    // ════════════════════════════════════════════════════════════════

    public DashboardViewModel      DashboardVm     { get; }
    public ManualControlViewModel  ManualControlVm { get; }
    public FilesViewModel          FilesVm         { get; }
    public ConnectViewModel        ConnectVm       { get; }
    public SettingsViewModel       SettingsVm      { get; }
    public DiagnosticsViewModel    DiagnosticsVm   { get; }

    // ════════════════════════════════════════════════════════════════
    // CONSTRUCTOR
    // ════════════════════════════════════════════════════════════════

    public MainWindowViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;

        Protocol = new PicoProtocolService(Serial);
        _heartbeatTimer = new DispatcherTimer
        {
            Interval = HeartbeatInterval
        };
        _heartbeatTimer.Tick += (_, _) => CheckHeartbeat();

        // Subscribe to protocol events — these are the only writers for machine state.
        Protocol.StateChanged    += HandleStateChanged;
        Protocol.CapsChanged     += c => Caps = c;
        Protocol.SafetyChanged   += l => SafetyLevel = l;
        Protocol.PositionChanged += OnPositionChanged;
        Protocol.JobChanged      += HandleJobChanged;
        Protocol.PongReceived    += HandlePongReceived;
        Protocol.EventReceived   += HandleProtocolEvent;
        Protocol.ErrorReceived   += msg => { StatusMessage = $"Error: {msg}"; IsStatusError = true; };
        Protocol.WaitReceived    += reason => StatusMessage = string.IsNullOrEmpty(reason)
            ? "Controller busy - please wait"
            : $"Controller busy: {reason}";

        // Handle serial-layer disconnect (cable pulled, port error, etc.)
        Serial.LineReceived += _ => HandleProtocolActivity();
        Serial.ErrorOccurred += HandleSerialError;

        // Page ViewModels
        DashboardVm    = new DashboardViewModel();
        ManualControlVm = new ManualControlViewModel();
        FilesVm        = new FilesViewModel();
        ConnectVm      = new ConnectViewModel();
        SettingsVm     = new SettingsViewModel();
        DiagnosticsVm  = new DiagnosticsViewModel();

        DashboardVm.SetMainViewModel(this);
        ManualControlVm.SetMainViewModel(this);
        FilesVm.SetMainViewModel(this);
        ConnectVm.SetMainViewModel(this);
        SettingsVm.SetMainViewModel(this);
        DiagnosticsVm.SetMainViewModel(this);

        // Navigation
        GoDashboardCommand     = new RelayCommand(() => NavigateTo(DashboardVm,    "Dashboard"));
        GoManualControlCommand = new RelayCommand(() => NavigateTo(ManualControlVm, "ManualControl"));
        GoFilesCommand         = new RelayCommand(() => NavigateTo(FilesVm,        "Files"));
        GoConnectCommand       = new RelayCommand(() => NavigateTo(ConnectVm,      "Connect"));
        GoSettingsCommand      = new RelayCommand(() => NavigateTo(SettingsVm,     "Settings"));
        GoDiagnosticsCommand   = new RelayCommand(() => NavigateTo(DiagnosticsVm,  "Diagnostics"));

        // Machine control
        StartCommand = new RelayCommand(ExecuteStart);
        PauseCommand = new RelayCommand(ExecutePause);
        StopCommand  = new RelayCommand(ExecuteStop);
        HomeCommand  = new RelayCommand(ExecuteHome);
        EStopCommand = new RelayCommand(ExecuteEStop);

        Settings.Load();
        SettingsVm.ApplyFrom(Settings.Current);
        ConnectVm.TryAutoConnect();

        CurrentPage = DashboardVm;
        _heartbeatTimer.Start();
    }

    // ════════════════════════════════════════════════════════════════
    // MACHINE CONTROL COMMANDS
    // ════════════════════════════════════════════════════════════════

    private void ExecuteStart()
    {
        if (Caps.JobResume)
            Protocol.SendJobResume();
        else if (Caps.JobStart)
            Protocol.SendJobStart();
    }

    private void ExecutePause() => Protocol.SendJobPause();

    private void ExecuteStop()  => Protocol.SendJobAbort();

    private void ExecuteHome()  => Protocol.SendHome();

    private void ExecuteEStop() => Protocol.SendEstop();

    // ════════════════════════════════════════════════════════════════
    // PROTOCOL EVENT ROUTING
    // ════════════════════════════════════════════════════════════════

    private void OnPositionChanged(PicoPos pos)
    {
        MachineX = pos.MX;
        MachineY = pos.MY;
        MachineZ = pos.MZ;
        WorkX    = pos.WX;
        WorkY    = pos.WY;
        WorkZ    = pos.WZ;
    }

    private void HandleJobChanged(string? name)
    {
        var previousName = CurrentFileName;
        CurrentFileName = name;
        if (name == null)
        {
            CurrentLine = 0;
            Progress = 0;
            if (!string.IsNullOrWhiteSpace(previousName))
            {
                StatusMessage = "Job unloaded";
                IsStatusError = false;
            }
            return;
        }

        StatusMessage = $"Loaded: {name}";
        IsStatusError = false;
    }

    private void HandleStateChanged(MachineOperationState state)
    {
        _hasReceivedMachineState = true;
        MachineState = state;
    }

    private void HandlePongReceived()
    {
        _lastPongUtc = DateTime.UtcNow;
        _heartbeatFaulted = false;
    }

    private void HandleProtocolActivity()
    {
        if (PiConnectionStatus != ConnectionStatus.Connected)
            return;

        _lastPongUtc = DateTime.UtcNow;
        _heartbeatFaulted = false;
    }

    private void HandleSerialError(string _)
    {
        if (PiConnectionStatus != ConnectionStatus.Connected)
            return;

        if (DateTime.UtcNow < _serialFaultGraceUntilUtc)
            return;

        OnDeviceLost();
    }

    private void CheckHeartbeat()
    {
        if (PiConnectionStatus != ConnectionStatus.Connected || _heartbeatFaulted)
            return;

        var now = DateTime.UtcNow;
        var hasOutstandingPing = _lastPingUtc != DateTime.MinValue && _lastPingUtc > _lastPongUtc;

        if (hasOutstandingPing && now - _lastPingUtc >= HeartbeatTimeout)
        {
            _heartbeatFaulted = true;
            OnDeviceLost();
            return;
        }

        if (!hasOutstandingPing &&
            (_lastPingUtc == DateTime.MinValue || now - _lastPingUtc >= HeartbeatInterval))
        {
            _lastPingUtc = now;
            Protocol.SendPing();
        }
    }

    private void HandleProtocolEvent(string name, IReadOnlyDictionary<string, string> kv)
    {
        switch (name)
        {
            case "JOB_PROGRESS":
                if (kv.TryGetValue("LINE",  out var lineStr)  && int.TryParse(lineStr,  out var line))  CurrentLine = line;
                if (kv.TryGetValue("TOTAL", out var totalStr) && int.TryParse(totalStr, out var total)) TotalLines  = total;
                Progress = TotalLines > 0 ? (double)CurrentLine / TotalLines : 0;
                break;

            case "JOB_COMPLETE":
                StatusMessage = $"Job complete: {CurrentFileName}";
                IsStatusError = false;
                break;

            case "JOB_ERROR":
                StatusMessage = kv.TryGetValue("REASON", out var reason)
                    ? $"Job error: {reason}"
                    : "Job error";
                IsStatusError = true;
                break;

            case "SD_MOUNTED":
                StatusMessage = "Storage ready";
                IsStatusError = false;
                break;

            case "SD_REMOVED":
                StatusMessage = "Storage unavailable";
                break;

            case "TEENSY_CONNECTED":
                TeensyConnectionStatus = ConnectionStatus.Connected;
                StatusMessage          = "Motion controller online";
                IsStatusError          = false;
                break;

            case "TEENSY_DISCONNECTED":
                TeensyConnectionStatus = ConnectionStatus.Disconnected;
                StatusMessage          = "Motion controller disconnected";
                break;

            case "ESTOP_ACTIVE":
                StatusMessage = "EMERGENCY STOP ACTIVE";
                IsStatusError = true;
                break;

            case "ESTOP_CLEARED":
                StatusMessage = "E-stop cleared";
                IsStatusError = false;
                break;

            case "STATE_CHANGED":
                // @STATE always accompanies this event and is the authoritative state update.
                break;

            case "LIMIT":
                if (kv.TryGetValue("AXIS", out var axis))
                    UpdateLimitAxes(axis);
                break;
        }
    }

    private void UpdateLimitAxes(string axes)
    {
        var upper = axes.ToUpperInvariant();
        XLimitTriggered = upper.Contains('X');
        YLimitTriggered = upper.Contains('Y');
        ZLimitTriggered = upper.Contains('Z');
    }

    // ════════════════════════════════════════════════════════════════
    // DEVICE LOST
    // ════════════════════════════════════════════════════════════════

    private void OnDeviceLost()
    {
        if (_heartbeatFaulted == false)
            _heartbeatFaulted = true;

        Serial.Disconnect();

        PiConnectionStatus     = ConnectionStatus.Error;
        TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MachineState           = MachineOperationState.CommsFault;
        SafetyLevel            = SafetyLevel.Safe;
        SpindleOn              = false;
        SpindleSpeed           = 0;
        CurrentFileName        = null;
        CurrentLine            = 0;
        TotalLines             = 0;
        Progress               = 0;
        ActiveGCodeDocument    = null;
        _hasReceivedMachineState = false;
        StatusMessage          = "Device disconnected";
        IsStatusError          = true;
        _lastPingUtc           = DateTime.MinValue;
        _lastPongUtc           = DateTime.MinValue;
        XHomed = YHomed = ZHomed = false;
        XLimitTriggered = YLimitTriggered = ZLimitTriggered = false;

        ConnectVm.ResetDeviceInfo();
    }

    // ════════════════════════════════════════════════════════════════
    // BACKWARD-COMPAT STUBS  (remove after the indicated phase)
    // ════════════════════════════════════════════════════════════════

    /// <summary>No-op. Remove after Phase 3 (ConnectViewModel rework).</summary>
    [Obsolete("Phase 3: ConnectViewModel will use Protocol.SendPing/SendInfo directly.")]
    public void StartPolling() { }

    /// <summary>No-op. Remove after Phase 3.</summary>
    [Obsolete("Phase 3: Remove after ConnectViewModel rework.")]
    public void StopPolling() { }

    /// <summary>No-op. Remove after Phase 4 (ManualControlViewModel rework).</summary>
    [Obsolete("Phase 4: ManualControlViewModel will call Protocol.SendHome() directly.")]
    public void SetAllAxesHomed(bool homed) { }

    /// <summary>No-op. Remove after Phase 4.</summary>
    [Obsolete("Phase 4: Remove after ManualControlViewModel rework.")]
    public void SetAxisHomed(string axis, bool homed) { }

    /// <summary>No-op. Remove after Phase 4.</summary>
    [Obsolete("Phase 4: Remove after ManualControlViewModel rework.")]
    public void ClearLimitStates() { }

    // ════════════════════════════════════════════════════════════════
    // MISC
    // ════════════════════════════════════════════════════════════════

    private void NavigateTo(object page, string pageName)
    {
        if (ReferenceEquals(page, SettingsVm))
            SettingsVm.PrepareForDisplay();

        CurrentPage     = page;
        CurrentPageName = pageName;
    }

    public void NotifySettingsChanged() => RaisePropertyChanged(nameof(Settings));

    public void Dispose()
    {
        _heartbeatTimer.Stop();
        Serial.Dispose();
    }

    private void HandleThemeChanged(object? sender, EventArgs e)
    {
        RaisePropertyChanged(nameof(ConnectionStatusBrush));
        RaisePropertyChanged(nameof(MotionStateBrush));
        RaisePropertyChanged(nameof(SafetyStateBrush));
    }
}



