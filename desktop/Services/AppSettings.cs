namespace PortableCncApp.Services;

public sealed class AppSettings
{
    // ── Connection ───────────────────────────────────────────────────────────
    public string? LastPort       { get; set; }
    public bool    AutoConnect    { get; set; } = true;

    // ── Machine Parameters ───────────────────────────────────────────────────
    public double StepsPerMmX { get; set; } = 800;
    public double StepsPerMmY { get; set; } = 800;
    public double StepsPerMmZ { get; set; } = 800;

    // ── Speed & Acceleration ─────────────────────────────────────────────────
    public double MaxFeedRateX  { get; set; } = 5000;
    public double MaxFeedRateY  { get; set; } = 5000;
    public double MaxFeedRateZ  { get; set; } = 1000;
    public double AccelerationX { get; set; } = 200;
    public double AccelerationY { get; set; } = 200;
    public double AccelerationZ { get; set; } = 100;

    // ── Travel Limits ────────────────────────────────────────────────────────
    public double MaxTravelX        { get; set; } = 300;
    public double MaxTravelY        { get; set; } = 200;
    public double MaxTravelZ        { get; set; } = 100;
    public bool   SoftLimitsEnabled { get; set; } = true;
    public bool   HardLimitsEnabled { get; set; } = true;

    // ── Spindle ──────────────────────────────────────────────────────────────
    public double SpindleMaxRpm { get; set; } = 24000;
    public double SpindleMinRpm { get; set; } = 1000;

    // ── Safety Thresholds ────────────────────────────────────────────────────
    public double MaxTemperature     { get; set; } = 50;
    public double WarningTemperature { get; set; } = 40;

    // ── UI ───────────────────────────────────────────────────────────────────
    public string Units { get; set; } = "mm";
}
