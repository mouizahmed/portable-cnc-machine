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

    private double _controllerTemperature = double.NaN;
    public double ControllerTemperature
    {
        get => _controllerTemperature;
        set
        {
            if (SetProperty(ref _controllerTemperature, value))
                RaisePropertyChanged(nameof(ControllerTemperatureText));
        }
    }
    public string ControllerTemperatureText => double.IsNaN(ControllerTemperature) ? "Unavailable" : $"{ControllerTemperature:F1} C";

    private double _electronicsTemperature = double.NaN;
    public double ElectronicsTemperature
    {
        get => _electronicsTemperature;
        set
        {
            if (SetProperty(ref _electronicsTemperature, value))
                RaisePropertyChanged(nameof(ElectronicsTemperatureText));
        }
    }
    public string ElectronicsTemperatureText => double.IsNaN(ElectronicsTemperature) ? "Unavailable" : $"{ElectronicsTemperature:F1} C";

    private double _spindleTemperature = double.NaN;
    public double SpindleTemperature
    {
        get => _spindleTemperature;
        set
        {
            if (SetProperty(ref _spindleTemperature, value))
                RaisePropertyChanged(nameof(SpindleTemperatureText));
        }
    }
    public string SpindleTemperatureText => double.IsNaN(SpindleTemperature) ? "Unavailable" : $"{SpindleTemperature:F1} C";

    public bool XLimitTriggered => MainVm?.XLimitTriggered == true;
    public bool YLimitTriggered => MainVm?.YLimitTriggered == true;
    public bool ZLimitTriggered => MainVm?.ZLimitTriggered == true;
    public bool XMinLimitTriggered => MainVm?.XMinLimitTriggered == true;
    public bool XMaxLimitTriggered => MainVm?.XMaxLimitTriggered == true;
    public bool YMinLimitTriggered => MainVm?.YMinLimitTriggered == true;
    public bool YMaxLimitTriggered => MainVm?.YMaxLimitTriggered == true;
    public bool ZMinLimitTriggered => MainVm?.ZMinLimitTriggered == true;
    public bool ZMaxLimitTriggered => MainVm?.ZMaxLimitTriggered == true;

    public string XLimitText => XLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string YLimitText => YLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string ZLimitText => ZLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string XMinLimitText => XMinLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string XMaxLimitText => XMaxLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string YMinLimitText => YMinLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string YMaxLimitText => YMaxLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string ZMinLimitText => ZMinLimitTriggered ? "TRIGGERED" : "CLEAR";
    public string ZMaxLimitText => ZMaxLimitTriggered ? "TRIGGERED" : "CLEAR";
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
            case nameof(MainWindowViewModel.XMinLimitTriggered):
            case nameof(MainWindowViewModel.XMaxLimitTriggered):
            case nameof(MainWindowViewModel.YMinLimitTriggered):
            case nameof(MainWindowViewModel.YMaxLimitTriggered):
            case nameof(MainWindowViewModel.ZMinLimitTriggered):
            case nameof(MainWindowViewModel.ZMaxLimitTriggered):
            case nameof(MainWindowViewModel.LimitSummaryText):
                RaiseLimitProperties();
                break;
        }
    }

    private void SendCommand()
    {
        if (string.IsNullOrWhiteSpace(CommandInput)) return;

        AddLog("BLOCKED", $"Raw command rejected in binary protocol mode: {CommandInput}");

        CommandInput = string.Empty;
    }

    private void RefreshSensors()
    {
        ControllerTemperature = double.NaN;
        ElectronicsTemperature = double.NaN;
        SpindleTemperature = double.NaN;

        MainVm?.Protocol.SendStatus();
        AddLog("INFO", "Thermal telemetry refresh requested");
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
        RaisePropertyChanged(nameof(XMinLimitTriggered));
        RaisePropertyChanged(nameof(XMaxLimitTriggered));
        RaisePropertyChanged(nameof(YMinLimitTriggered));
        RaisePropertyChanged(nameof(YMaxLimitTriggered));
        RaisePropertyChanged(nameof(ZMinLimitTriggered));
        RaisePropertyChanged(nameof(ZMaxLimitTriggered));
        RaisePropertyChanged(nameof(XLimitText));
        RaisePropertyChanged(nameof(YLimitText));
        RaisePropertyChanged(nameof(ZLimitText));
        RaisePropertyChanged(nameof(XMinLimitText));
        RaisePropertyChanged(nameof(XMaxLimitText));
        RaisePropertyChanged(nameof(YMinLimitText));
        RaisePropertyChanged(nameof(YMaxLimitText));
        RaisePropertyChanged(nameof(ZMinLimitText));
        RaisePropertyChanged(nameof(ZMaxLimitText));
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
