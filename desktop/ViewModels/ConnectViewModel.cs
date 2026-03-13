using System.Collections.ObjectModel;
using System.Threading.Tasks;
using System.Windows.Input;

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
        set { if (SetProperty(ref _selectedPort, value)) _connectCommand.RaiseCanExecuteChanged(); }
    }

    private bool _isConnecting;
    public bool IsConnecting
    {
        get => _isConnecting;
        set { if (SetProperty(ref _isConnecting, value)) _connectCommand.RaiseCanExecuteChanged(); }
    }

    // ════════════════════════════════════════════════════════════════
    // DEVICE INFO (populated from device responses)
    // ════════════════════════════════════════════════════════════════

    private string _picoFirmware = "-";
    public string PicoFirmware
    {
        get => _picoFirmware;
        set => SetProperty(ref _picoFirmware, value);
    }

    private string _teensyFirmware = "-";
    public string TeensyFirmware
    {
        get => _teensyFirmware;
        set => SetProperty(ref _teensyFirmware, value);
    }

    private string _picoSerialNumber = "-";
    public string PicoSerialNumber
    {
        get => _picoSerialNumber;
        set => SetProperty(ref _picoSerialNumber, value);
    }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    private readonly RelayCommand _connectCommand;
    public ICommand ConnectCommand => _connectCommand;
    public ICommand DisconnectCommand { get; }
    public ICommand RefreshPortsCommand { get; }
    public ICommand TestConnectionCommand { get; }

    public ConnectViewModel()
    {
        _connectCommand = new RelayCommand(Connect, () => SelectedPort != null && !IsConnecting);
        DisconnectCommand = new RelayCommand(Disconnect);
        RefreshPortsCommand = new RelayCommand(RefreshPorts);
        TestConnectionCommand = new RelayCommand(TestConnection);

        RefreshPorts();
    }

    /// <summary>Called by MainWindowViewModel after settings are loaded to trigger auto-connect.</summary>
    public async void TryAutoConnect()
    {
        if (MainVm == null) return;

        var settings = MainVm.Settings.Current;
        if (!settings.AutoConnect || string.IsNullOrEmpty(settings.LastPort)) return;

        // Only attempt if the saved port is actually available
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

        var success = MainVm.Serial.Connect(SelectedPort);
        if (!success)
        {
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Failed to open {SelectedPort}";
            IsConnecting = false;
            return;
        }

        // Port is open — probe every 200 ms for up to 3 s to handle USB CDC stabilisation
        MainVm.StatusMessage = $"Waiting for device on {SelectedPort}...";

        bool responded = false;
        void OnLine(string line) { if (line.StartsWith("[PICO:")) responded = true; }
        MainVm.Serial.LineReceived += OnLine;

        for (int i = 0; i < 15 && !responded; i++)
        {
            MainVm.Serial.SendRealtime((byte)'?');
            await Task.Delay(200);
        }

        MainVm.Serial.LineReceived -= OnLine;

        if (!responded)
        {
            MainVm.Serial.Disconnect();
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"No response on {SelectedPort} — is the Pico connected?";
            IsConnecting = false;
            return;
        }

        // Save the port that worked
        MainVm.Settings.Current.LastPort = SelectedPort;
        MainVm.Settings.Save();

        MainVm.PiConnectionStatus = ConnectionStatus.Connected;
        MainVm.StatusMessage = $"Connected on {SelectedPort} — waiting for Teensy...";

        MainVm.Serial.SendCommand("$I");
        MainVm.StartPolling();

        IsConnecting = false;
    }

    public void ResetDeviceInfo()
    {
        PicoFirmware     = "-";
        TeensyFirmware   = "-";
        PicoSerialNumber = "-";
    }

    private void Disconnect()
    {
        if (MainVm == null) return;

        MainVm.StopPolling();
        MainVm.Serial.Disconnect();

        MainVm.PiConnectionStatus     = ConnectionStatus.Disconnected;
        MainVm.TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.MotionState            = MotionState.PowerUp;
        MainVm.SafetyState            = SafetyState.SafeIdle;
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

        if (AvailablePorts.Count > 0 && SelectedPort == null)
            SelectedPort = AvailablePorts[0];
    }

    private void TestConnection()
    {
        if (MainVm == null) return;
        MainVm.Serial.SendRealtime((byte)'?');
        MainVm.StatusMessage = "Status query sent";
    }
}
