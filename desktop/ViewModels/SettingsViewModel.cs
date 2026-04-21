using System.Collections.ObjectModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using PortableCncApp.Services;

namespace PortableCncApp.ViewModels;

public sealed class SettingsViewModel : PageViewModelBase
{
    private const double MinStepsPerMm = 1;
    private const double MaxStepsPerMm = 20000;
    private const double MinFeedRate = 1;
    private const double MaxFeedRate = 50000;
    private const double MinAcceleration = 1;
    private const double MaxAcceleration = 10000;
    private const double MinTravel = 1;
    private const double MaxTravel = 5000;
    private const double MinSpindleRpm = 0;
    private const double MaxSpindleRpm = 50000;
    private const double MinTemperature = 0;
    private const double MaxTemperatureLimit = 150;

    private readonly RelayCommand _saveLocalSettingsCommand;
    private readonly RelayCommand _revertChangesCommand;
    private readonly RelayCommand _loadDefaultsCommand;
    private readonly RelayCommand _importSettingsCommand;
    private readonly RelayCommand _exportSettingsCommand;
    private readonly RelayCommand _refreshPortsCommand;

    private AppSettings _persistedSnapshot = new();
    private MachineSettingsSnapshot _machineSnapshot = MachineSettingsSnapshot.Default;
    private bool _isApplyingSnapshot;
    private string? _statusTextOverride;
    private string? _statusDetailOverride;

    private sealed record MachineSettingsSnapshot(
        double StepsPerMmX,
        double StepsPerMmY,
        double StepsPerMmZ,
        double MaxFeedRateX,
        double MaxFeedRateY,
        double MaxFeedRateZ,
        double AccelerationX,
        double AccelerationY,
        double AccelerationZ,
        double MaxTravelX,
        double MaxTravelY,
        double MaxTravelZ,
        bool SoftLimitsEnabled,
        bool HardLimitsEnabled,
        double SpindleMinRpm,
        double SpindleMaxRpm,
        double WarningTemperature,
        double MaxTemperature)
    {
        public static MachineSettingsSnapshot Default { get; } = new(
            800, 800, 800,
            5000, 5000, 1000,
            200, 200, 100,
            300, 200, 100,
            true, true,
            1000, 24000,
            40, 50);
    }

    public ObservableCollection<string> AvailablePorts { get; } = new();
    public IReadOnlyList<string> UnitsOptions { get; } = ["mm", "in"];
    public IReadOnlyList<string> ThemeModeOptions { get; } = ["system", "light", "dark"];

    private string? _lastPort;
    public string? LastPort
    {
        get => _lastPort;
        set => SetLocalProperty(ref _lastPort, NormalizePort(value));
    }

    private bool _autoConnect = true;
    public bool AutoConnect
    {
        get => _autoConnect;
        set => SetLocalProperty(ref _autoConnect, value);
    }

    private string _units = "mm";
    public string Units
    {
        get => _units;
        set => SetLocalProperty(ref _units, NormalizeUnits(value));
    }

    private string _themeMode = "system";
    public string ThemeMode
    {
        get => _themeMode;
        set
        {
            var normalized = NormalizeThemeMode(value);
            if (!SetLocalProperty(ref _themeMode, normalized))
                return;

            App.ApplyThemeMode(normalized);
        }
    }

    private double _stepsPerMmX = 800;
    public double StepsPerMmX
    {
        get => _stepsPerMmX;
        set => SetMachineProperty(ref _stepsPerMmX, Clamp(value, MinStepsPerMm, MaxStepsPerMm));
    }

    private double _stepsPerMmY = 800;
    public double StepsPerMmY
    {
        get => _stepsPerMmY;
        set => SetMachineProperty(ref _stepsPerMmY, Clamp(value, MinStepsPerMm, MaxStepsPerMm));
    }

