using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace PortableCncApp.Services.GCode;

public static class GCodeParser
{
    private const double InchToMillimeter = 25.4;
    private const double PositionEpsilon = 0.0001;
    private const double ArcPointStepDegrees = 5.0;

    public static async Task<GCodeDocument> ParseFileAsync(string filePath, CancellationToken cancellationToken)
    {
        var lines = await File.ReadAllLinesAsync(filePath, cancellationToken);
        return await Task.Run(() => ParseLines(lines, filePath, cancellationToken), cancellationToken);
    }

    private static GCodeDocument ParseLines(
        IReadOnlyList<string> lines,
        string filePath,
        CancellationToken cancellationToken)
    {
        var segments = new List<ToolpathSegment>();
        var warnings = new List<GCodeParseWarning>();
        var modal = new ModalState();

        bool hasBounds = false;
        double minX = 0;
        double maxX = 0;
        double minY = 0;
        double maxY = 0;
        double minZ = 0;
        double maxZ = 0;

        for (int index = 0; index < lines.Count; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            int sourceLineNumber = index + 1;
            var stripped = StripComments(lines[index]);
            if (string.IsNullOrWhiteSpace(stripped))
            {
                continue;
            }

            var words = ParseWords(stripped, sourceLineNumber, warnings);
            if (words.Count == 0)
            {
                continue;
            }

            ProcessLine(
                words,
                sourceLineNumber,
                ref modal,
                segments,
                warnings,
                ref hasBounds,
                ref minX,
                ref maxX,
                ref minY,
                ref maxY,
                ref minZ,
                ref maxZ);
        }

        return new GCodeDocument(
            filePath,
            segments.ToImmutableArray(),
            warnings.ToImmutableArray(),
            lines.Count,
            minX,
            maxX,
            minY,
            maxY,
            minZ,
            maxZ);
    }

