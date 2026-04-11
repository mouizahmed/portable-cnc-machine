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
    private readonly RelayCommand _applyMachineSettingsCommand;
    private readonly RelayCommand _importSettingsCommand;
    private readonly RelayCommand _exportSettingsCommand;
    private readonly RelayCommand _refreshPortsCommand;

    private AppSettings _persistedSnapshot = new();
    private bool _isApplyingSnapshot;
    private string? _statusTextOverride;
    private string? _statusDetailOverride;

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

    private bool _hasUnsavedLocalChanges;
    public bool HasUnsavedLocalChanges
    {
        get => _hasUnsavedLocalChanges;
        private set => SetProperty(ref _hasUnsavedLocalChanges, value);
    }

    private bool _hasPendingMachineChanges;
    public bool HasPendingMachineChanges
    {
        get => _hasPendingMachineChanges;
        private set => SetProperty(ref _hasPendingMachineChanges, value);
    }

    public bool HasSavedPort => !string.IsNullOrWhiteSpace(LastPort);
    public bool HasLimitsSafetyWarning => !SoftLimitsEnabled || !HardLimitsEnabled;
    public bool CanApplyMachineSettings => false;

    public string StatusText => _statusTextOverride ?? BuildStatusText();
    public string StatusDetail => _statusDetailOverride ?? BuildStatusDetail();
    public string LocalPersistenceText => HasUnsavedLocalChanges ? "Local save pending" : "Saved on this computer";
    public string MachineSyncStatusText => HasPendingMachineChanges ? "Not yet synced to machine" : "Controller profile unchanged";
    public string MachineSyncCaption => HasPendingMachineChanges
        ? "Machine-facing values are staged locally. Apply to machine is not implemented in v1."
        : "No controller profile changes are waiting for machine writeback.";
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
    public ICommand ApplyMachineSettingsCommand => _applyMachineSettingsCommand;
    public ICommand ImportSettingsCommand => _importSettingsCommand;
    public ICommand ExportSettingsCommand => _exportSettingsCommand;
    public ICommand RefreshPortsCommand => _refreshPortsCommand;

    public SettingsViewModel()
    {
        _saveLocalSettingsCommand = new RelayCommand(SaveLocalSettings, () => HasUnsavedLocalChanges);
        _revertChangesCommand = new RelayCommand(RevertChanges, () => HasUnsavedLocalChanges);
        _loadDefaultsCommand = new RelayCommand(LoadDefaults);
        _applyMachineSettingsCommand = new RelayCommand(ApplyMachineSettings, () => CanApplyMachineSettings);
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

    public void ApplyFrom(AppSettings settings)
    {
        _isApplyingSnapshot = true;

        LastPort = settings.LastPort;
        AutoConnect = settings.AutoConnect;
        Units = settings.Units;
        ThemeMode = settings.ThemeMode;
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
        _persistedSnapshot = settings.Clone();
        ClearStatusOverride();
        RefreshAvailablePorts();
        UpdateDerivedState();
    }

    public void PrepareForDisplay()
    {
        RefreshAvailablePorts();

        if (MainVm == null || HasUnsavedLocalChanges)
            return;

        var persisted = MainVm.Settings.Current;
        if (!MatchesAllSettings(persisted))
            ApplyFrom(persisted);
    }

    private void SaveLocalSettings()
    {
        if (MainVm == null)
            return;

        var machinePendingAfterSave = _persistedSnapshot.MachineSettingsSyncPending || HasUnsavedMachineDifferences();
        var current = BuildCurrentSettings();
        current.MachineSettingsSyncPending = machinePendingAfterSave;

        MainVm.Settings.Current.CopyFrom(current);
        MainVm.Settings.Save();

        _persistedSnapshot = current.Clone();
        ClearStatusOverride();
        UpdateDerivedState();
        RefreshAvailablePorts();
        MainVm.NotifySettingsChanged();
        MainVm.StatusMessage = machinePendingAfterSave
            ? "Settings saved locally. Machine sync is still pending."
            : "Settings saved locally.";
    }

    private void RevertChanges()
    {
        ApplyFrom(_persistedSnapshot);

        SetStatusOverride(
            "Saved locally",
            "Page values were reverted to the last settings saved on this computer.");

        if (MainVm != null)
            MainVm.StatusMessage = "Unsaved settings changes reverted.";
    }

    private void LoadDefaults()
    {
        ApplyValuesWithoutChangingSnapshot(new AppSettings());

        SetStatusOverride(
            "Defaults loaded",
            "Factory-style defaults are loaded in memory. Save locally when you are ready.");

        if (MainVm != null)
            MainVm.StatusMessage = "Default settings loaded into the page.";
    }

    private void ApplyMachineSettings()
    {
        SetStatusOverride(
            "Apply not yet implemented",
            "Controller writeback has not been wired up yet. Save locally stores the staged profile only.");

        if (MainVm != null)
            MainVm.StatusMessage = "Apply to machine is not implemented yet.";
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

        if (isMachineField && MainVm != null)
            MainVm.StatusMessage = "Machine profile edited locally. Save locally does not write to the controller.";
    }

    private void UpdateDerivedState()
    {
        HasUnsavedLocalChanges = HasUnsavedLocalDifferences();
        HasPendingMachineChanges = _persistedSnapshot.MachineSettingsSyncPending || HasUnsavedMachineDifferences();

        RaisePropertyChanged(nameof(StatusText));
        RaisePropertyChanged(nameof(StatusDetail));
        RaisePropertyChanged(nameof(LocalPersistenceText));
        RaisePropertyChanged(nameof(MachineSyncStatusText));
        RaisePropertyChanged(nameof(MachineSyncCaption));
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
        RaisePropertyChanged(nameof(CanApplyMachineSettings));

        _saveLocalSettingsCommand.RaiseCanExecuteChanged();
        _revertChangesCommand.RaiseCanExecuteChanged();
        _applyMachineSettingsCommand.RaiseCanExecuteChanged();
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
           || !StringEquals(ThemeMode, _persistedSnapshot.ThemeMode)
           || HasUnsavedMachineDifferences();

    private bool HasUnsavedMachineDifferences()
        => StepsPerMmX != _persistedSnapshot.StepsPerMmX
           || StepsPerMmY != _persistedSnapshot.StepsPerMmY
           || StepsPerMmZ != _persistedSnapshot.StepsPerMmZ
           || MaxFeedRateX != _persistedSnapshot.MaxFeedRateX
           || MaxFeedRateY != _persistedSnapshot.MaxFeedRateY
           || MaxFeedRateZ != _persistedSnapshot.MaxFeedRateZ
           || AccelerationX != _persistedSnapshot.AccelerationX
           || AccelerationY != _persistedSnapshot.AccelerationY
           || AccelerationZ != _persistedSnapshot.AccelerationZ
           || MaxTravelX != _persistedSnapshot.MaxTravelX
           || MaxTravelY != _persistedSnapshot.MaxTravelY
           || MaxTravelZ != _persistedSnapshot.MaxTravelZ
           || SoftLimitsEnabled != _persistedSnapshot.SoftLimitsEnabled
           || HardLimitsEnabled != _persistedSnapshot.HardLimitsEnabled
           || SpindleMinRpm != _persistedSnapshot.SpindleMinRpm
           || SpindleMaxRpm != _persistedSnapshot.SpindleMaxRpm
           || WarningTemperature != _persistedSnapshot.WarningTemperature
           || MaxTemperature != _persistedSnapshot.MaxTemperature;

    private bool MatchesAllSettings(AppSettings settings)
        => StringEquals(settings.LastPort, LastPort)
           && settings.AutoConnect == AutoConnect
           && StringEquals(settings.Units, Units)
           && StringEquals(settings.ThemeMode, ThemeMode)
           && settings.StepsPerMmX == StepsPerMmX
           && settings.StepsPerMmY == StepsPerMmY
           && settings.StepsPerMmZ == StepsPerMmZ
           && settings.MaxFeedRateX == MaxFeedRateX
           && settings.MaxFeedRateY == MaxFeedRateY
           && settings.MaxFeedRateZ == MaxFeedRateZ
           && settings.AccelerationX == AccelerationX
           && settings.AccelerationY == AccelerationY
           && settings.AccelerationZ == AccelerationZ
           && settings.MaxTravelX == MaxTravelX
           && settings.MaxTravelY == MaxTravelY
           && settings.MaxTravelZ == MaxTravelZ
           && settings.SoftLimitsEnabled == SoftLimitsEnabled
           && settings.HardLimitsEnabled == HardLimitsEnabled
           && settings.SpindleMinRpm == SpindleMinRpm
           && settings.SpindleMaxRpm == SpindleMaxRpm
           && settings.WarningTemperature == WarningTemperature
           && settings.MaxTemperature == MaxTemperature
           && settings.MachineSettingsSyncPending == _persistedSnapshot.MachineSettingsSyncPending;

    private AppSettings BuildCurrentSettings()
        => new()
        {
            LastPort = NormalizePort(LastPort),
            AutoConnect = AutoConnect,
            Units = NormalizeUnits(Units),
            ThemeMode = NormalizeThemeMode(ThemeMode),
            StepsPerMmX = StepsPerMmX,
            StepsPerMmY = StepsPerMmY,
            StepsPerMmZ = StepsPerMmZ,
            MaxFeedRateX = MaxFeedRateX,
            MaxFeedRateY = MaxFeedRateY,
            MaxFeedRateZ = MaxFeedRateZ,
            AccelerationX = AccelerationX,
            AccelerationY = AccelerationY,
            AccelerationZ = AccelerationZ,
            MaxTravelX = MaxTravelX,
            MaxTravelY = MaxTravelY,
            MaxTravelZ = MaxTravelZ,
            SoftLimitsEnabled = SoftLimitsEnabled,
            HardLimitsEnabled = HardLimitsEnabled,
            SpindleMinRpm = SpindleMinRpm,
            SpindleMaxRpm = SpindleMaxRpm,
            WarningTemperature = WarningTemperature,
            MaxTemperature = MaxTemperature
        };

    private string BuildStatusText()
    {
        if (HasUnsavedLocalChanges && HasPendingMachineChanges)
            return "Machine changes pending";

        if (HasUnsavedLocalChanges)
            return "Local changes pending";

        return "Saved locally";
    }

    private string BuildStatusDetail()
    {
        if (HasUnsavedLocalChanges && HasPendingMachineChanges)
            return "Machine-facing values were edited in the page. Save locally stores them on this computer, but controller sync is still separate.";

        if (HasUnsavedLocalChanges)
            return "Local preferences were edited and have not been saved on this computer yet.";

        if (HasPendingMachineChanges)
            return "The stored profile includes machine-facing changes that are not yet synced to the controller.";

        return "The page matches the last settings saved on this computer.";
    }

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
