namespace PortableCncApp.Services;

public sealed class AppSettings
{
    // Connection
    public string? LastPort { get; set; }
    public bool AutoConnect { get; set; } = true;

    // Machine profile
    public double StepsPerMmX { get; set; } = 800;
    public double StepsPerMmY { get; set; } = 800;
    public double StepsPerMmZ { get; set; } = 800;

    // Motion
    public double MaxFeedRateX { get; set; } = 5000;
    public double MaxFeedRateY { get; set; } = 5000;
    public double MaxFeedRateZ { get; set; } = 1000;
    public double AccelerationX { get; set; } = 200;
    public double AccelerationY { get; set; } = 200;
    public double AccelerationZ { get; set; } = 100;

    // Limits and safety
    public double MaxTravelX { get; set; } = 300;
    public double MaxTravelY { get; set; } = 200;
    public double MaxTravelZ { get; set; } = 100;
    public bool SoftLimitsEnabled { get; set; } = true;
    public bool HardLimitsEnabled { get; set; } = true;
    public double MaxTemperature { get; set; } = 50;
    public double WarningTemperature { get; set; } = 40;

    // Spindle
    public double SpindleMaxRpm { get; set; } = 24000;
    public double SpindleMinRpm { get; set; } = 1000;

    // Local app preferences
    public string Units { get; set; } = "mm";
    public string ThemeMode { get; set; } = "system";

    // Machine-facing settings can be saved locally before controller writeback exists.
    public bool MachineSettingsSyncPending { get; set; }

    public AppSettings Clone()
        => new()
        {
            LastPort = LastPort,
            AutoConnect = AutoConnect,
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
            MaxTemperature = MaxTemperature,
            WarningTemperature = WarningTemperature,
            SpindleMaxRpm = SpindleMaxRpm,
            SpindleMinRpm = SpindleMinRpm,
            Units = Units,
            ThemeMode = ThemeMode,
            MachineSettingsSyncPending = MachineSettingsSyncPending
        };

    public void CopyFrom(AppSettings source)
    {
        LastPort = source.LastPort;
        AutoConnect = source.AutoConnect;
        StepsPerMmX = source.StepsPerMmX;
        StepsPerMmY = source.StepsPerMmY;
        StepsPerMmZ = source.StepsPerMmZ;
        MaxFeedRateX = source.MaxFeedRateX;
        MaxFeedRateY = source.MaxFeedRateY;
        MaxFeedRateZ = source.MaxFeedRateZ;
        AccelerationX = source.AccelerationX;
        AccelerationY = source.AccelerationY;
        AccelerationZ = source.AccelerationZ;
        MaxTravelX = source.MaxTravelX;
        MaxTravelY = source.MaxTravelY;
        MaxTravelZ = source.MaxTravelZ;
        SoftLimitsEnabled = source.SoftLimitsEnabled;
        HardLimitsEnabled = source.HardLimitsEnabled;
        MaxTemperature = source.MaxTemperature;
        WarningTemperature = source.WarningTemperature;
        SpindleMaxRpm = source.SpindleMaxRpm;
        SpindleMinRpm = source.SpindleMinRpm;
        Units = source.Units;
        ThemeMode = source.ThemeMode;
        MachineSettingsSyncPending = source.MachineSettingsSyncPending;
    }
}
