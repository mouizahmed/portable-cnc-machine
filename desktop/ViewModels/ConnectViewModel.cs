using System;
using System.Collections.Generic;
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
    private readonly Dictionary<string, UsbCdcPortPair> _portPairsByAppPort = new(StringComparer.OrdinalIgnoreCase);

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

    public string SelectedPortDisplay => SelectedPort != null && _portPairsByAppPort.TryGetValue(SelectedPort, out var pair)
        ? pair.DisplayName
        : SelectedPort ?? "No port selected";

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

            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Error)
                return "ATTENTION REQUIRED";

            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                return "CONTROLLER ONLINE";

            return SelectedPort is null ? "SELECT A PORT" : "READY TO CONNECT";
        }
    }

    public string ConnectionSummary
    {
        get
        {
            if (IsConnecting)
                return "Opening the USB link and handshaking with the controller.";

            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Error)
                return "The last connection attempt failed. Re-scan ports or inspect Diagnostics for the fault.";

            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                return "The controller is online and reporting state.";

            return SelectedPort is null
                ? "Scan for the controller USB serial device to begin."
                : "The port is selected and ready for a connection attempt.";
        }
    }

    // ════════════════════════════════════════════════════════════════
    // DEVICE INFO
    // ════════════════════════════════════════════════════════════════

    private string _controllerFirmware = "-";
    public string ControllerFirmware
    {
        get => _controllerFirmware;
        set => SetProperty(ref _controllerFirmware, value);
    }

    private string _controllerBoard = "-";
    public string ControllerBoard
    {
        get => _controllerBoard;
        set => SetProperty(ref _controllerBoard, value);
    }
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
            case nameof(MainWindowViewModel.ControllerConnectionStatus):
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

        if (!_portPairsByAppPort.TryGetValue(SelectedPort, out var selectedPair))
        {
            selectedPair = UsbDeviceService.FindPortableCncPairByAppPort(SelectedPort)
                ?? new UsbCdcPortPair($"Portable CNC ({SelectedPort} app)", "", SelectedPort, "manual");
        }

        IsConnecting = true;
        MainVm.ControllerConnectionStatus = ConnectionStatus.Connecting;
        MainVm.StatusMessage = $"Opening app {selectedPair.AppPort} and GRBL {selectedPair.GrblPort}...";

        if (!string.IsNullOrWhiteSpace(selectedPair.GrblPort) &&
            !MainVm.GrblSerial.Connect(selectedPair.GrblPort))
        {
            MainVm.ControllerConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Failed to open GRBL port {selectedPair.GrblPort}";
            IsConnecting = false;
            return;
        }

        if (!MainVm.Serial.Connect(selectedPair.AppPort))
        {
            MainVm.GrblSerial.Disconnect();
            MainVm.ControllerConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Failed to open app protocol port {selectedPair.AppPort}";
            IsConnecting = false;
            return;
        }

        // USB CDC can briefly flap control-line state immediately after open.
        // Give the controller a moment to settle before the first handshake ping.
        await Task.Delay(350);

        // Step 2: PING - one-shot liveness probe during connect only.
        MainVm.StatusMessage = $"Waiting for app protocol on {selectedPair.AppPort} (PING)...";

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
            MainVm.GrblSerial.Disconnect();
            MainVm.ControllerConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"No app-protocol response on {selectedPair.AppPort}";
            IsConnecting = false;
            return;
        }

        MainVm.StatusMessage = "Querying controller info...";

        var infoTcs = new TaskCompletionSource<ControllerInfo>(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnInfo(ControllerInfo info) => infoTcs.TrySetResult(info);
        MainVm.Protocol.InfoReceived += OnInfo;
        MainVm.Protocol.SendInfo();
        var infoCompleted = await Task.WhenAny(infoTcs.Task, Task.Delay(2000)) == infoTcs.Task;
        MainVm.Protocol.InfoReceived -= OnInfo;

        if (infoCompleted)
        {
            var controllerInfo = await infoTcs.Task;
            ControllerFirmware = controllerInfo.Firmware;
            ControllerBoard    = controllerInfo.Board;
        }

        // Step 5: Mark the controller connected; state events can refine status afterward.
        MainVm.Settings.Current.LastPort = selectedPair.AppPort;
        MainVm.Settings.Save();

        MainVm.ControllerConnectionStatus = ConnectionStatus.Connected;
        MainVm.Protocol.SendStatus();
        MainVm.StatusMessage = infoCompleted
            ? $"Connected: app {selectedPair.AppPort}, GRBL {selectedPair.GrblPort}"
            : $"Connected on {selectedPair.AppPort} - INFO timed out, waiting for state...";

        IsConnecting = false;
    }

    public void ResetDeviceInfo()
    {
        ControllerFirmware = "-";
        ControllerBoard    = "-";
    }

    private void Disconnect()
    {
        if (MainVm == null) return;

        MainVm.Serial.Disconnect();
        MainVm.GrblSerial.Disconnect();

        MainVm.ControllerConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.StatusMessage          = "Disconnected";

        ResetDeviceInfo();
    }

    private void RefreshPorts()
    {
        AvailablePorts.Clear();
        _portPairsByAppPort.Clear();

        try
        {
            foreach (var pair in UsbDeviceService.GetPortableCncPortPairs())
            {
                _portPairsByAppPort[pair.AppPort] = pair;
                AvailablePorts.Add(pair.AppPort);
            }
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
    }

    private void HandleThemeChanged(object? sender, EventArgs e) => RaiseConnectionStateProperties();
}
