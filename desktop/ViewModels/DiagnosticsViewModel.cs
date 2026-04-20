using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class DiagnosticsViewModel : PageViewModelBase
{
    public ObservableCollection<LogEntry> LogEntries { get; } = new();
    public int LogEntryCount => LogEntries.Count;
    public string LatestLogSummary => LogEntries.Count == 0
        ? "No console entries yet."
        : $"{LogEntries[^1].Type}: {LogEntries[^1].Message}";
    public string FullLogText => string.Join(Environment.NewLine, LogEntries.Select(entry =>
        $"{entry.FormattedTime} {entry.Type,-8} {entry.Message}"));

    private string _commandInput = string.Empty;
    public string CommandInput
    {
        get => _commandInput;
        set => SetProperty(ref _commandInput, value);
    }

    private double _controllerTemperature;
    public double ControllerTemperature
    {
        get => _controllerTemperature;
        set => SetProperty(ref _controllerTemperature, value);
    }

    private double _electronicsTemperature;
    public double ElectronicsTemperature
    {
        get => _electronicsTemperature;
        set => SetProperty(ref _electronicsTemperature, value);
    }

    private double _spindleTemperature;
    public double SpindleTemperature
    {
        get => _spindleTemperature;
        set => SetProperty(ref _spindleTemperature, value);
    }

    public bool XLimitTriggered => MainVm?.XLimitTriggered == true;
    public bool YLimitTriggered => MainVm?.YLimitTriggered == true;
    public bool ZLimitTriggered => MainVm?.ZLimitTriggered == true;

    public string XLimitText => XLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string YLimitText => YLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string ZLimitText => ZLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string LimitSummaryText => MainVm?.LimitSummaryText ?? "XYZ CLEAR";

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

        AddLog("INFO", "Diagnostics panel initialized");
        AddLog("INFO", "Ready for commands");
    }

    protected override void OnMainViewModelSet()
    {
        RaiseLimitProperties();
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        switch (propertyName)
        {
            case nameof(MainWindowViewModel.XLimitTriggered):
            case nameof(MainWindowViewModel.YLimitTriggered):
            case nameof(MainWindowViewModel.ZLimitTriggered):
            case nameof(MainWindowViewModel.LimitSummaryText):
                RaiseLimitProperties();
                break;
        }
    }

    private void SendCommand()
    {
        if (string.IsNullOrWhiteSpace(CommandInput)) return;

        AddLog("TX", CommandInput);

        // TODO: Wire this through the Pico/Teensy command path.
        AddLog("RX", "ok");

        CommandInput = string.Empty;
    }

    private void RefreshSensors()
    {
        // TODO: Replace placeholder values with thermistor telemetry from the machine.
        ControllerTemperature = MainVm?.Temperature ?? 42.5;
        ElectronicsTemperature = 31.0;
        SpindleTemperature = 35.0;

        AddLog("INFO", "Thermal readings refreshed");
    }

    private async void ResetFault()
    {
        if (MainVm == null) return;

        if (MainVm.MachineState == MachineOperationState.Fault)
        {
            AddLog("INFO", "Reset sent - waiting for machine to return to idle");
            var result = await MainVm.SendCommandAndWaitAsync("RESET", MainVm.Protocol.SendReset, TimeSpan.FromSeconds(3));
            if (!result.Success && result.Kind != MainWindowViewModel.ControllerCommandResultKind.Timeout)
                AddLog("ERROR", $"Reset failed: {result.Message}");
        }
    }

    private async void Unlock()
    {
        if (MainVm == null) return;

        if (MainVm.MachineState == MachineOperationState.Estop)
        {
            AddLog("INFO", "Reset sent - waiting for E-stop to clear");
            var result = await MainVm.SendCommandAndWaitAsync("RESET", MainVm.Protocol.SendReset, TimeSpan.FromSeconds(3));
            if (!result.Success && result.Kind != MainWindowViewModel.ControllerCommandResultKind.Timeout)
                AddLog("ERROR", $"Reset failed: {result.Message}");
        }
    }

    public void AddLog(string type, string message)
    {
        LogEntries.Add(new LogEntry
        {
            Timestamp = DateTime.Now,
            Type = type,
            Message = message
        });

        while (LogEntries.Count > 500)
            LogEntries.RemoveAt(0);

        RaisePropertyChanged(nameof(LogEntryCount));
        RaisePropertyChanged(nameof(LatestLogSummary));
        RaisePropertyChanged(nameof(FullLogText));
    }

    private void ClearLog()
    {
        LogEntries.Clear();
        RaisePropertyChanged(nameof(LogEntryCount));
        RaisePropertyChanged(nameof(LatestLogSummary));
        RaisePropertyChanged(nameof(FullLogText));
    }

    private void RaiseLimitProperties()
    {
        RaisePropertyChanged(nameof(XLimitTriggered));
        RaisePropertyChanged(nameof(YLimitTriggered));
        RaisePropertyChanged(nameof(ZLimitTriggered));
        RaisePropertyChanged(nameof(XLimitText));
        RaisePropertyChanged(nameof(YLimitText));
        RaisePropertyChanged(nameof(ZLimitText));
        RaisePropertyChanged(nameof(LimitSummaryText));
    }
}

public class LogEntry
{
    public DateTime Timestamp { get; set; }
    public string Type { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;

    public string FormattedTime => Timestamp.ToString("HH:mm:ss.fff");
}