    private double _stepsPerMmZ = 800;
    public double StepsPerMmZ
    {
        get => _stepsPerMmZ;
        set => SetMachineProperty(ref _stepsPerMmZ, Clamp(value, MinStepsPerMm, MaxStepsPerMm));
    }

    private double _maxFeedRateX = 5000;
    public double MaxFeedRateX
    {
        get => _maxFeedRateX;
        set => SetMachineProperty(ref _maxFeedRateX, Clamp(value, MinFeedRate, MaxFeedRate));
    }

    private double _maxFeedRateY = 5000;
    public double MaxFeedRateY
    {
        get => _maxFeedRateY;
        set => SetMachineProperty(ref _maxFeedRateY, Clamp(value, MinFeedRate, MaxFeedRate));
    }

    private double _maxFeedRateZ = 1000;
    public double MaxFeedRateZ
    {
        get => _maxFeedRateZ;
        set => SetMachineProperty(ref _maxFeedRateZ, Clamp(value, MinFeedRate, MaxFeedRate));
    }

    private double _accelerationX = 200;
    public double AccelerationX
    {
        get => _accelerationX;
        set => SetMachineProperty(ref _accelerationX, Clamp(value, MinAcceleration, MaxAcceleration));
    }

    private double _accelerationY = 200;
    public double AccelerationY
    {
        get => _accelerationY;
        set => SetMachineProperty(ref _accelerationY, Clamp(value, MinAcceleration, MaxAcceleration));
    }

    private double _accelerationZ = 100;
    public double AccelerationZ
    {
        get => _accelerationZ;
        set => SetMachineProperty(ref _accelerationZ, Clamp(value, MinAcceleration, MaxAcceleration));
    }

    private double _maxTravelX = 300;
    public double MaxTravelX
    {
        get => _maxTravelX;
        set => SetMachineProperty(ref _maxTravelX, Clamp(value, MinTravel, MaxTravel));
    }

    private double _maxTravelY = 200;
    public double MaxTravelY
    {
        get => _maxTravelY;
        set => SetMachineProperty(ref _maxTravelY, Clamp(value, MinTravel, MaxTravel));
    }

    private double _maxTravelZ = 100;
    public double MaxTravelZ
    {
        get => _maxTravelZ;
        set => SetMachineProperty(ref _maxTravelZ, Clamp(value, MinTravel, MaxTravel));
    }

    private bool _softLimitsEnabled = true;
    public bool SoftLimitsEnabled
    {
        get => _softLimitsEnabled;
        set => SetMachineProperty(ref _softLimitsEnabled, value);
    }

    private bool _hardLimitsEnabled = true;
    public bool HardLimitsEnabled
    {
        get => _hardLimitsEnabled;
        set => SetMachineProperty(ref _hardLimitsEnabled, value);
    }

    private double _spindleMinRpm = 1000;
    public double SpindleMinRpm
    {
        get => _spindleMinRpm;
        set
        {
            var clamped = Clamp(value, MinSpindleRpm, MaxSpindleRpm);
            var changed = false;

            if (SetProperty(ref _spindleMinRpm, clamped))
                changed = true;

            if (_spindleMaxRpm < _spindleMinRpm)
            {
                _spindleMaxRpm = _spindleMinRpm;
                RaisePropertyChanged(nameof(SpindleMaxRpm));
                changed = true;
            }

            if (changed)
                HandleFieldEdited(isMachineField: true);
        }
    }

    private double _spindleMaxRpm = 24000;
    public double SpindleMaxRpm
    {
        get => _spindleMaxRpm;
        set
        {
            var clamped = Clamp(value, MinSpindleRpm, MaxSpindleRpm);
            var changed = false;

            if (SetProperty(ref _spindleMaxRpm, clamped))
                changed = true;

            if (_spindleMinRpm > _spindleMaxRpm)
            {
                _spindleMinRpm = _spindleMaxRpm;
                RaisePropertyChanged(nameof(SpindleMinRpm));
                changed = true;
            }

            if (changed)
                HandleFieldEdited(isMachineField: true);
        }
    }

