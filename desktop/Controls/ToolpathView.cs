using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.Controls;

public sealed class ToolpathView : Control
{
    public static readonly StyledProperty<GCodeDocument?> DocumentProperty =
        AvaloniaProperty.Register<ToolpathView, GCodeDocument?>(nameof(Document));

    public static readonly StyledProperty<int> CurrentLineProperty =
        AvaloniaProperty.Register<ToolpathView, int>(nameof(CurrentLine));

    public static readonly StyledProperty<double> PositionXProperty =
        AvaloniaProperty.Register<ToolpathView, double>(nameof(PositionX));

    public static readonly StyledProperty<double> PositionYProperty =
        AvaloniaProperty.Register<ToolpathView, double>(nameof(PositionY));

    public static readonly StyledProperty<double> PositionZProperty =
        AvaloniaProperty.Register<ToolpathView, double>(nameof(PositionZ));

    public static readonly StyledProperty<bool> Is3DViewProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(Is3DView), true);

    public static readonly StyledProperty<string> CameraPresetProperty =
        AvaloniaProperty.Register<ToolpathView, string>(nameof(CameraPreset), "Iso");

    public static readonly StyledProperty<int> ResetViewTokenProperty =
        AvaloniaProperty.Register<ToolpathView, int>(nameof(ResetViewToken));

    public static readonly StyledProperty<bool> ShowRapidsProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowRapids), true);

    public static readonly StyledProperty<bool> ShowCutsProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowCuts), true);

    public static readonly StyledProperty<bool> ShowArcsProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowArcs), true);

    public static readonly StyledProperty<bool> ShowPlungesProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowPlunges), true);

    public static readonly StyledProperty<bool> ShowCompletedPathProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowCompletedPath), true);

    public static readonly StyledProperty<bool> ShowRemainingPathProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowRemainingPath), true);

    public static readonly StyledProperty<bool> ShowStockBoxProperty =
        AvaloniaProperty.Register<ToolpathView, bool>(nameof(ShowStockBox), true);

    private const double ViewPadding = 24;
    private const double ZoomStep = 1.15;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 25;
    private const double OrbitSensitivity = 0.45;
    private const double PanSensitivity = 1.0;
    private const double PitchClamp = 89.0;

    private InteractionMode _interactionMode;
    private Point _lastPointerPoint;
    private Vector _panOffset;
    private double _zoom = 1.0;
    private double _yawDegrees = -35.0;
    private double _pitchDegrees = 60.0;

    static ToolpathView()
    {
        AffectsRender<ToolpathView>(
            DocumentProperty,
            CurrentLineProperty,
            PositionXProperty,
            PositionYProperty,
            PositionZProperty,
            Is3DViewProperty,
            CameraPresetProperty,
            ShowRapidsProperty,
            ShowCutsProperty,
            ShowArcsProperty,
            ShowPlungesProperty,
            ShowCompletedPathProperty,
            ShowRemainingPathProperty,
            ShowStockBoxProperty);
    }

    public ToolpathView()
    {
        ClipToBounds = true;
        PointerPressed += OnPointerPressed;
        PointerMoved += OnPointerMoved;
        PointerReleased += OnPointerReleased;
        PointerWheelChanged += OnPointerWheelChanged;
        ResetView();
    }

    public GCodeDocument? Document
    {
        get => GetValue(DocumentProperty);
        set => SetValue(DocumentProperty, value);
    }

    public int CurrentLine
    {
        get => GetValue(CurrentLineProperty);
        set => SetValue(CurrentLineProperty, value);
    }

    public double PositionX
    {
        get => GetValue(PositionXProperty);
        set => SetValue(PositionXProperty, value);
    }

    public double PositionY
    {
        get => GetValue(PositionYProperty);
        set => SetValue(PositionYProperty, value);
    }

    public double PositionZ
    {
        get => GetValue(PositionZProperty);
        set => SetValue(PositionZProperty, value);
    }

    public bool Is3DView
    {
        get => GetValue(Is3DViewProperty);
        set => SetValue(Is3DViewProperty, value);
    }

    public string CameraPreset
    {
        get => GetValue(CameraPresetProperty);
        set => SetValue(CameraPresetProperty, value);
    }

    public int ResetViewToken
    {
        get => GetValue(ResetViewTokenProperty);
        set => SetValue(ResetViewTokenProperty, value);
    }

    public bool ShowRapids
    {
        get => GetValue(ShowRapidsProperty);
        set => SetValue(ShowRapidsProperty, value);
    }

    public bool ShowCuts
    {
        get => GetValue(ShowCutsProperty);
        set => SetValue(ShowCutsProperty, value);
    }

    public bool ShowArcs
    {
        get => GetValue(ShowArcsProperty);
        set => SetValue(ShowArcsProperty, value);
    }

    public bool ShowPlunges
    {
        get => GetValue(ShowPlungesProperty);
        set => SetValue(ShowPlungesProperty, value);
    }

    public bool ShowCompletedPath
    {
        get => GetValue(ShowCompletedPathProperty);
        set => SetValue(ShowCompletedPathProperty, value);
    }

    public bool ShowRemainingPath
    {
        get => GetValue(ShowRemainingPathProperty);
        set => SetValue(ShowRemainingPathProperty, value);
    }

    public bool ShowStockBox
    {
        get => GetValue(ShowStockBoxProperty);
        set => SetValue(ShowStockBoxProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == DocumentProperty ||
            change.Property == Is3DViewProperty ||
            change.Property == ResetViewTokenProperty)
        {
            ResetView();
            return;
        }

        if (change.Property == CameraPresetProperty)
        {
            ApplyPreset(CameraPreset);
            InvalidateVisual();
        }
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        var viewport = Bounds;
        if (viewport.Width <= 0 || viewport.Height <= 0)
        {
            return;
        }

        context.FillRectangle(new SolidColorBrush(Color.Parse("#161616")), viewport);
        if (!Is3DView)
        {
            DrawScreenGrid(context, viewport);
        }

        DrawBoundsFrame(context, viewport);

        if (Document is not { HasGeometry: true } document)
        {
            DrawCenteredText(context, viewport, "Select a G-code file to build the toolpath preview.");
            return;
        }

        var view = CreateViewTransform(document, viewport);
        if (Is3DView)
        {
            DrawWorldGrid(context, document, view);
        }

        if (ShowStockBox)
        {
            DrawStockBox(context, document, view);
        }

        DrawAxes(context, document, view);
        DrawSegments(context, document, view);
        DrawMachinePosition(context, view);
    }

    private void DrawSegments(DrawingContext context, GCodeDocument document, ViewTransform view)
    {
        var renderSegments = new List<RenderedSegment>(document.Segments.Length);

        foreach (var segment in document.Segments)
        {
            bool isCompleted = CurrentLine > 0 && segment.SourceLine <= CurrentLine;
            if (!ShouldRenderSegment(segment, isCompleted))
            {
                continue;
            }

            var start = view.Project(segment.Start);
            var end = view.Project(segment.End);
            double depth = (start.Depth + end.Depth) * 0.5;
            Color color = ApplyDepthFade(
                GetSegmentColor(segment, isCompleted, document),
                view.NormalizeDepth(depth),
                minimumAlpha: segment.IsRapid ? (byte)68 : (byte)88);
            var pen = new Pen(new SolidColorBrush(color), GetSegmentThickness(segment, isCompleted));
            renderSegments.Add(new RenderedSegment(start, end, pen, depth));
        }

        if (Is3DView)
        {
            renderSegments.Sort((left, right) => right.Depth.CompareTo(left.Depth));
        }

        foreach (var segment in renderSegments)
        {
            context.DrawLine(segment.Pen, segment.Start.Screen, segment.End.Screen);
        }
    }

    private bool ShouldRenderSegment(ToolpathSegment segment, bool isCompleted)
    {
        if (isCompleted && !ShowCompletedPath)
        {
            return false;
        }

        if (!isCompleted && !ShowRemainingPath)
        {
            return false;
        }

        if (segment.IsRapid)
        {
            return ShowRapids;
        }

        if (segment.IsPlungeOrRetract)
        {
            return ShowPlunges;
        }

        if (segment.IsArc)
        {
            return ShowArcs;
        }

        return ShowCuts;
    }

    private Color GetSegmentColor(ToolpathSegment segment, bool isCompleted, GCodeDocument document)
    {
        if (segment.IsRapid)
        {
            return isCompleted ? Color.Parse("#E0A100") : Color.Parse("#7A5A25");
        }

        if (segment.IsPlungeOrRetract)
        {
            return isCompleted ? Color.Parse("#D86CFF") : Color.Parse("#6A3D7A");
        }

        double depthFactor = document.DepthMm > 0.0001
            ? (segment.AverageZ - document.MinZ) / document.DepthMm
            : 0.5;
        depthFactor = Math.Clamp(depthFactor, 0, 1);

        if (segment.IsArc)
        {
            return InterpolateColor(
                isCompleted ? Color.Parse("#4EE3D2") : Color.Parse("#1F6B63"),
                isCompleted ? Color.Parse("#8CD8FF") : Color.Parse("#2D5065"),
                depthFactor);
        }

        return InterpolateColor(
            isCompleted ? Color.Parse("#4C8DFF") : Color.Parse("#274B79"),
            isCompleted ? Color.Parse("#9BE7FF") : Color.Parse("#34536F"),
            depthFactor);
    }

    private static double GetSegmentThickness(ToolpathSegment segment, bool isCompleted)
    {
        double thickness = isCompleted ? 1.55 : 1.15;
        if (segment.IsPlungeOrRetract)
        {
            return thickness + 0.35;
        }

        if (segment.IsRapid)
        {
            return thickness - 0.1;
        }

        return thickness;
    }

    private void DrawMachinePosition(DrawingContext context, ViewTransform view)
    {
        var machine = view.Project(new Point3D(PositionX, PositionY, PositionZ));
        var markerBrush = new SolidColorBrush(Color.Parse("#FFFFFF"));
        var markerPen = new Pen(markerBrush, 1);

        context.DrawEllipse(markerBrush, null, machine.Screen, 3.5, 3.5);
        context.DrawLine(markerPen, machine.Screen + new Point(-8, 0), machine.Screen + new Point(8, 0));
        context.DrawLine(markerPen, machine.Screen + new Point(0, -8), machine.Screen + new Point(0, 8));
    }

    private void DrawAxes(DrawingContext context, GCodeDocument document, ViewTransform view)
    {
        double axisLength = Math.Max(12, Math.Max(document.WidthMm, Math.Max(document.HeightMm, Math.Max(document.DepthMm, 12))) * 0.18);

        var origin = view.Project(new Point3D(0, 0, 0)).Screen;
        var xAxis = view.Project(new Point3D(axisLength, 0, 0)).Screen;
        var yAxis = view.Project(new Point3D(0, axisLength, 0)).Screen;

        context.DrawLine(new Pen(new SolidColorBrush(Color.Parse("#D85C5C")), 1.25), origin, xAxis);
        context.DrawLine(new Pen(new SolidColorBrush(Color.Parse("#64B56A")), 1.25), origin, yAxis);

        if (Is3DView)
        {
            var zAxis = view.Project(new Point3D(0, 0, axisLength)).Screen;
            context.DrawLine(new Pen(new SolidColorBrush(Color.Parse("#6FA8DC")), 1.25), origin, zAxis);
        }
    }

    private void DrawWorldGrid(DrawingContext context, GCodeDocument document, ViewTransform view)
    {
        double span = Math.Max(document.WidthMm, document.HeightMm);
        double margin = Math.Max(10, span * 0.2);
        double minX = document.MinX - margin;
        double maxX = document.MaxX + margin;
        double minY = document.MinY - margin;
        double maxY = document.MaxY + margin;
        double floorZ = document.MinZ;
        double step = ChooseGridStep(span + (margin * 2));

        for (double x = RoundDown(minX, step); x <= maxX; x += step)
        {
            var start = view.Project(new Point3D(x, minY, floorZ));
            var end = view.Project(new Point3D(x, maxY, floorZ));
            double depth = (start.Depth + end.Depth) * 0.5;
            var color = ApplyDepthFade(Color.Parse("#2A3640"), view.NormalizeDepth(depth), 18);
            context.DrawLine(new Pen(new SolidColorBrush(color), 1), start.Screen, end.Screen);
        }

        for (double y = RoundDown(minY, step); y <= maxY; y += step)
        {
            var start = view.Project(new Point3D(minX, y, floorZ));
            var end = view.Project(new Point3D(maxX, y, floorZ));
            double depth = (start.Depth + end.Depth) * 0.5;
            var color = ApplyDepthFade(Color.Parse("#2A3640"), view.NormalizeDepth(depth), 18);
            context.DrawLine(new Pen(new SolidColorBrush(color), 1), start.Screen, end.Screen);
        }
    }

    private void DrawStockBox(DrawingContext context, GCodeDocument document, ViewTransform view)
    {
        double stockTop = Math.Max(document.MaxZ, 0);
        var corners = new[]
        {
            new Point3D(document.MinX, document.MinY, document.MinZ),
            new Point3D(document.MaxX, document.MinY, document.MinZ),
            new Point3D(document.MaxX, document.MaxY, document.MinZ),
            new Point3D(document.MinX, document.MaxY, document.MinZ),
            new Point3D(document.MinX, document.MinY, stockTop),
            new Point3D(document.MaxX, document.MinY, stockTop),
            new Point3D(document.MaxX, document.MaxY, stockTop),
            new Point3D(document.MinX, document.MaxY, stockTop)
        };

        int[][] edges =
        {
            new[] { 0, 1 }, new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 },
            new[] { 4, 5 }, new[] { 5, 6 }, new[] { 6, 7 }, new[] { 7, 4 },
            new[] { 0, 4 }, new[] { 1, 5 }, new[] { 2, 6 }, new[] { 3, 7 }
        };

        foreach (var edge in edges)
        {
            var start = view.Project(corners[edge[0]]);
            var end = view.Project(corners[edge[1]]);
            double depth = (start.Depth + end.Depth) * 0.5;
            bool isTopFace = edge[0] >= 4 && edge[1] >= 4;
            bool isBottomFace = edge[0] < 4 && edge[1] < 4;
            var baseColor = Color.Parse(isTopFace ? "#47627B" : isBottomFace ? "#253240" : "#314252");
            var color = ApplyDepthFade(baseColor, view.NormalizeDepth(depth), isTopFace ? (byte)48 : (byte)28);
            context.DrawLine(new Pen(new SolidColorBrush(color), isTopFace ? 1 : 0.8), start.Screen, end.Screen);
        }
    }

    private static void DrawBoundsFrame(DrawingContext context, Rect viewport)
    {
        var borderPen = new Pen(new SolidColorBrush(Color.Parse("#2A2A2A")), 1);
        context.DrawRectangle(null, borderPen, viewport.Deflate(0.5));
    }

    private static void DrawScreenGrid(DrawingContext context, Rect viewport)
    {
        var gridPen = new Pen(new SolidColorBrush(Color.Parse("#1F1F1F")), 1);
        const double spacing = 32;

        for (double x = spacing; x < viewport.Width; x += spacing)
        {
            context.DrawLine(gridPen, new Point(x, 0), new Point(x, viewport.Height));
        }

        for (double y = spacing; y < viewport.Height; y += spacing)
        {
            context.DrawLine(gridPen, new Point(0, y), new Point(viewport.Width, y));
        }
    }

    private static void DrawCenteredText(DrawingContext context, Rect viewport, string text)
    {
        var formattedText = new FormattedText(
            text,
            CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight,
            Typeface.Default,
            13,
            new SolidColorBrush(Color.Parse("#7A7A7A")));

        var origin = new Point(
            (viewport.Width - formattedText.Width) / 2,
            (viewport.Height - formattedText.Height) / 2);

        context.DrawText(formattedText, origin);
    }

    private ViewTransform CreateViewTransform(GCodeDocument document, Rect viewport)
    {
        double usableWidth = Math.Max(viewport.Width - (ViewPadding * 2), 1);
        double usableHeight = Math.Max(viewport.Height - (ViewPadding * 2), 1);
        double screenCenterX = (viewport.Width / 2) + _panOffset.X;
        double screenCenterY = (viewport.Height / 2) + _panOffset.Y;

        if (!Is3DView)
        {
            double contentWidth = Math.Max(document.WidthMm, 1);
            double contentHeight = Math.Max(document.HeightMm, 1);
            double baseScale = Math.Min(usableWidth / contentWidth, usableHeight / contentHeight);
            double scale = baseScale * _zoom;

            return ViewTransform.Create2D(
                (document.MinX + document.MaxX) * 0.5,
                (document.MinY + document.MaxY) * 0.5,
                scale,
                screenCenterX,
                screenCenterY);
        }

        double centerX = (document.MinX + document.MaxX) * 0.5;
        double centerY = (document.MinY + document.MaxY) * 0.5;
        double centerZ = (document.MinZ + Math.Max(document.MaxZ, 0)) * 0.5;
        double maxDimension = Math.Max(Math.Max(document.WidthMm, document.HeightMm), Math.Max(Math.Max(document.MaxZ, 0) - document.MinZ, 1));
        double cameraDistance = Math.Max(80, maxDimension * 4.5);

        double minProjectedX = double.PositiveInfinity;
        double maxProjectedX = double.NegativeInfinity;
        double minProjectedY = double.PositiveInfinity;
        double maxProjectedY = double.NegativeInfinity;
        double minProjectedDepth = double.PositiveInfinity;
        double maxProjectedDepth = double.NegativeInfinity;

        void include(Point3D point)
        {
            var projected = ViewTransform.ProjectRelative(point, centerX, centerY, centerZ, _yawDegrees, _pitchDegrees, cameraDistance);
            minProjectedX = Math.Min(minProjectedX, projected.Screen.X);
            maxProjectedX = Math.Max(maxProjectedX, projected.Screen.X);
            minProjectedY = Math.Min(minProjectedY, projected.Screen.Y);
            maxProjectedY = Math.Max(maxProjectedY, projected.Screen.Y);
            minProjectedDepth = Math.Min(minProjectedDepth, projected.Depth);
            maxProjectedDepth = Math.Max(maxProjectedDepth, projected.Depth);
        }

        foreach (var segment in document.Segments)
        {
            include(segment.Start);
            include(segment.End);
        }

        if (ShowStockBox)
        {
            double stockTop = Math.Max(document.MaxZ, 0);
            include(new Point3D(document.MinX, document.MinY, document.MinZ));
            include(new Point3D(document.MaxX, document.MinY, document.MinZ));
            include(new Point3D(document.MaxX, document.MaxY, document.MinZ));
            include(new Point3D(document.MinX, document.MaxY, document.MinZ));
            include(new Point3D(document.MinX, document.MinY, stockTop));
            include(new Point3D(document.MaxX, document.MinY, stockTop));
            include(new Point3D(document.MaxX, document.MaxY, stockTop));
            include(new Point3D(document.MinX, document.MaxY, stockTop));
        }

        include(new Point3D(PositionX, PositionY, PositionZ));

        double projectedWidth = Math.Max(maxProjectedX - minProjectedX, 1);
        double projectedHeight = Math.Max(maxProjectedY - minProjectedY, 1);
        double projectedScale = Math.Min(usableWidth / projectedWidth, usableHeight / projectedHeight);

        return ViewTransform.Create3D(
            centerX,
            centerY,
            centerZ,
            _yawDegrees,
            _pitchDegrees,
            cameraDistance,
            projectedScale * _zoom,
            screenCenterX,
            screenCenterY,
            (minProjectedX + maxProjectedX) * 0.5,
            (minProjectedY + maxProjectedY) * 0.5,
            minProjectedDepth,
            maxProjectedDepth);
    }

    private void ResetView()
    {
        _zoom = 1.0;
        _panOffset = default;
        ApplyPreset(CameraPreset);
        InvalidateVisual();
    }

    private void ApplyPreset(string? preset)
    {
        if (!Is3DView)
        {
            _yawDegrees = 0;
            _pitchDegrees = 0;
            return;
        }

        switch ((preset ?? string.Empty).Trim().ToLowerInvariant())
        {
            case "top":
                _yawDegrees = 0;
                _pitchDegrees = 0;
                break;
            case "front":
                _yawDegrees = 0;
                _pitchDegrees = 85;
                break;
            case "right":
                _yawDegrees = 90;
                _pitchDegrees = 85;
                break;
            default:
                _yawDegrees = -42;
                _pitchDegrees = 48;
                break;
        }
    }

    private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var props = e.GetCurrentPoint(this).Properties;
        if (props.IsRightButtonPressed)
        {
            _interactionMode = InteractionMode.Pan;
        }
        else if (props.IsLeftButtonPressed)
        {
            _interactionMode = Is3DView ? InteractionMode.Orbit : InteractionMode.Pan;
        }
        else
        {
            return;
        }

        _lastPointerPoint = e.GetPosition(this);
        e.Pointer.Capture(this);
    }

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_interactionMode == InteractionMode.None)
        {
            return;
        }

        Point current = e.GetPosition(this);
        Vector delta = current - _lastPointerPoint;
        _lastPointerPoint = current;

        if (_interactionMode == InteractionMode.Pan)
        {
            _panOffset += delta * PanSensitivity;
        }
        else if (_interactionMode == InteractionMode.Orbit)
        {
            _yawDegrees += delta.X * OrbitSensitivity;
            _pitchDegrees = Math.Clamp(_pitchDegrees - (delta.Y * OrbitSensitivity), -PitchClamp, PitchClamp);
        }

        InvalidateVisual();
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_interactionMode == InteractionMode.None)
        {
            return;
        }

        _interactionMode = InteractionMode.None;
        e.Pointer.Capture(null);
    }

    private void OnPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        double factor = e.Delta.Y >= 0 ? ZoomStep : 1 / ZoomStep;
        _zoom = Math.Clamp(_zoom * factor, MinZoom, MaxZoom);
        InvalidateVisual();
    }

    private static Color InterpolateColor(Color start, Color end, double t)
    {
        byte lerp(byte a, byte b) => (byte)Math.Round(a + ((b - a) * t));
        return Color.FromArgb(
            lerp(start.A, end.A),
            lerp(start.R, end.R),
            lerp(start.G, end.G),
            lerp(start.B, end.B));
    }

    private static Color ApplyDepthFade(Color color, double normalizedDepth, byte minimumAlpha)
    {
        normalizedDepth = Math.Clamp(normalizedDepth, 0, 1);
        double alpha = minimumAlpha + ((color.A - minimumAlpha) * (1 - normalizedDepth));
        return Color.FromArgb((byte)Math.Clamp(Math.Round(alpha), minimumAlpha, color.A), color.R, color.G, color.B);
    }

    private static double ChooseGridStep(double span)
    {
        if (span <= 20) return 2;
        if (span <= 50) return 5;
        if (span <= 100) return 10;
        if (span <= 250) return 20;
        return 50;
    }

    private static double RoundDown(double value, double step)
        => Math.Floor(value / step) * step;

    private readonly record struct RenderedSegment(ProjectedPoint Start, ProjectedPoint End, Pen Pen, double Depth);

    private enum InteractionMode
    {
        None,
        Pan,
        Orbit
    }

    private readonly record struct ProjectedPoint(Point Screen, double Depth);

    private readonly record struct ViewTransform(
        bool Is3D,
        double CenterWorldX,
        double CenterWorldY,
        double CenterWorldZ,
        double YawDegrees,
        double PitchDegrees,
        double CameraDistance,
        double Scale,
        double CenterScreenX,
        double CenterScreenY,
        double ProjectedCenterX,
        double ProjectedCenterY,
        double MinProjectedDepth,
        double MaxProjectedDepth)
    {
        public static ViewTransform Create2D(
            double centerWorldX,
            double centerWorldY,
            double scale,
            double centerScreenX,
            double centerScreenY)
            => new(
                false,
                centerWorldX,
                centerWorldY,
                0,
                0,
                0,
                1,
                scale,
                centerScreenX,
                centerScreenY,
                0,
                0,
                0,
                1);

        public static ViewTransform Create3D(
            double centerWorldX,
            double centerWorldY,
            double centerWorldZ,
            double yawDegrees,
            double pitchDegrees,
            double cameraDistance,
            double scale,
            double centerScreenX,
            double centerScreenY,
            double projectedCenterX,
            double projectedCenterY,
            double minProjectedDepth,
            double maxProjectedDepth)
            => new(
                true,
                centerWorldX,
                centerWorldY,
                centerWorldZ,
                yawDegrees,
                pitchDegrees,
                cameraDistance,
                scale,
                centerScreenX,
                centerScreenY,
                projectedCenterX,
                projectedCenterY,
                minProjectedDepth,
                maxProjectedDepth);

        public ProjectedPoint Project(Point3D point)
        {
            if (!Is3D)
            {
                double dx = (point.X - CenterWorldX) * Scale;
                double dy = (point.Y - CenterWorldY) * Scale;
                return new ProjectedPoint(
                    new Point(CenterScreenX + dx, CenterScreenY - dy),
                    point.Z - CenterWorldZ);
            }

            var projected = ProjectRelative(point, CenterWorldX, CenterWorldY, CenterWorldZ, YawDegrees, PitchDegrees, CameraDistance);
            return new ProjectedPoint(
                new Point(
                    CenterScreenX + ((projected.Screen.X - ProjectedCenterX) * Scale),
                    CenterScreenY - ((projected.Screen.Y - ProjectedCenterY) * Scale)),
                projected.Depth);
        }

        public double NormalizeDepth(double depth)
        {
            if (!Is3D)
            {
                return 0;
            }

            double range = Math.Max(MaxProjectedDepth - MinProjectedDepth, 0.0001);
            return (depth - MinProjectedDepth) / range;
        }

        public static ProjectedPoint ProjectRelative(
            Point3D point,
            double centerWorldX,
            double centerWorldY,
            double centerWorldZ,
            double yawDegrees,
            double pitchDegrees,
            double cameraDistance)
        {
            double yaw = DegreesToRadians(yawDegrees);
            double pitch = DegreesToRadians(pitchDegrees);

            double x = point.X - centerWorldX;
            double y = point.Y - centerWorldY;
            double z = point.Z - centerWorldZ;

            double cosYaw = Math.Cos(yaw);
            double sinYaw = Math.Sin(yaw);
            double cosPitch = Math.Cos(pitch);
            double sinPitch = Math.Sin(pitch);

            double x1 = (x * cosYaw) - (y * sinYaw);
            double depth1 = (x * sinYaw) + (y * cosYaw);
            double y2 = (z * sinPitch) + (depth1 * cosPitch);
            double depth2 = (depth1 * sinPitch) - (z * cosPitch);

            double perspective = cameraDistance / Math.Max(cameraDistance + depth2, 1);
            return new ProjectedPoint(new Point(x1 * perspective, y2 * perspective), depth2);
        }

        private static double DegreesToRadians(double degrees)
            => degrees * Math.PI / 180.0;
    }
}
