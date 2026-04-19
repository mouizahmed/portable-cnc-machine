using System;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia.Media;
using PortableCncApp.Services;

namespace PortableCncApp.ViewModels;

public sealed class ConnectViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // SERIAL PORT SELECTION
    // ════════════════════════════════════════════════════════════════

    public ObservableCollection<string> AvailablePorts { get; } = new();

    private string? _selectedPort;
    public string? SelectedPort
    {
        get => _selectedPort;
        set
        {
            if (SetProperty(ref _selectedPort, value))
            {
                _connectCommand.RaiseCanExecuteChanged();
                RaiseConnectionStateProperties();
            }
        }
    }

    private bool _isConnecting;
    public bool IsConnecting
    {
        get => _isConnecting;
        set
        {
            if (SetProperty(ref _isConnecting, value))
            {
                _connectCommand.RaiseCanExecuteChanged();
                RaiseConnectionStateProperties();
            }
        }
    }

    public string SelectedPortDisplay => SelectedPort ?? "No port selected";

    public string PortInventorySummary => AvailablePorts.Count switch
    {
        0 => "No compatible USB serial devices detected",
        1 => "1 compatible USB serial device detected",
        _ => $"{AvailablePorts.Count} compatible USB serial devices detected"
    };

    public string ConnectionHeadline
    {
        get
        {
            if (IsConnecting)
                return "CONNECTING";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Error ||
                MainVm?.TeensyConnectionStatus == ConnectionStatus.Error)
                return "ATTENTION REQUIRED";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected &&
                MainVm.TeensyConnectionStatus == ConnectionStatus.Connected)
                return "MACHINE ONLINE";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                return "CONTROLLER LINKED";

            return SelectedPort is null ? "SELECT A PORT" : "READY TO CONNECT";
        }
    }

    public string ConnectionSummary
    {
        get
        {
            if (IsConnecting)
                return "Opening the USB link, handshaking with the controller, then checking the motion controller.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Error ||
                MainVm?.TeensyConnectionStatus == ConnectionStatus.Error)
                return "The last connection attempt failed. Re-scan ports or inspect Diagnostics for the fault.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected &&
                MainVm.TeensyConnectionStatus == ConnectionStatus.Connected)
                return "Both controllers are online and reporting state.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
            {
                return MainVm.TeensyConnectionStatus switch
                {
                    ConnectionStatus.Connecting => "The controller link is healthy. Waiting for the motion controller state.",
                    ConnectionStatus.Disconnected => "The controller link is healthy, but the motion controller is disconnected.",
                    ConnectionStatus.Error => "The controller link is healthy, but the motion controller reported a fault.",
                    _ => "The controller link is healthy. Waiting for the motion controller state."
                };
            }

            return SelectedPort is null
                ? "Scan for the controller USB serial device to begin."
                : "The port is selected and ready for a connection attempt.";
        }
    }

    private bool _lastInfoReceived;

    public string PortSelectionStageText => SelectedPort is null
        ? "Pick a USB serial device to arm the connection."
        : $"{SelectedPort} selected for the USB link.";

    public string PicoStageText
    {
        get
        {
            if (IsConnecting)
                return "Opening the USB link and probing the controller.";

            return MainVm?.PiConnectionStatus switch
            {
                ConnectionStatus.Connected => _lastInfoReceived
                    ? "Controller responded to @PING and @INFO."
                    : "Controller link is up. Waiting for device info/state.",
                ConnectionStatus.Error => "No controller response on the selected port.",
                _ => SelectedPort is null
                    ? "Waiting for a port selection."
                    : "Ready to probe the controller."
            };
        }
    }

    public string TeensyStageText
    {
        get
        {
            if (IsConnecting)
                return "Waiting for the controller handshake before checking the motion controller.";

            if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
                return "Motion controller status depends on the controller link.";

            return MainVm?.TeensyConnectionStatus switch
            {
                ConnectionStatus.Connected => "Motion controller is online.",
                ConnectionStatus.Disconnected => "The controller reported the motion controller as disconnected.",
                ConnectionStatus.Connecting => "Waiting for the controller to finish resolving motion controller state.",
                ConnectionStatus.Error => "Motion controller reported a connection error.",
                _ => "Waiting for the controller to report the motion controller state."
            };
        }
    }

    public string PicoLinkSummary
    {
        get
        {
            if (IsConnecting)
                return "Linking";

            return MainVm?.PiConnectionStatus switch
            {
                ConnectionStatus.Connected => "Online",
                ConnectionStatus.Error => "Fault",
                ConnectionStatus.Disconnected => "Disconnected",
                _ => "--"
            };
        }
    }

    public string TeensyLinkSummary
    {
        get
        {
            if (IsConnecting)
                return "Waiting for Controller";

            if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
                return "Disconnected";

            return MainVm?.TeensyConnectionStatus switch
            {
                ConnectionStatus.Connected => "Online",
                ConnectionStatus.Connecting => "Linking",
                ConnectionStatus.Error => "Fault",
                ConnectionStatus.Disconnected => "Disconnected",
                _ => "--"
            };
        }
    }

    public IBrush PortStageBackground => SelectedPort is null
        ? ThemeResources.Brush("StageWarningBackgroundBrush", "#4A390B")
        : ThemeResources.Brush("StageSuccessBackgroundBrush", "#1F3A2A");

    public IBrush PicoStageBackground => MainVm?.PiConnectionStatus switch
    {
        ConnectionStatus.Connected  => ThemeResources.Brush("StageSuccessBackgroundBrush", "#1F3A2A"),
        ConnectionStatus.Connecting => ThemeResources.Brush("StageWarningBackgroundBrush", "#4A390B"),
        ConnectionStatus.Error      => ThemeResources.Brush("StageDangerBackgroundBrush",  "#4A1616"),
        _ => SelectedPort is null
            ? ThemeResources.Brush("StageNeutralBackgroundBrush", "#252525")
            : ThemeResources.Brush("StageWarningBackgroundBrush", "#4A390B")
    };

    public IBrush TeensyStageBackground => MainVm?.TeensyConnectionStatus switch
    {
        ConnectionStatus.Connected  => ThemeResources.Brush("StageSuccessBackgroundBrush", "#1F3A2A"),
        ConnectionStatus.Connecting => ThemeResources.Brush("StageWarningBackgroundBrush", "#4A390B"),
        ConnectionStatus.Disconnected when MainVm?.PiConnectionStatus == ConnectionStatus.Connected
            => ThemeResources.Brush("StageDangerBackgroundBrush",  "#4A1616"),
        ConnectionStatus.Error      => ThemeResources.Brush("StageDangerBackgroundBrush",  "#4A1616"),
        _ => MainVm?.PiConnectionStatus == ConnectionStatus.Error
            ? ThemeResources.Brush("StageDangerBackgroundBrush",  "#4A1616")
            : MainVm?.PiConnectionStatus == ConnectionStatus.Connected
                ? ThemeResources.Brush("StageWarningBackgroundBrush", "#4A390B")
                : ThemeResources.Brush("StageNeutralBackgroundBrush", "#252525")
    };

    public IBrush PortStageBorderBrush => SelectedPort is null
        ? ThemeResources.Brush("WarningBrush", "#E0A100")
        : ThemeResources.Brush("SuccessBrush", "#3BB273");

    public IBrush PicoStageBorderBrush => MainVm?.PiConnectionStatus switch
    {
        ConnectionStatus.Connected  => ThemeResources.Brush("SuccessBrush",            "#3BB273"),
        ConnectionStatus.Connecting => ThemeResources.Brush("WarningBrush",            "#E0A100"),
        ConnectionStatus.Error      => ThemeResources.Brush("DangerBrush",             "#D83B3B"),
        _ => SelectedPort is null
            ? ThemeResources.Brush("StageNeutralBorderBrush", "#333333")
            : ThemeResources.Brush("WarningBrush",            "#E0A100")
    };

    public IBrush TeensyStageBorderBrush => MainVm?.TeensyConnectionStatus switch
    {
        ConnectionStatus.Connected  => ThemeResources.Brush("SuccessBrush",            "#3BB273"),
        ConnectionStatus.Connecting => ThemeResources.Brush("WarningBrush",            "#E0A100"),
        ConnectionStatus.Disconnected when MainVm?.PiConnectionStatus == ConnectionStatus.Connected
            => ThemeResources.Brush("DangerBrush",             "#D83B3B"),
        ConnectionStatus.Error      => ThemeResources.Brush("DangerBrush",             "#D83B3B"),
        _ => MainVm?.PiConnectionStatus == ConnectionStatus.Error
            ? ThemeResources.Brush("DangerBrush",             "#D83B3B")
            : MainVm?.PiConnectionStatus == ConnectionStatus.Connected
                ? ThemeResources.Brush("WarningBrush",            "#E0A100")
                : ThemeResources.Brush("StageNeutralBorderBrush", "#333333")
    };

    // ════════════════════════════════════════════════════════════════
    // DEVICE INFO  (populated from @INFO response)
    // ════════════════════════════════════════════════════════════════

    private string _picoFirmware = "-";
    public string PicoFirmware
    {
        get => _picoFirmware;
        set => SetProperty(ref _picoFirmware, value);
    }

    private string _picoBoard = "-";
    public string PicoBoard
    {
        get => _picoBoard;
        set => SetProperty(ref _picoBoard, value);
    }

    // Kept for UI compat — not populated by current protocol; remove or wire when Teensy reports version.
    private string _teensyFirmware = "-";
    public string TeensyFirmware
    {
        get => _teensyFirmware;
        set => SetProperty(ref _teensyFirmware, value);
    }

    // Kept for UI compat — old [SN:] field; maps to PicoBoard from @INFO.
    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    private readonly RelayCommand _connectCommand;
    public ICommand ConnectCommand    => _connectCommand;
    public ICommand DisconnectCommand  { get; }
    public ICommand RefreshPortsCommand { get; }

    public ConnectViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;

        _connectCommand       = new RelayCommand(Connect, () => SelectedPort != null && !IsConnecting);
        DisconnectCommand     = new RelayCommand(Disconnect);
        RefreshPortsCommand   = new RelayCommand(RefreshPorts);

        RefreshPorts();
    }

    protected override void OnMainViewModelSet() => RaiseConnectionStateProperties();

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        switch (propertyName)
        {
            case nameof(MainWindowViewModel.PiConnectionStatus):
            case nameof(MainWindowViewModel.TeensyConnectionStatus):
            case nameof(MainWindowViewModel.StatusMessage):
                RaiseConnectionStateProperties();
                break;
        }
    }

    // ════════════════════════════════════════════════════════════════
    // CONNECTION LOGIC
    // ════════════════════════════════════════════════════════════════

    /// <summary>Called by MainWindowViewModel after settings load to attempt auto-connect.</summary>
    public async void TryAutoConnect()
    {
        if (MainVm == null) return;

        var settings = MainVm.Settings.Current;
        if (!settings.AutoConnect || string.IsNullOrEmpty(settings.LastPort)) return;
        if (!AvailablePorts.Contains(settings.LastPort)) return;

        SelectedPort = settings.LastPort;
        await ConnectAsync();
    }

    private async void Connect() => await ConnectAsync();

    private async Task ConnectAsync()
    {
        if (MainVm == null || SelectedPort == null) return;

        IsConnecting = true;
        _lastInfoReceived = false;
        MainVm.PiConnectionStatus = ConnectionStatus.Connecting;
        MainVm.TeensyConnectionStatus = ConnectionStatus.Connecting;
        MainVm.StatusMessage = $"Opening {SelectedPort}...";

        // Step 1: Open serial port
        if (!MainVm.Serial.Connect(SelectedPort))
        {
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Failed to open {SelectedPort}";
            IsConnecting = false;
            return;
        }

        // USB CDC can briefly flap control-line state immediately after open.
        // Give the Pico a moment to settle before the first handshake ping.
        await Task.Delay(350);

        // Step 2: @PING — one-shot liveness probe during connect only.
        MainVm.StatusMessage = $"Waiting for controller on {SelectedPort} (@PING)...";

        var pongTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnPong() => pongTcs.TrySetResult(true);
        MainVm.Protocol.PongReceived += OnPong;
        MainVm.Protocol.SendPing();
        var pongTask = pongTcs.Task;
        var pongCompleted = await Task.WhenAny(pongTask, Task.Delay(3000)) == pongTask;
        MainVm.Protocol.PongReceived -= OnPong;

        if (!pongCompleted)
        {
            MainVm.Serial.Disconnect();
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"No response on {SelectedPort} — is the controller connected?";
            IsConnecting = false;
            return;
        }

        // Step 3: @INFO — wait up to 2 s for device info
        MainVm.StatusMessage = "Querying controller info (@INFO)...";

        var infoTcs = new TaskCompletionSource<PicoInfo>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnInfo(PicoInfo info) => infoTcs.TrySetResult(info);
        MainVm.Protocol.InfoReceived += OnInfo;
        MainVm.Protocol.SendInfo();
        var infoCompleted = await Task.WhenAny(infoTcs.Task, Task.Delay(2000)) == infoTcs.Task;
        MainVm.Protocol.InfoReceived -= OnInfo;

        if (infoCompleted)
        {
            // Step 4: Populate device info from @INFO response
            var picoInfo = await infoTcs.Task;
            _lastInfoReceived = true;
            PicoFirmware = picoInfo.Firmware;
            PicoBoard    = picoInfo.Board;
            MainVm.TeensyConnectionStatus = picoInfo.TeensyConnected
                ? ConnectionStatus.Connected
                : ConnectionStatus.Disconnected;
        }

        // Step 5: Mark Pico connected — Teensy status will come via @EVENT if not already set
        MainVm.Settings.Current.LastPort = SelectedPort;
        MainVm.Settings.Save();

        MainVm.PiConnectionStatus = ConnectionStatus.Connected;
        MainVm.Protocol.SendStatus();
        MainVm.StatusMessage = infoCompleted
            ? $"Connected on {SelectedPort} — firmware {PicoFirmware}"
            : $"Connected on {SelectedPort} — @INFO timed out, waiting for state...";

        IsConnecting = false;
    }

    public void ResetDeviceInfo()
    {
        _lastInfoReceived = false;
        PicoFirmware   = "-";
        PicoBoard      = "-";
        TeensyFirmware = "-";
    }

    private void Disconnect()
    {
        if (MainVm == null) return;

        MainVm.Serial.Disconnect();

        MainVm.PiConnectionStatus     = ConnectionStatus.Disconnected;
        MainVm.TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.StatusMessage          = "Disconnected";

        ResetDeviceInfo();
    }

    private void RefreshPorts()
    {
        AvailablePorts.Clear();

        try
        {
            foreach (var port in PortableCncApp.Services.UsbDeviceService.GetPicoPorts())
                AvailablePorts.Add(port);
        }
        catch { }

        if (SelectedPort is not null && !AvailablePorts.Contains(SelectedPort))
            SelectedPort = null;

        if (AvailablePorts.Count > 0 && SelectedPort == null)
            SelectedPort = AvailablePorts[0];

        RaiseConnectionStateProperties();
    }

    private void RaiseConnectionStateProperties()
    {
        RaisePropertyChanged(nameof(SelectedPortDisplay));
        RaisePropertyChanged(nameof(PortInventorySummary));
        RaisePropertyChanged(nameof(ConnectionHeadline));
        RaisePropertyChanged(nameof(ConnectionSummary));
        RaisePropertyChanged(nameof(PortSelectionStageText));
        RaisePropertyChanged(nameof(PicoStageText));
        RaisePropertyChanged(nameof(TeensyStageText));
        RaisePropertyChanged(nameof(PicoLinkSummary));
        RaisePropertyChanged(nameof(TeensyLinkSummary));
        RaisePropertyChanged(nameof(PortStageBackground));
        RaisePropertyChanged(nameof(PicoStageBackground));
        RaisePropertyChanged(nameof(TeensyStageBackground));
        RaisePropertyChanged(nameof(PortStageBorderBrush));
        RaisePropertyChanged(nameof(PicoStageBorderBrush));
        RaisePropertyChanged(nameof(TeensyStageBorderBrush));
    }

    private void HandleThemeChanged(object? sender, EventArgs e) => RaiseConnectionStateProperties();
}
