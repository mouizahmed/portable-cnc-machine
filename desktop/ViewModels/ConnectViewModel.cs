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
        set => SetProperty(ref _selectedPort, value);
    }

    private int _baudRate = 115200;
    public int BaudRate
    {
        get => _baudRate;
        set => SetProperty(ref _baudRate, value);
    }

    public int[] BaudRateOptions { get; } = { 9600, 19200, 38400, 57600, 115200, 230400, 250000, 500000, 1000000 };

    private bool _isConnecting;
    public bool IsConnecting
    {
        get => _isConnecting;
        set => SetProperty(ref _isConnecting, value);
    }

    // ════════════════════════════════════════════════════════════════
    // DEVICE INFO (populated from GRBL responses)
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

    private string _grblVersion = "-";
    public string GrblVersion
    {
        get => _grblVersion;
        set => SetProperty(ref _grblVersion, value);
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

    public ICommand ConnectCommand { get; }
    public ICommand DisconnectCommand { get; }
    public ICommand RefreshPortsCommand { get; }
    public ICommand TestConnectionCommand { get; }

    public ConnectViewModel()
    {
        ConnectCommand = new RelayCommand(Connect, () => SelectedPort != null && !IsConnecting);
        DisconnectCommand = new RelayCommand(Disconnect);
        RefreshPortsCommand = new RelayCommand(RefreshPorts);
        TestConnectionCommand = new RelayCommand(TestConnection);

        RefreshPorts();
    }

    private async void Connect()
    {
        if (MainVm == null || SelectedPort == null) return;

        IsConnecting = true;
        MainVm.PiConnectionStatus = ConnectionStatus.Connecting;
        MainVm.StatusMessage = $"Opening {SelectedPort}...";

        var success = MainVm.Serial.Connect(SelectedPort, BaudRate);
        if (!success)
        {
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = "Failed to open port — check port and baud rate";
            IsConnecting = false;
            return;
        }

        // Port is open. Send a wakeup and wait up to 3 s for any response.
        MainVm.StatusMessage = $"Waiting for device on {SelectedPort}...";
        MainVm.Serial.SendRealtime((byte)'?');

        bool responded = false;
        void OnLine(string _) => responded = true;
        MainVm.Serial.LineReceived += OnLine;

        for (int i = 0; i < 30 && !responded; i++)
            await Task.Delay(100);

        MainVm.Serial.LineReceived -= OnLine;

        if (!responded)
        {
            // No device answered — close the port and report the failure
            MainVm.Serial.Disconnect();
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"No response on {SelectedPort} — is the Pico connected?";
            IsConnecting = false;
            return;
        }

        MainVm.PiConnectionStatus = ConnectionStatus.Connected;
        MainVm.StatusMessage = $"Connected on {SelectedPort} — waiting for Teensy...";

        // Request firmware info and start periodic status polling
        MainVm.Serial.SendCommand("$I");
        MainVm.StartPolling();

        IsConnecting = false;
    }

    private void Disconnect()
    {
        if (MainVm == null) return;

        MainVm.StopPolling();
        MainVm.Serial.Disconnect();

        MainVm.PiConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.MotionState = MotionState.PowerUp;
        MainVm.SafetyState = SafetyState.SafeIdle;

        TeensyFirmware = "-";
        GrblVersion = "-";
        PicoSerialNumber = "-";
        PicoFirmware = "-";

        MainVm.StatusMessage = "Disconnected";
    }

    private void RefreshPorts()
    {
        AvailablePorts.Clear();

        try
        {
            foreach (var port in System.IO.Ports.SerialPort.GetPortNames())
                AvailablePorts.Add(port);
        }
        catch { }

        if (AvailablePorts.Count > 0 && SelectedPort == null)
            SelectedPort = AvailablePorts[0];
    }

    private void TestConnection()
    {
        if (MainVm == null) return;

        // Send real-time status query — response will come through OnGrblLine
        MainVm.Serial.SendRealtime((byte)'?');
        MainVm.StatusMessage = "Status query sent";
    }
}
