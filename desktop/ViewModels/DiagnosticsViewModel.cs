using System;
using System.Collections.ObjectModel;
using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class DiagnosticsViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // CONSOLE
    // ════════════════════════════════════════════════════════════════

    public ObservableCollection<LogEntry> LogEntries { get; } = new();
    public int LogEntryCount => LogEntries.Count;
    public string LatestLogSummary => LogEntries.Count == 0
        ? "No console entries yet."
        : $"{LogEntries[^1].Type}: {LogEntries[^1].Message}";

    private string _commandInput = "";
    public string CommandInput
    {
        get => _commandInput;
        set => SetProperty(ref _commandInput, value);
    }

    // ════════════════════════════════════════════════════════════════
    // SENSOR DATA
    // ════════════════════════════════════════════════════════════════

    private double _cpuTemperature;
    public double CpuTemperature { get => _cpuTemperature; set => SetProperty(ref _cpuTemperature, value); }

    private double _ambientTemperature;
    public double AmbientTemperature { get => _ambientTemperature; set => SetProperty(ref _ambientTemperature, value); }

    private double _spindleTemperature;
    public double SpindleTemperature { get => _spindleTemperature; set => SetProperty(ref _spindleTemperature, value); }

    private double _humidity;
    public double Humidity { get => _humidity; set => SetProperty(ref _humidity, value); }

    private double _vibrationLevel;
    public double VibrationLevel { get => _vibrationLevel; set => SetProperty(ref _vibrationLevel, value); }

    // ════════════════════════════════════════════════════════════════
    // DRIVER STATUS
    // ════════════════════════════════════════════════════════════════

    private bool _xDriverOk = true;
    public bool XDriverOk { get => _xDriverOk; set => SetProperty(ref _xDriverOk, value); }

    private bool _yDriverOk = true;
    public bool YDriverOk { get => _yDriverOk; set => SetProperty(ref _yDriverOk, value); }

    private bool _zDriverOk = true;
    public bool ZDriverOk { get => _zDriverOk; set => SetProperty(ref _zDriverOk, value); }

    private bool _spindleDriverOk = true;
    public bool SpindleDriverOk { get => _spindleDriverOk; set => SetProperty(ref _spindleDriverOk, value); }

    // ════════════════════════════════════════════════════════════════
    // LIMIT SWITCHES
    // ════════════════════════════════════════════════════════════════

    private bool _xMinLimit;
    public bool XMinLimit { get => _xMinLimit; set => SetProperty(ref _xMinLimit, value); }

    private bool _xMaxLimit;
    public bool XMaxLimit { get => _xMaxLimit; set => SetProperty(ref _xMaxLimit, value); }

    private bool _yMinLimit;
    public bool YMinLimit { get => _yMinLimit; set => SetProperty(ref _yMinLimit, value); }

    private bool _yMaxLimit;
    public bool YMaxLimit { get => _yMaxLimit; set => SetProperty(ref _yMaxLimit, value); }

    private bool _zMinLimit;
    public bool ZMinLimit { get => _zMinLimit; set => SetProperty(ref _zMinLimit, value); }

    private bool _zMaxLimit;
    public bool ZMaxLimit { get => _zMaxLimit; set => SetProperty(ref _zMaxLimit, value); }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand SendCommandCommand { get; }
    public ICommand ClearLogCommand { get; }
    public ICommand RefreshSensorsCommand { get; }
    public ICommand ResetFaultCommand { get; }
    public ICommand UnlockCommand { get; }

    public DiagnosticsViewModel()
    {
        SendCommandCommand = new RelayCommand(SendCommand);
        ClearLogCommand = new RelayCommand(ClearLog);
        RefreshSensorsCommand = new RelayCommand(RefreshSensors);
        ResetFaultCommand = new RelayCommand(ResetFault);
        UnlockCommand = new RelayCommand(Unlock);

        // Add some demo log entries
        AddLog("INFO", "Diagnostics panel initialized");
        AddLog("INFO", "Ready for commands");
    }

    private void SendCommand()
    {
        if (string.IsNullOrWhiteSpace(CommandInput)) return;

        AddLog("TX", CommandInput);

        // TODO: Actually send command to machine
        // Simulate response
        AddLog("RX", "ok");

        CommandInput = "";
    }

    private void RefreshSensors()
    {
        // TODO: Query actual sensor values
        CpuTemperature = 42.5;
        AmbientTemperature = MainVm?.Temperature ?? 24.0;
        SpindleTemperature = 35.0;
        Humidity = MainVm?.Humidity ?? 45.0;
        VibrationLevel = 0.02;

        AddLog("INFO", "Sensors refreshed");
    }

    private void ResetFault()
    {
        if (MainVm == null) return;

        if (MainVm.MotionState == MotionState.Fault)
        {
            MainVm.MotionState = MotionState.Idle;
            MainVm.StatusMessage = "Fault cleared";
            AddLog("INFO", "Fault reset - Machine now IDLE");
        }
    }

    private void Unlock()
    {
        if (MainVm == null) return;

        if (MainVm.MotionState == MotionState.EStopLatched)
        {
            MainVm.MotionState = MotionState.Idle;
            MainVm.SafetyState = SafetyState.SafeIdle;
            MainVm.StatusMessage = "E-Stop cleared - Machine unlocked";
            MainVm.IsStatusError = false;
            AddLog("INFO", "E-Stop cleared - Machine unlocked");
        }
    }

    private void AddLog(string type, string message)
    {
        LogEntries.Add(new LogEntry
        {
            Timestamp = DateTime.Now,
            Type = type,
            Message = message
        });
        RaisePropertyChanged(nameof(LogEntryCount));
        RaisePropertyChanged(nameof(LatestLogSummary));
    }

    private void ClearLog()
    {
        LogEntries.Clear();
        RaisePropertyChanged(nameof(LogEntryCount));
        RaisePropertyChanged(nameof(LatestLogSummary));
    }
}

public class LogEntry
{
    public DateTime Timestamp { get; set; }
    public string Type { get; set; } = "";
    public string Message { get; set; } = "";
    
    public string FormattedTime => Timestamp.ToString("HH:mm:ss.fff");
}