    private double _warningTemperature = 40;
    public double WarningTemperature
    {
        get => _warningTemperature;
        set
        {
            var clamped = Clamp(value, MinTemperature, MaxTemperatureLimit);
            var changed = false;

            if (SetProperty(ref _warningTemperature, clamped))
                changed = true;

            if (_maxTemperature < _warningTemperature)
            {
                _maxTemperature = _warningTemperature;
                RaisePropertyChanged(nameof(MaxTemperature));
                changed = true;
            }

            if (changed)
                HandleFieldEdited(isMachineField: true);
        }
    }

    private double _maxTemperature = 50;
    public double MaxTemperature
    {
        get => _maxTemperature;
        set
        {
            var clamped = Clamp(value, MinTemperature, MaxTemperatureLimit);
            var changed = false;

            if (SetProperty(ref _maxTemperature, clamped))
                changed = true;

            if (_warningTemperature > _maxTemperature)
            {
                _warningTemperature = _maxTemperature;
                RaisePropertyChanged(nameof(WarningTemperature));
                changed = true;
            }

            if (changed)
                HandleFieldEdited(isMachineField: true);
        }
    }

    private bool _hasPendingSettingsChanges;
    public bool HasPendingSettingsChanges
    {
        get => _hasPendingSettingsChanges;
        private set => SetProperty(ref _hasPendingSettingsChanges, value);
    }

    public bool HasSavedPort => !string.IsNullOrWhiteSpace(LastPort);
    public bool HasLimitsSafetyWarning => !SoftLimitsEnabled || !HardLimitsEnabled;
    public bool IsPicoConnected => MainVm?.PiConnectionStatus == ConnectionStatus.Connected;
    public bool HasMachineSettingsLoaded => IsPicoConnected;

    public string StatusText => _statusTextOverride ?? BuildStatusText();
    public string StatusDetail => _statusDetailOverride ?? BuildStatusDetail();
    public string SettingsStateText => HasPendingSettingsChanges ? "PENDING" : "SAVED";
    public string LastPortSummary => HasSavedPort ? LastPort! : "No saved port";
    public string UnitsSummary => Units == "in" ? "Inches" : "Millimeters";
    public string AutoConnectSummary => AutoConnect ? "Armed for startup" : "Manual connect only";
    public string ThemeModeSummary => ThemeMode switch
    {
        "light" => "Light",
        "dark" => "Dark",
        _ => "System"
    };
    public string MachineProfileSummary => $"{MaxTravelX:0.#} x {MaxTravelY:0.#} x {MaxTravelZ:0.#} mm envelope";
    public string StepsSummary => $"X {StepsPerMmX:0.#}  Y {StepsPerMmY:0.#}  Z {StepsPerMmZ:0.#}";
    public string MotionSummary => $"Feed {MaxFeedRateX:0.#}/{MaxFeedRateY:0.#}/{MaxFeedRateZ:0.#} mm/min";
    public string AccelerationSummary => $"Accel {AccelerationX:0.#}/{AccelerationY:0.#}/{AccelerationZ:0.#} mm/s^2";
    public string SpindleSummary => $"{SpindleMinRpm:0.#} to {SpindleMaxRpm:0.#} RPM";
    public string TemperatureSummary => $"Warn {WarningTemperature:0.#} C / Max {MaxTemperature:0.#} C";
    public string LimitsProtectionSummary => $"Soft {(SoftLimitsEnabled ? "On" : "Off")} / Hard {(HardLimitsEnabled ? "On" : "Off")}";
    public string LimitsSafetyWarningText => BuildLimitsSafetyWarning();

