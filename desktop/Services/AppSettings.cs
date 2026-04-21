namespace PortableCncApp.Services;

public sealed class AppSettings
{
    // Connection
    public string? LastPort { get; set; }
    public bool AutoConnect { get; set; } = true;

    // Local app preferences
    public string Units { get; set; } = "mm";
    public string ThemeMode { get; set; } = "system";

    public AppSettings Clone()
        => new()
        {
            LastPort = LastPort,
            AutoConnect = AutoConnect,
            Units = Units,
            ThemeMode = ThemeMode
        };

    public void CopyFrom(AppSettings source)
    {
        LastPort = source.LastPort;
        AutoConnect = source.AutoConnect;
        Units = source.Units;
        ThemeMode = source.ThemeMode;
    }
}