    private static void ProcessLine(
        IReadOnlyList<GCodeWord> words,
        int lineNumber,
        ref ModalState modal,
        List<ToolpathSegment> segments,
        List<GCodeParseWarning> warnings,
        ref bool hasBounds,
        ref double minX,
        ref double maxX,
        ref double minY,
        ref double maxY,
        ref double minZ,
        ref double maxZ)
    {
        MotionType lineMotion = modal.Motion;
        bool motionChanged = false;

        double? x = null;
        double? y = null;
        double? z = null;
        double? i = null;
        double? j = null;
        double? k = null;
        double? r = null;

        foreach (var word in words)
        {
            switch (word.Letter)
            {
                case 'G':
                    ApplyGWord(word.Value, lineNumber, ref modal, ref lineMotion, ref motionChanged, warnings);
                    break;
                case 'M':
                    ApplyMWord(word.Value, ref modal);
                    break;
                case 'X':
                    x = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'Y':
                    y = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'Z':
                    z = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'I':
                    i = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'J':
                    j = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'K':
                    k = ConvertToMillimeters(word.Value, modal.Units);
                    break;
                case 'R':
                    r = ConvertToMillimeters(word.Value, modal.Units);
                    break;
            }
        }

        if (motionChanged)
        {
            modal.Motion = lineMotion;
        }

        bool hasTargetCoordinates = x.HasValue || y.HasValue || z.HasValue;
        if (!hasTargetCoordinates)
        {
            return;
        }

        if (modal.Motion == MotionType.None)
        {
            warnings.Add(new GCodeParseWarning(lineNumber, "Motion coordinates were ignored because no modal motion is active."));
            return;
        }

        var target = ResolveTarget(modal.Position, modal.IsAbsolute, x, y, z);
        if (PositionsMatch(modal.Position, target))
        {
            return;
        }

        switch (modal.Motion)
        {
            case MotionType.Rapid:
            case MotionType.Linear:
                AddSegment(modal.Position, target, modal.Motion, lineNumber, segments, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
                break;
            case MotionType.ArcClockwise:
            case MotionType.ArcCounterClockwise:
                if (!TryAddArcSegments(modal.Position, target, modal.Plane, modal.Motion, lineNumber, i, j, k, r, segments, warnings, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ))
                {
                    AddSegment(modal.Position, target, MotionType.Linear, lineNumber, segments, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
                }
                break;
        }

        modal.Position = target;
    }

    private static void ApplyGWord(
        double value,
        int lineNumber,
        ref ModalState modal,
        ref MotionType lineMotion,
        ref bool motionChanged,
        List<GCodeParseWarning> warnings)
    {
        int code = (int)Math.Round(value);
        switch (code)
        {
            case 0:
                lineMotion = MotionType.Rapid;
                motionChanged = true;
                break;
            case 1:
                lineMotion = MotionType.Linear;
                motionChanged = true;
                break;
            case 2:
                lineMotion = MotionType.ArcClockwise;
                motionChanged = true;
                break;
            case 3:
                lineMotion = MotionType.ArcCounterClockwise;
                motionChanged = true;
                break;
            case 17:
                modal.Plane = GCodePlane.XY;
                break;
            case 18:
                modal.Plane = GCodePlane.XZ;
                break;
            case 19:
                modal.Plane = GCodePlane.YZ;
                break;
            case 20:
                modal.Units = GCodeUnits.Inches;
                break;
            case 21:
                modal.Units = GCodeUnits.Millimeters;
                break;
            case 54:
            case 55:
            case 56:
            case 57:
            case 58:
            case 59:
            case 93:
            case 94:
            case 95:
                break;
            case 90:
                modal.IsAbsolute = true;
                break;
            case 91:
                modal.IsAbsolute = false;
                break;
            default:
                if (code is >= 4 and <= 99)
                {
                    warnings.Add(new GCodeParseWarning(lineNumber, $"Unsupported G-code G{code} was ignored by the preview parser."));
                }
                break;
        }
    }

    private static void ApplyMWord(double value, ref ModalState modal)
    {
        int code = (int)Math.Round(value);
        if (code is 3 or 4)
        {
            modal.SpindleOn = true;
        }
        else if (code == 5)
        {
            modal.SpindleOn = false;
        }
    }

    private static Point3D ResolveTarget(Point3D current, bool isAbsolute, double? x, double? y, double? z)
    {
        if (isAbsolute)
        {
            return new Point3D(
                x ?? current.X,
                y ?? current.Y,
                z ?? current.Z);
        }

        return new Point3D(
            current.X + (x ?? 0),
            current.Y + (y ?? 0),
            current.Z + (z ?? 0));
    }

    private static bool TryAddArcSegments(
        Point3D start,
        Point3D end,
        GCodePlane plane,
        MotionType motion,
        int lineNumber,
        double? i,
        double? j,
        double? k,
        double? r,
        List<ToolpathSegment> segments,
        List<GCodeParseWarning> warnings,
        ref bool hasBounds,
        ref double minX,
        ref double maxX,
        ref double minY,
        ref double maxY,
        ref double minZ,
        ref double maxZ)
    {
        if (r.HasValue)
        {
            warnings.Add(new GCodeParseWarning(lineNumber, "Arc radius syntax (R) is not yet supported. The move was rendered as a straight line."));
            return false;
        }

        var axes = GetPlaneAxes(plane);
        double startU = GetAxis(start, axes.UAxis);
        double startV = GetAxis(start, axes.VAxis);
        double endU = GetAxis(end, axes.UAxis);
        double endV = GetAxis(end, axes.VAxis);

        double? offsetU = axes.UOffset switch
        {
            'I' => i,
            'J' => j,
            'K' => k,
            _ => null
        };

        double? offsetV = axes.VOffset switch
        {
            'I' => i,
            'J' => j,
            'K' => k,
            _ => null
        };

        if (!offsetU.HasValue || !offsetV.HasValue)
        {
            warnings.Add(new GCodeParseWarning(lineNumber, "Arc center offsets are incomplete. The move was rendered as a straight line."));
            return false;
        }

        double centerU = startU + offsetU.Value;
        double centerV = startV + offsetV.Value;
        double radius = Math.Sqrt(Math.Pow(startU - centerU, 2) + Math.Pow(startV - centerV, 2));
        if (radius < PositionEpsilon)
        {
            warnings.Add(new GCodeParseWarning(lineNumber, "Arc radius was too small to render. The move was rendered as a straight line."));
            return false;
        }

        double startAngle = Math.Atan2(startV - centerV, startU - centerU);
        double endAngle = Math.Atan2(endV - centerV, endU - centerU);
        double sweep = ComputeSweep(startAngle, endAngle, motion == MotionType.ArcClockwise);
        int steps = Math.Max(12, (int)Math.Ceiling(Math.Abs(sweep) / DegreesToRadians(ArcPointStepDegrees)));

        Point3D previous = start;
        for (int step = 1; step <= steps; step++)
        {
            double t = step / (double)steps;
            double angle = startAngle + (sweep * t);
            double w = Lerp(GetAxis(start, axes.WAxis), GetAxis(end, axes.WAxis), t);

            var next = ComposePoint(
                axes,
                centerU + (Math.Cos(angle) * radius),
                centerV + (Math.Sin(angle) * radius),
                w);

            AddSegment(previous, next, motion, lineNumber, segments, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
            previous = next;
        }

        return true;
    }

    private static void AddSegment(
        Point3D start,
        Point3D end,
        MotionType motion,
        int lineNumber,
        List<ToolpathSegment> segments,
        ref bool hasBounds,
        ref double minX,
        ref double maxX,
        ref double minY,
        ref double maxY,
        ref double minZ,
        ref double maxZ)
    {
        segments.Add(new ToolpathSegment(start, end, motion, lineNumber));
        IncludePoint(start, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
        IncludePoint(end, ref hasBounds, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
    }

    private static void IncludePoint(
        Point3D point,
        ref bool hasBounds,
        ref double minX,
        ref double maxX,
        ref double minY,
        ref double maxY,
        ref double minZ,
        ref double maxZ)
    {
        if (!hasBounds)
        {
            minX = maxX = point.X;
            minY = maxY = point.Y;
            minZ = maxZ = point.Z;
            hasBounds = true;
            return;
        }

        minX = Math.Min(minX, point.X);
        maxX = Math.Max(maxX, point.X);
        minY = Math.Min(minY, point.Y);
        maxY = Math.Max(maxY, point.Y);
        minZ = Math.Min(minZ, point.Z);
        maxZ = Math.Max(maxZ, point.Z);
    }

    private static double ConvertToMillimeters(double value, GCodeUnits units)
        => units == GCodeUnits.Inches ? value * InchToMillimeter : value;

    private static bool PositionsMatch(Point3D left, Point3D right)
        => Math.Abs(left.X - right.X) < PositionEpsilon &&
           Math.Abs(left.Y - right.Y) < PositionEpsilon &&
           Math.Abs(left.Z - right.Z) < PositionEpsilon;

    private static string StripComments(string line)
    {
        var builder = new StringBuilder(line.Length);
        bool inParenComment = false;

        foreach (char ch in line)
        {
            if (inParenComment)
            {
                if (ch == ')')
                {
                    inParenComment = false;
                }

                continue;
            }

            if (ch == ';')
            {
                break;
            }

            if (ch == '(')
            {
                inParenComment = true;
                continue;
            }

            builder.Append(ch);
        }

        var stripped = builder.ToString().Trim();
        return stripped == "%" ? string.Empty : stripped;
    }

    private static List<GCodeWord> ParseWords(string line, int lineNumber, List<GCodeParseWarning> warnings)
    {
        var words = new List<GCodeWord>();
        int index = 0;

        while (index < line.Length)
        {
            while (index < line.Length && (char.IsWhiteSpace(line[index]) || line[index] == ','))
            {
                index++;
            }

            if (index >= line.Length)
            {
                break;
            }

            char letter = char.ToUpperInvariant(line[index]);
            if (letter is < 'A' or > 'Z')
            {
                warnings.Add(new GCodeParseWarning(lineNumber, $"Unexpected token '{line[index]}' was ignored."));
                index++;
                continue;
            }

            index++;
            int valueStart = index;
            while (index < line.Length)
            {
                char current = line[index];
                if (char.IsWhiteSpace(current) || current == ',')
                {
                    break;
                }

                if ((current is >= 'A' and <= 'Z') || (current is >= 'a' and <= 'z'))
                {
                    break;
                }

                index++;
            }

            string rawValue = line[valueStart..index];
            if (rawValue.Length == 0)
            {
                warnings.Add(new GCodeParseWarning(lineNumber, $"Word '{letter}' is missing a numeric value."));
                continue;
            }

            if (!double.TryParse(rawValue, NumberStyles.Float, CultureInfo.InvariantCulture, out double parsedValue))
            {
                warnings.Add(new GCodeParseWarning(lineNumber, $"Value '{rawValue}' for word '{letter}' could not be parsed."));
                continue;
            }

            words.Add(new GCodeWord(letter, parsedValue));
        }

        return words;
    }

    private static (int UAxis, int VAxis, int WAxis, char UOffset, char VOffset) GetPlaneAxes(GCodePlane plane)
        => plane switch
        {
            GCodePlane.XY => (0, 1, 2, 'I', 'J'),
            GCodePlane.XZ => (0, 2, 1, 'I', 'K'),
            GCodePlane.YZ => (1, 2, 0, 'J', 'K'),
            _ => (0, 1, 2, 'I', 'J')
        };

    private static double GetAxis(Point3D point, int axisIndex)
        => axisIndex switch
        {
            0 => point.X,
            1 => point.Y,
            2 => point.Z,
            _ => 0
        };

    private static Point3D ComposePoint((int UAxis, int VAxis, int WAxis, char UOffset, char VOffset) axes, double u, double v, double w)
    {
        double x = 0;
        double y = 0;
        double z = 0;

        SetAxis(ref x, ref y, ref z, axes.UAxis, u);
        SetAxis(ref x, ref y, ref z, axes.VAxis, v);
        SetAxis(ref x, ref y, ref z, axes.WAxis, w);

        return new Point3D(x, y, z);
    }

    private static void SetAxis(ref double x, ref double y, ref double z, int axisIndex, double value)
    {
        switch (axisIndex)
        {
            case 0:
                x = value;
                break;
            case 1:
                y = value;
                break;
            case 2:
                z = value;
                break;
        }
    }

    private static double ComputeSweep(double startAngle, double endAngle, bool clockwise)
    {
        double sweep = endAngle - startAngle;
        if (clockwise)
        {
            if (sweep >= 0)
            {
                sweep -= Math.PI * 2;
            }
        }
        else if (sweep <= 0)
        {
            sweep += Math.PI * 2;
        }

        return sweep;
    }

    private static double DegreesToRadians(double degrees)
        => degrees * Math.PI / 180.0;

    private static double Lerp(double start, double end, double t)
        => start + ((end - start) * t);

    private sealed class ModalState
    {
        public Point3D Position { get; set; } = new(0, 0, 0);
        public MotionType Motion { get; set; } = MotionType.None;
        public GCodePlane Plane { get; set; } = GCodePlane.XY;
        public GCodeUnits Units { get; set; } = GCodeUnits.Millimeters;
        public bool IsAbsolute { get; set; } = true;
        public bool SpindleOn { get; set; }
    }

    private readonly record struct GCodeWord(char Letter, double Value);
}