    public ICommand SaveLocalSettingsCommand => _saveLocalSettingsCommand;
    public ICommand RevertChangesCommand => _revertChangesCommand;
    public ICommand LoadDefaultsCommand => _loadDefaultsCommand;
    public ICommand ImportSettingsCommand => _importSettingsCommand;
    public ICommand ExportSettingsCommand => _exportSettingsCommand;
    public ICommand RefreshPortsCommand => _refreshPortsCommand;

    public SettingsViewModel()
    {
        _saveLocalSettingsCommand = new RelayCommand(SaveLocalSettings, () => HasPendingSettingsChanges);
        _revertChangesCommand = new RelayCommand(RevertChanges, () => HasPendingSettingsChanges);
        _loadDefaultsCommand = new RelayCommand(LoadDefaults);
        _importSettingsCommand = new RelayCommand(ImportSettings);
        _exportSettingsCommand = new RelayCommand(ExportSettings);
        _refreshPortsCommand = new RelayCommand(RefreshAvailablePorts);

        RefreshAvailablePorts();
        UpdateDerivedState();
    }

    protected override void OnMainViewModelSet()
    {
        RefreshAvailablePorts();
        UpdateDerivedState();
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        if (propertyName != nameof(MainWindowViewModel.PiConnectionStatus))
            return;

        ClearStatusOverride();
        UpdateDerivedState();
    }

    public void ApplyFrom(AppSettings settings)
    {
        _isApplyingSnapshot = true;

        LastPort = settings.LastPort;
        AutoConnect = settings.AutoConnect;
        Units = settings.Units;
        ThemeMode = settings.ThemeMode;

        _isApplyingSnapshot = false;
        _persistedSnapshot = settings.Clone();
        ClearStatusOverride();
        RefreshAvailablePorts();
        UpdateDerivedState();
    }

    public void PrepareForDisplay()
    {
        RefreshAvailablePorts();

        if (MainVm == null || HasPendingSettingsChanges)
            return;

        var persisted = MainVm.Settings.Current;
        if (!MatchesLocalSettings(persisted))
            ApplyFrom(persisted);
    }

    private void SaveLocalSettings()
    {
        if (MainVm == null)
            return;

        var current = BuildCurrentSettings();

        MainVm.Settings.Current.CopyFrom(current);
        MainVm.Settings.Save();

        _persistedSnapshot = current.Clone();
        if (HasMachineSettingsLoaded)
            _machineSnapshot = BuildCurrentMachineSettings();
        ClearStatusOverride();
        UpdateDerivedState();
        RefreshAvailablePorts();
        MainVm.NotifySettingsChanged();
        MainVm.StatusMessage = "Settings saved.";
    }

    private void RevertChanges()
    {
        ApplyFrom(_persistedSnapshot);
        if (HasMachineSettingsLoaded)
            ApplyMachineSettingsSnapshot(_machineSnapshot, updateSnapshot: false);

        SetStatusOverride(
            "Reverted",
            "Settings were restored to the last unchanged values.");

        if (MainVm != null)
            MainVm.StatusMessage = "Unsaved settings changes reverted.";
    }

    private void LoadDefaults()
    {
        ApplyValuesWithoutChangingSnapshot(new AppSettings());
        if (HasMachineSettingsLoaded)
            ApplyMachineSettingsSnapshot(MachineSettingsSnapshot.Default, updateSnapshot: false);

        SetStatusOverride(
            "Defaults loaded",
            "Defaults are loaded in memory. Save when you are ready.");

        if (MainVm != null)
            MainVm.StatusMessage = "Default settings loaded into the page.";
    }

    private void ImportSettings()
    {
        SetStatusOverride(
            "Import not yet implemented",
            "Import will load a saved local profile into the page once file flow is added.");

        if (MainVm != null)
            MainVm.StatusMessage = "Import settings is not implemented yet.";
    }

    private void ExportSettings()
    {
        SetStatusOverride(
            "Export not yet implemented",
            "Export will save the local profile to a file once file flow is added.");

        if (MainVm != null)
            MainVm.StatusMessage = "Export settings is not implemented yet.";
    }

