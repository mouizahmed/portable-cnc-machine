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
                return "PICO LINKED";

            return SelectedPort is null ? "SELECT A PORT" : "READY TO CONNECT";
        }
    }

    public string ConnectionSummary
    {
        get
        {
            if (IsConnecting)
                return "Handshaking with the Pico over USB serial.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Error ||
                MainVm?.TeensyConnectionStatus == ConnectionStatus.Error)
                return "The last connection attempt failed. Re-scan ports or inspect Diagnostics for the fault.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected &&
                MainVm.TeensyConnectionStatus == ConnectionStatus.Connected)
                return "Both controllers are online and reporting state.";

            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                return "The Pico link is healthy. Waiting for the motion controller to report in.";

            return SelectedPort is null
                ? "Scan for the Pico 2W USB serial device to begin."
                : "The port is selected and ready for a connection attempt.";
        }
    }

    public string PortSelectionStageText => SelectedPort is null
        ? "Pick a serial device to arm the connection."
        : $"{SelectedPort} selected for the handshake.";

    public string PicoStageText => MainVm?.PiConnectionStatus switch
    {
        ConnectionStatus.Connected  => "Pico responded to @PING and @INFO.",
        ConnectionStatus.Connecting => "Probing the Pico with @PING...",
        ConnectionStatus.Error      => "No Pico response on the selected port.",
        _ => SelectedPort is null
            ? "Waiting for a port selection."
            : "Ready to probe the Pico."
    };

    public string TeensyStageText => MainVm?.TeensyConnectionStatus switch
    {
        ConnectionStatus.Connected => "Teensy motion controller is online.",
        ConnectionStatus.Connecting => "Waiting for the motion controller.",
        ConnectionStatus.Error => "Motion controller reported a connection error.",
        _ => MainVm?.PiConnectionStatus == ConnectionStatus.Connected
            ? "Waiting for Teensy to connect via @EVENT."
            : "Teensy handshake starts after the Pico link is up."
    };

    public string PicoLinkSummary => MainVm?.PiConnectionStatus switch
    {
        ConnectionStatus.Connected  => "Online",
        ConnectionStatus.Connecting => "Linking",
        ConnectionStatus.Error      => "Fault",
        _                           => "Idle"
    };

    public string TeensyLinkSummary => MainVm?.TeensyConnectionStatus switch
    {
        ConnectionStatus.Connected  => "Online",
        ConnectionStatus.Connecting => "Linking",
        ConnectionStatus.Error      => "Fault",
        _                           => "Idle"
    };

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
    public string PicoSerialNumber => PicoBoard;

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    private readonly RelayCommand _connectCommand;
    public ICommand ConnectCommand    => _connectCommand;
    public ICommand DisconnectCommand  { get; }
    public ICommand RefreshPortsCommand { get; }
    public ICommand TestConnectionCommand { get; }

    public ConnectViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;

        _connectCommand       = new RelayCommand(Connect, () => SelectedPort != null && !IsConnecting);
        DisconnectCommand     = new RelayCommand(Disconnect);
        RefreshPortsCommand   = new RelayCommand(RefreshPorts);
        TestConnectionCommand = new RelayCommand(TestConnection);

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
        MainVm.PiConnectionStatus = ConnectionStatus.Connecting;
        MainVm.StatusMessage = $"Opening {SelectedPort}...";

        // Step 1: Open serial port
        if (!MainVm.Serial.Connect(SelectedPort))
        {
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Failed to open {SelectedPort}";
            IsConnecting = false;
            return;
        }

        // Step 2: @PING — wait up to 3 s for PONG (handles USB CDC stabilisation delay)
        MainVm.StatusMessage = $"Waiting for Pico on {SelectedPort} (@PING)...";

        var pongTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnPong() => pongTcs.TrySetResult(true);
        MainVm.Protocol.PongReceived += OnPong;

        // Send @PING every 500 ms during the 3 s window so we don't miss the device waking up.
        var pongTask = pongTcs.Task;
        for (int i = 0; i < 6 && !pongTask.IsCompleted; i++)
        {
            MainVm.Protocol.SendPing();
            await Task.WhenAny(pongTask, Task.Delay(500));
        }

        MainVm.Protocol.PongReceived -= OnPong;

        if (!pongTask.IsCompleted)
        {
            MainVm.Serial.Disconnect();
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"No response on {SelectedPort} — is the Pico connected?";
            IsConnecting = false;
            return;
        }

        // Step 3: @INFO — wait up to 2 s for device info
        MainVm.StatusMessage = "Querying Pico info (@INFO)...";

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
            PicoFirmware = picoInfo.Firmware;
            PicoBoard    = picoInfo.Board;

            if (picoInfo.TeensyConnected)
                MainVm.TeensyConnectionStatus = ConnectionStatus.Connected;
        }

        // Step 5: Mark Pico connected — Teensy status will come via @EVENT if not already set
        MainVm.Settings.Current.LastPort = SelectedPort;
        MainVm.Settings.Save();

        MainVm.PiConnectionStatus = ConnectionStatus.Connected;
        MainVm.StatusMessage = infoCompleted
            ? $"Connected on {SelectedPort} — firmware {PicoFirmware}"
            : $"Connected on {SelectedPort} — @INFO timed out, waiting for state...";

        IsConnecting = false;
    }

    public void ResetDeviceInfo()
    {
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

    private void TestConnection()
    {
        if (MainVm == null) return;
        MainVm.Protocol.SendPing();
        MainVm.StatusMessage = "@PING sent";
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
