using System.Collections.ObjectModel;
using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class ConnectViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // SERIAL PORT CONNECTION (USB CDC to Pico 2W)
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

    // Common baud rates for selection
    public int[] BaudRateOptions { get; } = { 9600, 19200, 38400, 57600, 115200, 230400, 250000, 500000, 1000000 };

    private bool _isConnecting;
    public bool IsConnecting
    {
        get => _isConnecting;
        set => SetProperty(ref _isConnecting, value);
    }

    // ════════════════════════════════════════════════════════════════
    // DEVICE INFO
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

        // Initialize with available ports
        RefreshPorts();
    }

    private async void Connect()
    {
        if (MainVm == null || SelectedPort == null) return;

        IsConnecting = true;
        MainVm.PiConnectionStatus = ConnectionStatus.Connecting;
        MainVm.StatusMessage = $"Connecting to Pico 2W on {SelectedPort}...";

        try
        {
            // TODO: Actual serial connection logic
            // var serialPort = new SerialPort(SelectedPort, BaudRate);
            // serialPort.Open();
            // Send handshake/version query

            // Simulate connection delay
            await System.Threading.Tasks.Task.Delay(1000);

            // Simulate successful connection
            MainVm.PiConnectionStatus = ConnectionStatus.Connected;
            MainVm.TeensyConnectionStatus = ConnectionStatus.Connected;
            MainVm.MotionState = MotionState.Idle;
            MainVm.SafetyState = SafetyState.SafeIdle;

            // Populate device info (would come from actual device query)
            PicoFirmware = "v1.0.0";
            TeensyFirmware = "v1.2.3";
            GrblVersion = "1.1h";
            PicoSerialNumber = "PICO-2W-001234";

            MainVm.StatusMessage = $"Connected to Pico 2W on {SelectedPort}";
        }
        catch (System.Exception ex)
        {
            MainVm.PiConnectionStatus = ConnectionStatus.Error;
            MainVm.StatusMessage = $"Connection failed: {ex.Message}";
        }
        finally
        {
            IsConnecting = false;
        }
    }

    private void Disconnect()
    {
        if (MainVm == null) return;

        // TODO: Close serial port
        // serialPort?.Close();

        MainVm.PiConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.TeensyConnectionStatus = ConnectionStatus.Disconnected;
        MainVm.MotionState = MotionState.PowerUp;
        MainVm.SafetyState = SafetyState.SafeIdle;

        PicoFirmware = "-";
        TeensyFirmware = "-";
        GrblVersion = "-";
        PicoSerialNumber = "-";

        MainVm.StatusMessage = "Disconnected";
    }

    private void RefreshPorts()
    {
        AvailablePorts.Clear();
        
        try
        {
            // Enumerate available serial ports (works on Windows, macOS, Linux)
            foreach (var port in System.IO.Ports.SerialPort.GetPortNames())
            {
                AvailablePorts.Add(port);
            }
        }
        catch (System.Exception)
        {
            // Fallback if serial port enumeration fails
        }

        // Auto-select first port if available
        if (AvailablePorts.Count > 0 && SelectedPort == null)
            SelectedPort = AvailablePorts[0];
    }

    private void TestConnection()
    {
        if (MainVm == null) return;

        // TODO: Send a ping/status request to verify connection is still alive
        // Send "?" to get GRBL status
        MainVm.StatusMessage = "Testing connection...";
    }
}
