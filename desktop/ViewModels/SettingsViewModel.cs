using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class SettingsViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // MACHINE PARAMETERS
    // ════════════════════════════════════════════════════════════════

    private double _stepsPerMmX = 800;
    public double StepsPerMmX { get => _stepsPerMmX; set => SetProperty(ref _stepsPerMmX, value); }

    private double _stepsPerMmY = 800;
    public double StepsPerMmY { get => _stepsPerMmY; set => SetProperty(ref _stepsPerMmY, value); }

    private double _stepsPerMmZ = 800;
    public double StepsPerMmZ { get => _stepsPerMmZ; set => SetProperty(ref _stepsPerMmZ, value); }

    // ════════════════════════════════════════════════════════════════
    // SPEED & ACCELERATION
    // ════════════════════════════════════════════════════════════════

    private double _maxFeedRateX = 5000;
    public double MaxFeedRateX { get => _maxFeedRateX; set => SetProperty(ref _maxFeedRateX, value); }

    private double _maxFeedRateY = 5000;
    public double MaxFeedRateY { get => _maxFeedRateY; set => SetProperty(ref _maxFeedRateY, value); }

    private double _maxFeedRateZ = 1000;
    public double MaxFeedRateZ { get => _maxFeedRateZ; set => SetProperty(ref _maxFeedRateZ, value); }

    private double _accelerationX = 200;
    public double AccelerationX { get => _accelerationX; set => SetProperty(ref _accelerationX, value); }

    private double _accelerationY = 200;
    public double AccelerationY { get => _accelerationY; set => SetProperty(ref _accelerationY, value); }

    private double _accelerationZ = 100;
    public double AccelerationZ { get => _accelerationZ; set => SetProperty(ref _accelerationZ, value); }

    // ════════════════════════════════════════════════════════════════
    // TRAVEL LIMITS
    // ════════════════════════════════════════════════════════════════

    private double _maxTravelX = 300;
    public double MaxTravelX { get => _maxTravelX; set => SetProperty(ref _maxTravelX, value); }

    private double _maxTravelY = 200;
    public double MaxTravelY { get => _maxTravelY; set => SetProperty(ref _maxTravelY, value); }

    private double _maxTravelZ = 100;
    public double MaxTravelZ { get => _maxTravelZ; set => SetProperty(ref _maxTravelZ, value); }

    private bool _softLimitsEnabled = true;
    public bool SoftLimitsEnabled { get => _softLimitsEnabled; set => SetProperty(ref _softLimitsEnabled, value); }

    private bool _hardLimitsEnabled = true;
    public bool HardLimitsEnabled { get => _hardLimitsEnabled; set => SetProperty(ref _hardLimitsEnabled, value); }

    // ════════════════════════════════════════════════════════════════
    // SPINDLE SETTINGS
    // ════════════════════════════════════════════════════════════════

    private double _spindleMaxRpm = 24000;
    public double SpindleMaxRpm { get => _spindleMaxRpm; set => SetProperty(ref _spindleMaxRpm, value); }

    private double _spindleMinRpm = 1000;
    public double SpindleMinRpm { get => _spindleMinRpm; set => SetProperty(ref _spindleMinRpm, value); }

    // ════════════════════════════════════════════════════════════════
    // SAFETY THRESHOLDS
    // ════════════════════════════════════════════════════════════════

    private double _maxTemperature = 50;
    public double MaxTemperature { get => _maxTemperature; set => SetProperty(ref _maxTemperature, value); }

    private double _warningTemperature = 40;
    public double WarningTemperature { get => _warningTemperature; set => SetProperty(ref _warningTemperature, value); }

    // ════════════════════════════════════════════════════════════════
    // UI SETTINGS
    // ════════════════════════════════════════════════════════════════

    private bool _darkMode = true;
    public bool DarkMode { get => _darkMode; set => SetProperty(ref _darkMode, value); }

    private string _units = "mm";
    public string Units { get => _units; set => SetProperty(ref _units, value); }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand SaveSettingsCommand { get; }
    public ICommand LoadDefaultsCommand { get; }
    public ICommand ApplyToMachineCommand { get; }
    public ICommand ImportSettingsCommand { get; }
    public ICommand ExportSettingsCommand { get; }

    public SettingsViewModel()
    {
        SaveSettingsCommand = new RelayCommand(SaveSettings);
        LoadDefaultsCommand = new RelayCommand(LoadDefaults);
        ApplyToMachineCommand = new RelayCommand(ApplyToMachine);
        ImportSettingsCommand = new RelayCommand(ImportSettings);
        ExportSettingsCommand = new RelayCommand(ExportSettings);
    }

    public void ApplyFrom(PortableCncApp.Services.AppSettings s)
    {
        StepsPerMmX = s.StepsPerMmX;
        StepsPerMmY = s.StepsPerMmY;
        StepsPerMmZ = s.StepsPerMmZ;
        MaxFeedRateX = s.MaxFeedRateX;
        MaxFeedRateY = s.MaxFeedRateY;
        MaxFeedRateZ = s.MaxFeedRateZ;
        AccelerationX = s.AccelerationX;
        AccelerationY = s.AccelerationY;
        AccelerationZ = s.AccelerationZ;
        MaxTravelX = s.MaxTravelX;
        MaxTravelY = s.MaxTravelY;
        MaxTravelZ = s.MaxTravelZ;
        SoftLimitsEnabled = s.SoftLimitsEnabled;
        HardLimitsEnabled = s.HardLimitsEnabled;
        SpindleMaxRpm = s.SpindleMaxRpm;
        SpindleMinRpm = s.SpindleMinRpm;
        MaxTemperature = s.MaxTemperature;
        WarningTemperature = s.WarningTemperature;
        Units = s.Units;
    }

    private void SaveTo(PortableCncApp.Services.AppSettings s)
    {
        s.StepsPerMmX = StepsPerMmX;
        s.StepsPerMmY = StepsPerMmY;
        s.StepsPerMmZ = StepsPerMmZ;
        s.MaxFeedRateX = MaxFeedRateX;
        s.MaxFeedRateY = MaxFeedRateY;
        s.MaxFeedRateZ = MaxFeedRateZ;
        s.AccelerationX = AccelerationX;
        s.AccelerationY = AccelerationY;
        s.AccelerationZ = AccelerationZ;
        s.MaxTravelX = MaxTravelX;
        s.MaxTravelY = MaxTravelY;
        s.MaxTravelZ = MaxTravelZ;
        s.SoftLimitsEnabled = SoftLimitsEnabled;
        s.HardLimitsEnabled = HardLimitsEnabled;
        s.SpindleMaxRpm = SpindleMaxRpm;
        s.SpindleMinRpm = SpindleMinRpm;
        s.MaxTemperature = MaxTemperature;
        s.WarningTemperature = WarningTemperature;
        s.Units = Units;
    }

    private void SaveSettings()
    {
        if (MainVm == null) return;
        SaveTo(MainVm.Settings.Current);
        MainVm.Settings.Save();
        MainVm.StatusMessage = "Settings saved";
    }

    private void LoadDefaults()
    {
        StepsPerMmX = StepsPerMmY = StepsPerMmZ = 800;
        MaxFeedRateX = MaxFeedRateY = 5000;
        MaxFeedRateZ = 1000;
        AccelerationX = AccelerationY = 200;
        AccelerationZ = 100;
        MaxTravelX = 300;
        MaxTravelY = 200;
        MaxTravelZ = 100;
        SpindleMaxRpm = 24000;
        SpindleMinRpm = 1000;
        MaxTemperature = 50;
        WarningTemperature = 40;
        
        MainVm!.StatusMessage = "Default settings loaded";
    }

    private void ApplyToMachine()
    {
        // TODO: Send $$ commands to GRBL
        MainVm!.StatusMessage = "Settings applied to machine";
    }

    private void ImportSettings()
    {
        // TODO: Import from file
    }

    private void ExportSettings()
    {
        // TODO: Export to file
    }
}
