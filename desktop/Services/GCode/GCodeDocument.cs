using System;
using System.Collections.Immutable;

namespace PortableCncApp.Services.GCode;

public enum MotionType
{
    None,
    Rapid,
    Linear,
    ArcClockwise,
    ArcCounterClockwise
}

public enum GCodePlane
{
    XY,
    XZ,
    YZ
}

public enum GCodeUnits
{
    Millimeters,
    Inches
}

public readonly record struct Point3D(double X, double Y, double Z);

public sealed record ToolpathSegment(
    Point3D Start,
    Point3D End,
    MotionType Motion,
    int SourceLine)
{
    public bool IsRapid => Motion == MotionType.Rapid;
    public bool IsArc => Motion is MotionType.ArcClockwise or MotionType.ArcCounterClockwise;
    public bool IsPlungeOrRetract =>
        Math.Abs(Start.X - End.X) < 0.0001 &&
        Math.Abs(Start.Y - End.Y) < 0.0001 &&
        Math.Abs(Start.Z - End.Z) >= 0.0001;
    public double AverageZ => (Start.Z + End.Z) * 0.5;
}

public sealed record GCodeParseWarning(int LineNumber, string Message);

public sealed class GCodeDocument
{
    public GCodeDocument(
        string sourcePath,
        ImmutableArray<ToolpathSegment> segments,
        ImmutableArray<GCodeParseWarning> warnings,
        int totalLines,
        double minX,
        double maxX,
        double minY,
        double maxY,
        double minZ,
        double maxZ)
    {
        SourcePath = sourcePath;
        Segments = segments;
        Warnings = warnings;
        TotalLines = totalLines;
        MinX = minX;
        MaxX = maxX;
        MinY = minY;
        MaxY = maxY;
        MinZ = minZ;
        MaxZ = maxZ;
    }

    public string SourcePath { get; }
    public ImmutableArray<ToolpathSegment> Segments { get; }
    public ImmutableArray<GCodeParseWarning> Warnings { get; }
    public int TotalLines { get; }

    public double MinX { get; }
    public double MaxX { get; }
    public double MinY { get; }
    public double MaxY { get; }
    public double MinZ { get; }
    public double MaxZ { get; }

    public bool HasGeometry => Segments.Length > 0;
    public int WarningCount => Warnings.Length;
    public double WidthMm => HasGeometry ? MaxX - MinX : 0;
    public double HeightMm => HasGeometry ? MaxY - MinY : 0;
    public double DepthMm => HasGeometry ? MaxZ - MinZ : 0;
    public string DisplayUnitsLabel => "mm";
}
