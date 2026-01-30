using System;
using System.Windows.Input;
using Avalonia.Media;

namespace PortableCncApp.ViewModels;

public sealed class MainWindowViewModel : ViewModelBase
{
    // ---- Header bindings ----
    public string ConnectionLabel => IsConnected ? $"CONNECTED {MachineName}" : "DISCONNECTED";
    public string MachineStateLabel => MachineState.ToUpperInvariant();

    // These are placeholders; you can bind to real telemetry later.
    public string LimitsLabel => LimitsTriggered ? "Limits" : "Limits OK";
    public string SpindleLabel => SpindleOn ? "Spindle ON" : "Spindle OFF";

    public IBrush StatePillBrush =>
        MachineState switch
        {
            "ALARM" => new SolidColorBrush(Color.Parse("#D83B3B")),
            "RUN" => new SolidColorBrush(Color.Parse("#3BB273")),
            "PAUSED" => new SolidColorBrush(Color.Parse("#E0A100")),
            _ => new SolidColorBrush(Color.Parse("#3A3A3A")),
        };

    public bool CanStop => IsConnected && (MachineState == "RUN" || MachineState == "PAUSED" || MachineState == "ALARM");

    // ---- Navigation ----
    private object? _currentPage;
    public object? CurrentPage
    {
        get => _currentPage;
        private set => SetProperty(ref _currentPage, value);
    }

    public bool AdvancedEnabled
    {
        get => _advancedEnabled;
        set => SetProperty(ref _advancedEnabled, value);
    }
    private bool _advancedEnabled;

    // ---- Example machine state backing fields ----
    private bool _isConnected;
    public bool IsConnected
    {
        get => _isConnected;
        set
        {
            if (SetProperty(ref _isConnected, value))
            {
                RaiseHeaderChanged();
                RaisePropertyChanged(nameof(CanStop));
            }
        }
    }

    private string _machineName = "CNC-1234";
    public string MachineName
    {
        get => _machineName;
        set
        {
            if (SetProperty(ref _machineName, value))
                RaiseHeaderChanged();
        }
    }

    private string _machineState = "IDLE";
    public string MachineState
    {
        get => _machineState;
        set
        {
            if (SetProperty(ref _machineState, value))
            {
                RaiseHeaderChanged();
                RaisePropertyChanged(nameof(StatePillBrush));
                RaisePropertyChanged(nameof(CanStop));
            }
        }
    }

    private bool _limitsTriggered;
    public bool LimitsTriggered
    {
        get => _limitsTriggered;
        set
        {
            if (SetProperty(ref _limitsTriggered, value))
                RaisePropertyChanged(nameof(LimitsLabel));
        }
    }

    private bool _spindleOn;
    public bool SpindleOn
    {
        get => _spindleOn;
        set
        {
            if (SetProperty(ref _spindleOn, value))
                RaisePropertyChanged(nameof(SpindleLabel));
        }
    }

    // ---- Commands ----
    public ICommand GoRunCommand { get; }
    public ICommand GoJogCommand { get; }
    public ICommand GoFilesCommand { get; }
    public ICommand GoConnectCommand { get; }
    public ICommand GoSettingsCommand { get; }
    public ICommand GoMaintenanceCommand { get; }
    public ICommand StopCommand { get; }

    public MainWindowViewModel(
        RunViewModel runVm,
        JogViewModel jogVm,
        FilesViewModel filesVm,
        ConnectViewModel connectVm,
        SettingsViewModel settingsVm,
        MaintenanceViewModel maintenanceVm)
    {
        GoRunCommand = new RelayCommand(() => CurrentPage = runVm);
        GoJogCommand = new RelayCommand(() => CurrentPage = jogVm);
        GoFilesCommand = new RelayCommand(() => CurrentPage = filesVm);
        GoConnectCommand = new RelayCommand(() => CurrentPage = connectVm);
        GoSettingsCommand = new RelayCommand(() => CurrentPage = settingsVm);
        GoMaintenanceCommand = new RelayCommand(() =>
        {
            if (AdvancedEnabled) CurrentPage = maintenanceVm;
        });

        StopCommand = new RelayCommand(() =>
        {
            // TODO: call your PiApiClient.StopAsync()
            MachineState = "IDLE";
        });

        CurrentPage = runVm;
    }

    private void RaiseHeaderChanged()
    {
        RaisePropertyChanged(nameof(ConnectionLabel));
        RaisePropertyChanged(nameof(MachineStateLabel));
    }
}
