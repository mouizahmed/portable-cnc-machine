using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Input.Platform;
using Avalonia.OpenGL;
using Avalonia.OpenGL.Controls;
using PortableCncApp.Services.GCode;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

/// <summary>
/// Native OpenGL toolpath viewer control using Avalonia's OpenGlControlBase + Silk.NET.
/// Exposes Avalonia StyledProperties for dashboard data binding.
/// </summary>
public sealed class NativeGlToolpathView : OpenGlControlBase, IToolpathViewportBridge
{
    // ---- Styled Properties ----

    public static readonly StyledProperty<GCodeDocument?> DocumentProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, GCodeDocument?>(nameof(Document));

    public static readonly StyledProperty<int> CurrentLineProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, int>(nameof(CurrentLine));

    public static readonly StyledProperty<double> PositionXProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, double>(nameof(PositionX));

    public static readonly StyledProperty<double> PositionYProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, double>(nameof(PositionY));

    public static readonly StyledProperty<double> PositionZProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, double>(nameof(PositionZ));

    public static readonly StyledProperty<string> CameraPresetProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, string>(nameof(CameraPreset), "Iso");

    public static readonly StyledProperty<int> ResetViewTokenProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, int>(nameof(ResetViewToken));

    public static readonly StyledProperty<bool> ShowRapidsProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowRapids), true);

    public static readonly StyledProperty<bool> ShowCutsProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowCuts), true);

    public static readonly StyledProperty<bool> ShowArcsProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowArcs), true);

    public static readonly StyledProperty<bool> ShowPlungesProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowPlunges), true);

    public static readonly StyledProperty<bool> ShowCompletedPathProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowCompletedPath), true);

    public static readonly StyledProperty<bool> ShowRemainingPathProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowRemainingPath), true);

    public static readonly StyledProperty<bool> ShowStockBoxProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowStockBox), false);

    public static readonly StyledProperty<bool> ShowGridProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowGrid), true);

    public static readonly StyledProperty<bool> ShowToolpathPointsProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, bool>(nameof(ShowToolpathPoints), false);

    public static readonly StyledProperty<string> PreviewPlaybackModeProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, string>(nameof(PreviewPlaybackMode), "stopped");

    public static readonly StyledProperty<double> PreviewPlaybackStepDurationMsProperty =
        AvaloniaProperty.Register<NativeGlToolpathView, double>(nameof(PreviewPlaybackStepDurationMs), 0d);

    // ---- Properties ----

    public GCodeDocument? Document { get => GetValue(DocumentProperty); set => SetValue(DocumentProperty, value); }
    public int CurrentLine { get => GetValue(CurrentLineProperty); set => SetValue(CurrentLineProperty, value); }
    public double PositionX { get => GetValue(PositionXProperty); set => SetValue(PositionXProperty, value); }
    public double PositionY { get => GetValue(PositionYProperty); set => SetValue(PositionYProperty, value); }
    public double PositionZ { get => GetValue(PositionZProperty); set => SetValue(PositionZProperty, value); }
    public string CameraPreset { get => GetValue(CameraPresetProperty); set => SetValue(CameraPresetProperty, value); }
    public int ResetViewToken { get => GetValue(ResetViewTokenProperty); set => SetValue(ResetViewTokenProperty, value); }
    public bool ShowRapids { get => GetValue(ShowRapidsProperty); set => SetValue(ShowRapidsProperty, value); }
    public bool ShowCuts { get => GetValue(ShowCutsProperty); set => SetValue(ShowCutsProperty, value); }
    public bool ShowArcs { get => GetValue(ShowArcsProperty); set => SetValue(ShowArcsProperty, value); }
    public bool ShowPlunges { get => GetValue(ShowPlungesProperty); set => SetValue(ShowPlungesProperty, value); }
    public bool ShowCompletedPath { get => GetValue(ShowCompletedPathProperty); set => SetValue(ShowCompletedPathProperty, value); }
    public bool ShowRemainingPath { get => GetValue(ShowRemainingPathProperty); set => SetValue(ShowRemainingPathProperty, value); }
    public bool ShowStockBox { get => GetValue(ShowStockBoxProperty); set => SetValue(ShowStockBoxProperty, value); }
    public bool ShowGrid { get => GetValue(ShowGridProperty); set => SetValue(ShowGridProperty, value); }
    public bool ShowToolpathPoints { get => GetValue(ShowToolpathPointsProperty); set => SetValue(ShowToolpathPointsProperty, value); }
    public string PreviewPlaybackMode { get => GetValue(PreviewPlaybackModeProperty); set => SetValue(PreviewPlaybackModeProperty, value); }
    public double PreviewPlaybackStepDurationMs { get => GetValue(PreviewPlaybackStepDurationMsProperty); set => SetValue(PreviewPlaybackStepDurationMsProperty, value); }

    // ---- GL state ----

    private IToolpathRenderBackend? _renderer;
    private bool _rendererFailed;
    private int _lastResetToken = int.MinValue;
    private double _lastLoggedRenderScale = double.NaN;
    private int _lastLoggedPixelWidth = -1;
    private int _lastLoggedPixelHeight = -1;

    // ---- Mouse input state ----

    private bool _isOrbiting;
    private bool _isPanning;
    private Point _lastPointerPos;

    // ---- Constructor ----

    public NativeGlToolpathView()
    {
        Focusable = true;
        IsHitTestVisible = true;
        Cursor = new Cursor(StandardCursorType.Arrow);

        // Tunnel handlers fire before parent gesture logic can swallow the event.
        AddHandler(PointerPressedEvent,      OnViewPointerPressed,     RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerMovedEvent,        OnViewPointerMoved,       RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerReleasedEvent,     OnViewPointerReleased,    RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerWheelChangedEvent, OnViewPointerWheel,       RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(PointerCaptureLostEvent,  OnViewPointerCaptureLost, RoutingStrategies.Tunnel, handledEventsToo: true);

        ThemeResources.ThemeChanged += OnThemeChanged;
        Unloaded += (_, _) => ThemeResources.ThemeChanged -= OnThemeChanged;
    }

    // ---- IToolpathViewportBridge ----

    public void LoadScene(GCodeDocument? document)
    {
        _renderer?.LoadScene(document, IsDark());
        RequestNextFrameRendering();
    }

    public void ApplyState(ToolpathViewportState state)
    {
        _renderer?.SetCurrentLine(state.CurrentLine);
        _renderer?.SetMachinePosition(state.PositionX, state.PositionY, state.PositionZ);
        _renderer?.SetVisibility(state.ShowRapids, state.ShowCuts, state.ShowArcs, state.ShowPlunges,
            state.ShowCompletedPath, state.ShowRemainingPath);
        _renderer?.SetGridVisibility(state.ShowGrid, state.ShowStockBox);
        _renderer?.SetToolpathPointsVisible(state.ShowToolpathPoints);
        _renderer?.SetPlaybackState(state.PreviewPlaybackMode, state.PreviewPlaybackStepDurationMs);
        RequestNextFrameRendering();
    }

    public void ResetCamera()
    {
        _renderer?.ResetCamera();
        RequestNextFrameRendering();
    }

    public void SetCameraPreset(string preset)
    {
        _renderer?.SetCameraPreset(preset);
        RequestNextFrameRendering();
    }

    public void OrbitBy(float deltaX, float deltaY)
    {
        _renderer?.Orbit(deltaX, deltaY);
        RequestNextFrameRendering();
    }

    public void PanBy(float deltaX, float deltaY)
    {
        _renderer?.Pan(deltaX, deltaY);
        RequestNextFrameRendering();
    }

    public void ZoomBy(float delta)
    {
        _renderer?.Zoom(delta);
        RequestNextFrameRendering();
    }

    // ---- Avalonia OpenGlControlBase overrides ----

    protected override void OnOpenGlInit(GlInterface gl)
    {
        try
        {
            var silkGl = GL.GetApi(name => gl.GetProcAddress(name));
            _renderer = new ToolpathGlRenderer(silkGl);
            _renderer.Initialize();
            _renderer.LoadScene(Document, IsDark());
            _renderer.SetCurrentLine(CurrentLine);
            SyncAllState();
            Trace.WriteLine($"Toolpath viewer backend initialized: {_renderer.Diagnostics.ToLogString()}");
            Console.WriteLine($"Toolpath viewer backend initialized: {_renderer.Diagnostics.ToLogString()}");
            _rendererFailed = false;
        }
        catch (Exception ex)
        {
            _rendererFailed = true;
            _renderer?.Dispose();
            _renderer = null;
            Trace.WriteLine($"Toolpath viewer initialization failed: {ex}");
            Console.Error.WriteLine($"Toolpath viewer initialization failed: {ex}");
        }
    }

    protected override void OnOpenGlDeinit(GlInterface gl)
    {
        _renderer?.Dispose();
        _renderer = null;
    }

    protected override void OnOpenGlRender(GlInterface gl, int fb)
    {
        if (_rendererFailed || _renderer == null)
        {
            return;
        }

        try
        {
            var scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            var pixelWidth = (float)Math.Max(1.0, Bounds.Width * scale);
            var pixelHeight = (float)Math.Max(1.0, Bounds.Height * scale);
            LogFramebufferMetrics(scale, pixelWidth, pixelHeight);
            _renderer.Render(fb, pixelWidth, pixelHeight);
        }
        catch (Exception ex)
        {
            _rendererFailed = true;
            var scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            var pixelWidth = (float)Math.Max(1.0, Bounds.Width * scale);
            var pixelHeight = (float)Math.Max(1.0, Bounds.Height * scale);
            Trace.WriteLine($"Toolpath viewer render failed: scale={scale:F2} framebuffer={pixelWidth:F0}x{pixelHeight:F0} bounds={Bounds.Width:F1}x{Bounds.Height:F1} {ex}");
            Console.Error.WriteLine($"Toolpath viewer render failed: scale={scale:F2} framebuffer={pixelWidth:F0}x{pixelHeight:F0} bounds={Bounds.Width:F1}x{Bounds.Height:F1} {ex}");
            return;
        }

        if (_renderer.WantsAnimationFrame)
        {
            RequestNextFrameRendering();
        }
    }

    // ---- Property change handling ----

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (_renderer == null) return;

        if (change.Property == DocumentProperty)
        {
            _renderer.LoadScene((GCodeDocument?)change.NewValue, IsDark());
            RequestNextFrameRendering();
        }
        else if (change.Property == CurrentLineProperty)
        {
            _renderer.SetCurrentLine((int)(change.NewValue ?? 0));
            RequestNextFrameRendering();
        }
        else if (change.Property == PositionXProperty ||
                 change.Property == PositionYProperty ||
                 change.Property == PositionZProperty)
        {
            _renderer.SetMachinePosition(PositionX, PositionY, PositionZ);
            RequestNextFrameRendering();
        }
        else if (change.Property == ShowRapidsProperty ||
                 change.Property == ShowCutsProperty ||
                 change.Property == ShowArcsProperty ||
                 change.Property == ShowPlungesProperty ||
                 change.Property == ShowCompletedPathProperty ||
                 change.Property == ShowRemainingPathProperty)
        {
            SyncVisibility();
            RequestNextFrameRendering();
        }
        else if (change.Property == ShowGridProperty ||
                 change.Property == ShowStockBoxProperty)
        {
            _renderer.SetGridVisibility(ShowGrid, ShowStockBox);
            RequestNextFrameRendering();
        }
        else if (change.Property == ShowToolpathPointsProperty)
        {
            _renderer.SetToolpathPointsVisible(ShowToolpathPoints);
            RequestNextFrameRendering();
        }
        else if (change.Property == PreviewPlaybackModeProperty ||
                 change.Property == PreviewPlaybackStepDurationMsProperty)
        {
            _renderer.SetPlaybackState(PreviewPlaybackMode, PreviewPlaybackStepDurationMs);
            RequestNextFrameRendering();
        }
        else if (change.Property == ResetViewTokenProperty)
        {
            int token = (int)(change.NewValue ?? 0);
            if (token != _lastResetToken)
            {
                _lastResetToken = token;
                _renderer.SetCameraPreset(CameraPreset);
                _renderer.ResetCamera();
                RequestNextFrameRendering();
            }
        }
        else if (change.Property == CameraPresetProperty)
        {
            _renderer.SetCameraPreset((string)(change.NewValue ?? "Iso"));
            RequestNextFrameRendering();
        }
    }

    // ---- Mouse input ----

    private void OnViewPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (_renderer == null)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        _lastPointerPos = point.Position;
        Focus();

        if (point.Properties.IsLeftButtonPressed)
        {
            _isOrbiting = true;
            _isPanning = false;
            e.Pointer.Capture(this);
            e.Handled = true;
        }
        else if (point.Properties.IsRightButtonPressed || point.Properties.IsMiddleButtonPressed)
        {
            _isPanning = true;
            _isOrbiting = false;
            e.Pointer.Capture(this);
            e.Handled = true;
        }
    }

    private void OnViewPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!_isOrbiting && !_isPanning) return;

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed && !point.Properties.IsRightButtonPressed && !point.Properties.IsMiddleButtonPressed)
        {
            StopPointerInteraction(e.Pointer);
            return;
        }

        var delta = point.Position - _lastPointerPos;
        _lastPointerPos = point.Position;

        if (_isOrbiting && _renderer != null)
        {
            _renderer.Orbit((float)delta.X, (float)delta.Y);
            RequestNextFrameRendering();
        }
        else if (_isPanning && _renderer != null)
        {
            _renderer.Pan((float)delta.X, (float)delta.Y);
            RequestNextFrameRendering();
        }
        e.Handled = true;
    }

    private void OnViewPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        StopPointerInteraction(e.Pointer);
    }

    private void OnViewPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        StopPointerInteraction(e.Pointer);
    }

    private void OnViewPointerWheel(object? sender, PointerWheelEventArgs e)
    {
        if (_renderer != null)
        {
            _renderer.Zoom((float)-e.Delta.Y * 0.12f);
            RequestNextFrameRendering();
        }
        e.Handled = true;
    }

    // ---- Helpers ----

    private void OnThemeChanged(object? sender, EventArgs e)
    {
        _renderer?.SetTheme(IsDark());
        RequestNextFrameRendering();
    }

    private bool IsDark() => ThemeResources.CurrentThemeMode == "dark";

    private void SyncAllState()
    {
        SyncVisibility();
        _renderer?.SetGridVisibility(ShowGrid, ShowStockBox);
        _renderer?.SetToolpathPointsVisible(ShowToolpathPoints);
        _renderer?.SetPlaybackState(PreviewPlaybackMode, PreviewPlaybackStepDurationMs);
        _renderer?.SetMachinePosition(PositionX, PositionY, PositionZ);
        _renderer?.SetCurrentLine(CurrentLine);
    }

    private void SyncVisibility()
    {
        _renderer?.SetVisibility(ShowRapids, ShowCuts, ShowArcs, ShowPlunges, ShowCompletedPath, ShowRemainingPath);
    }

    private void LogFramebufferMetrics(double scale, float pixelWidth, float pixelHeight)
    {
        int width = (int)MathF.Round(pixelWidth);
        int height = (int)MathF.Round(pixelHeight);
        if (Math.Abs(scale - _lastLoggedRenderScale) < 0.001 &&
            width == _lastLoggedPixelWidth &&
            height == _lastLoggedPixelHeight)
        {
            return;
        }

        _lastLoggedRenderScale = scale;
        _lastLoggedPixelWidth = width;
        _lastLoggedPixelHeight = height;

        string message =
            $"Toolpath viewer framebuffer: scale={scale:F2} framebuffer={width}x{height} bounds={Bounds.Width:F1}x{Bounds.Height:F1}";
        Trace.WriteLine(message);
        Console.WriteLine(message);
    }

    private void StopPointerInteraction(IPointer pointer)
    {
        _isOrbiting = false;
        _isPanning = false;
        if (pointer.Captured == this)
        {
            pointer.Capture(null);
        }
    }
}