    private void ApplyValuesWithoutChangingSnapshot(AppSettings settings)
    {
        _isApplyingSnapshot = true;

        LastPort = settings.LastPort;
        AutoConnect = settings.AutoConnect;
        Units = settings.Units;
        ThemeMode = settings.ThemeMode;

        _isApplyingSnapshot = false;
        RefreshAvailablePorts();
        UpdateDerivedState();
    }

    private bool SetLocalProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!SetProperty(ref field, value, propertyName))
            return false;

        HandleFieldEdited(isMachineField: false);
        return true;
    }

    private bool SetMachineProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!SetProperty(ref field, value, propertyName))
            return false;

        HandleFieldEdited(isMachineField: true);
        return true;
    }

    private void HandleFieldEdited(bool isMachineField)
    {
        if (_isApplyingSnapshot)
            return;

        ClearStatusOverride();
        UpdateDerivedState();
    }

    private void UpdateDerivedState()
    {
        HasPendingSettingsChanges = HasUnsavedLocalDifferences() ||
                                    (HasMachineSettingsLoaded && HasUnsavedMachineDifferences());

        RaisePropertyChanged(nameof(StatusText));
        RaisePropertyChanged(nameof(StatusDetail));
        RaisePropertyChanged(nameof(SettingsStateText));
        RaisePropertyChanged(nameof(IsPicoConnected));
        RaisePropertyChanged(nameof(HasMachineSettingsLoaded));
        RaisePropertyChanged(nameof(HasSavedPort));
        RaisePropertyChanged(nameof(LastPortSummary));
        RaisePropertyChanged(nameof(UnitsSummary));
        RaisePropertyChanged(nameof(AutoConnectSummary));
        RaisePropertyChanged(nameof(ThemeModeSummary));
        RaisePropertyChanged(nameof(MachineProfileSummary));
        RaisePropertyChanged(nameof(StepsSummary));
        RaisePropertyChanged(nameof(MotionSummary));
        RaisePropertyChanged(nameof(AccelerationSummary));
        RaisePropertyChanged(nameof(SpindleSummary));
        RaisePropertyChanged(nameof(TemperatureSummary));
        RaisePropertyChanged(nameof(LimitsProtectionSummary));
        RaisePropertyChanged(nameof(HasLimitsSafetyWarning));
        RaisePropertyChanged(nameof(LimitsSafetyWarningText));

        _saveLocalSettingsCommand.RaiseCanExecuteChanged();
        _revertChangesCommand.RaiseCanExecuteChanged();
    }

    private void RefreshAvailablePorts()
    {
        AvailablePorts.Clear();

        try
        {
            foreach (var port in UsbDeviceService.GetPicoPorts())
            {
                if (!AvailablePorts.Contains(port))
                    AvailablePorts.Add(port);
            }
        }
        catch
        {
            // Ignore device enumeration failures and preserve any saved port.
        }

        if (!string.IsNullOrWhiteSpace(LastPort) && !AvailablePorts.Contains(LastPort))
            AvailablePorts.Add(LastPort);
    }

    private bool HasUnsavedLocalDifferences()
        => !StringEquals(LastPort, _persistedSnapshot.LastPort)
           || AutoConnect != _persistedSnapshot.AutoConnect
           || !StringEquals(Units, _persistedSnapshot.Units)
           || !StringEquals(ThemeMode, _persistedSnapshot.ThemeMode);

    private bool HasUnsavedMachineDifferences()
        => BuildCurrentMachineSettings() != _machineSnapshot;

    private bool MatchesLocalSettings(AppSettings settings)
        => StringEquals(settings.LastPort, LastPort)
           && settings.AutoConnect == AutoConnect
           && StringEquals(settings.Units, Units)
           && StringEquals(settings.ThemeMode, ThemeMode);

    private AppSettings BuildCurrentSettings()
        => new()
        {
            LastPort = NormalizePort(LastPort),
            AutoConnect = AutoConnect,
            Units = NormalizeUnits(Units),
            ThemeMode = NormalizeThemeMode(ThemeMode)
        };

    private MachineSettingsSnapshot BuildCurrentMachineSettings()
        => new(
            StepsPerMmX,
            StepsPerMmY,
            StepsPerMmZ,
            MaxFeedRateX,
            MaxFeedRateY,
            MaxFeedRateZ,
            AccelerationX,
            AccelerationY,
            AccelerationZ,
            MaxTravelX,
            MaxTravelY,
            MaxTravelZ,
            SoftLimitsEnabled,
            HardLimitsEnabled,
            SpindleMinRpm,
            SpindleMaxRpm,
            WarningTemperature,
            MaxTemperature);

    private void ApplyMachineSettingsSnapshot(MachineSettingsSnapshot settings, bool updateSnapshot)
    {
        _isApplyingSnapshot = true;

        StepsPerMmX = settings.StepsPerMmX;
        StepsPerMmY = settings.StepsPerMmY;
        StepsPerMmZ = settings.StepsPerMmZ;
        MaxFeedRateX = settings.MaxFeedRateX;
        MaxFeedRateY = settings.MaxFeedRateY;
        MaxFeedRateZ = settings.MaxFeedRateZ;
        AccelerationX = settings.AccelerationX;
        AccelerationY = settings.AccelerationY;
        AccelerationZ = settings.AccelerationZ;
        MaxTravelX = settings.MaxTravelX;
        MaxTravelY = settings.MaxTravelY;
        MaxTravelZ = settings.MaxTravelZ;
        SoftLimitsEnabled = settings.SoftLimitsEnabled;
        HardLimitsEnabled = settings.HardLimitsEnabled;
        SpindleMinRpm = settings.SpindleMinRpm;
        SpindleMaxRpm = settings.SpindleMaxRpm;
        WarningTemperature = settings.WarningTemperature;
        MaxTemperature = settings.MaxTemperature;

        _isApplyingSnapshot = false;
        if (updateSnapshot)
            _machineSnapshot = settings;
        UpdateDerivedState();
    }

    private string BuildStatusText()
        => HasPendingSettingsChanges ? "Settings pending" : "Settings unchanged";

    private string BuildStatusDetail()
        => HasPendingSettingsChanges
            ? "Visible settings have unsaved changes."
            : "Visible settings match the current unchanged snapshot.";

    private string BuildLimitsSafetyWarning()
    {
        if (!SoftLimitsEnabled && !HardLimitsEnabled)
            return "Warning: both soft and hard limits are disabled.";

        if (!SoftLimitsEnabled)
            return "Warning: soft limits are disabled.";

        if (!HardLimitsEnabled)
            return "Warning: hard limits are disabled.";

        return string.Empty;
    }

    private void SetStatusOverride(string title, string detail)
    {
        _statusTextOverride = title;
        _statusDetailOverride = detail;
        RaisePropertyChanged(nameof(StatusText));
        RaisePropertyChanged(nameof(StatusDetail));
    }

    private void ClearStatusOverride()
    {
        _statusTextOverride = null;
        _statusDetailOverride = null;
        RaisePropertyChanged(nameof(StatusText));
        RaisePropertyChanged(nameof(StatusDetail));
    }

    private static double Clamp(double value, double min, double max) => Math.Clamp(value, min, max);

    private static string? NormalizePort(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return null;

        return value.Trim();
    }

    private static string NormalizeUnits(string? value)
        => string.Equals(value, "in", StringComparison.OrdinalIgnoreCase) ? "in" : "mm";

    private static string NormalizeThemeMode(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "light" => "light",
            "dark" => "dark",
            _ => "system"
        };

    private static bool StringEquals(string? left, string? right)
        => string.Equals(left, right, StringComparison.OrdinalIgnoreCase);
}
