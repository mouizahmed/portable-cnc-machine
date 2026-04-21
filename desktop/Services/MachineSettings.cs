namespace PortableCncApp.Services;

public sealed class MachineSettings
{
    public double StepsPerMmX { get; set; } = 800;
    public double StepsPerMmY { get; set; } = 800;
    public double StepsPerMmZ { get; set; } = 800;
    public double MaxFeedRateX { get; set; } = 5000;
    public double MaxFeedRateY { get; set; } = 5000;
    public double MaxFeedRateZ { get; set; } = 1000;
    public double AccelerationX { get; set; } = 200;
    public double AccelerationY { get; set; } = 200;
    public double AccelerationZ { get; set; } = 100;
    public double MaxTravelX { get; set; } = 300;
    public double MaxTravelY { get; set; } = 200;
    public double MaxTravelZ { get; set; } = 100;
    public bool SoftLimitsEnabled { get; set; } = true;
    public bool HardLimitsEnabled { get; set; } = true;
    public double SpindleMinRpm { get; set; } = 1000;
    public double SpindleMaxRpm { get; set; } = 24000;
    public double WarningTemperature { get; set; } = 40;
    public double MaxTemperature { get; set; } = 50;

    public static MachineSettings Default => new();

    public MachineSettings Clone()
        => new()
        {
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

    public void CopyFrom(MachineSettings source)
    {
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
        SpindleMinRpm = source.SpindleMinRpm;
        SpindleMaxRpm = source.SpindleMaxRpm;
        WarningTemperature = source.WarningTemperature;
        MaxTemperature = source.MaxTemperature;
    }

    public bool ValueEquals(MachineSettings other)
        => StepsPerMmX == other.StepsPerMmX
           && StepsPerMmY == other.StepsPerMmY
           && StepsPerMmZ == other.StepsPerMmZ
           && MaxFeedRateX == other.MaxFeedRateX
           && MaxFeedRateY == other.MaxFeedRateY
           && MaxFeedRateZ == other.MaxFeedRateZ
           && AccelerationX == other.AccelerationX
           && AccelerationY == other.AccelerationY
           && AccelerationZ == other.AccelerationZ
           && MaxTravelX == other.MaxTravelX
           && MaxTravelY == other.MaxTravelY
           && MaxTravelZ == other.MaxTravelZ
           && SoftLimitsEnabled == other.SoftLimitsEnabled
           && HardLimitsEnabled == other.HardLimitsEnabled
           && SpindleMinRpm == other.SpindleMinRpm
           && SpindleMaxRpm == other.SpindleMaxRpm
           && WarningTemperature == other.WarningTemperature
           && MaxTemperature == other.MaxTemperature;

    public static MachineSettings FromProtocol(PicoMachineSettings settings)
        => new()
        {
            StepsPerMmX = settings.StepsPerMmX,
            StepsPerMmY = settings.StepsPerMmY,
            StepsPerMmZ = settings.StepsPerMmZ,
            MaxFeedRateX = settings.MaxFeedRateX,
            MaxFeedRateY = settings.MaxFeedRateY,
            MaxFeedRateZ = settings.MaxFeedRateZ,
            AccelerationX = settings.AccelerationX,
            AccelerationY = settings.AccelerationY,
            AccelerationZ = settings.AccelerationZ,
            MaxTravelX = settings.MaxTravelX,
            MaxTravelY = settings.MaxTravelY,
            MaxTravelZ = settings.MaxTravelZ,
            SoftLimitsEnabled = settings.SoftLimitsEnabled,
            HardLimitsEnabled = settings.HardLimitsEnabled,
            SpindleMinRpm = settings.SpindleMinRpm,
            SpindleMaxRpm = settings.SpindleMaxRpm,
            WarningTemperature = settings.WarningTemperature,
            MaxTemperature = settings.MaxTemperature
        };

    public PicoMachineSettings ToProtocol()
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
}
