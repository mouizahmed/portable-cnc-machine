using System;
using System.Collections.Generic;
using System.Numerics;
using PortableCncApp.Services.GCode;
using Silk.NET.OpenGL;

namespace PortableCncApp.Rendering;

/// <summary>
/// Orchestrates all GL sub-renderers. All GL calls happen on the GL thread (inside OpenGlControlBase callbacks).
/// </summary>
internal sealed class ToolpathGlRenderer : IDisposable
{
    private readonly GL _gl;
    private uint _lineProgram;
    private uint _meshProgram;

    private readonly LineRenderer _lines;
    private readonly GridRenderer _grid;
    private readonly MarkerRenderer _marker;
    private readonly OrbitCamera _camera = new();

    private GCodeDocument? _document;
    private PlaybackFrame[] _playbackFrames = Array.Empty<PlaybackFrame>();

    // State snapshot (set from UI thread, read on GL thread)
    private volatile bool _documentDirty;
    private volatile bool _gridDirty;
    private volatile bool _initialized;

    private GCodeDocument? _pendingDocument;
    private bool _pendingIsDark;
    private bool _pendingShowGrid;
    private bool _pendingShowStockBox;

    private int _currentLine;
    private bool _showRapids = true;
    private bool _showCuts = true;
    private bool _showArcs = true;
    private bool _showPlunges = true;
    private bool _showCompleted = true;
    private bool _showRemaining = true;
    private bool _showToolpathPoints;
    private bool _isDark;
    private Vector3 _machinePosition;
    private string _cameraPreset = "Iso";
    private string _previewPlaybackMode = "stopped";
    private double _previewPlaybackStepDurationMs;

    private float _viewportWidth = 800;
    private float _viewportHeight = 600;
    private Vector3 _markerDisplayPosition;
    private Vector3 _markerTargetPosition;
    private Vector3 _markerStartPosition;
    private DateTime _markerTransitionStartUtc = DateTime.UtcNow;
    private bool _markerInitialized;
    private bool _needsAnimationFrame;
    private int _lastMarkerLine = int.MinValue;
    private Vector3 _lastMachinePosition = new(float.NaN, float.NaN, float.NaN);
    private int _markerAnimationFrameIndex = -1;
    private int _markerAnimationDirection;

    public ToolpathGlRenderer(GL gl)
    {
        _gl = gl;
        _lines = new LineRenderer(gl);
        _grid = new GridRenderer(gl);
        _marker = new MarkerRenderer(gl);
    }

    public void Initialize()
    {
        _lineProgram = GlShaderHelper.CompileProgram(_gl,
            "PortableCncApp.Rendering.Shaders.line.vert",
            "PortableCncApp.Rendering.Shaders.line.frag");
        _meshProgram = GlShaderHelper.CompileProgram(_gl,
            "PortableCncApp.Rendering.Shaders.mesh.vert",
            "PortableCncApp.Rendering.Shaders.mesh.frag");

        _lines.Initialize();
        _grid.Initialize();
        _marker.Initialize();

        _gl.Enable(EnableCap.Blend);
        _gl.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
        _gl.Enable(EnableCap.DepthTest);
        _gl.LineWidth(1.0f);

        _initialized = true;
    }

    public void SetViewport(float width, float height)
    {
        _viewportWidth = Math.Max(width, 1);
        _viewportHeight = Math.Max(height, 1);
    }

    // -- UI-thread setters (snapshot into pending state) --

    public void SetDocument(GCodeDocument? document, bool isDark)
    {
        _pendingDocument = document;
        _pendingIsDark = isDark;
        _documentDirty = true;
        _gridDirty = true;
    }

    public void SetCurrentLine(int line) => _currentLine = line;

    public void SetMachinePosition(double x, double y, double z)
        => _machinePosition = new Vector3((float)x, (float)y, (float)z);

    public void SetVisibility(bool showRapids, bool showCuts, bool showArcs, bool showPlunges,
                               bool showCompleted, bool showRemaining)
    {
        _showRapids = showRapids;
        _showCuts = showCuts;
        _showArcs = showArcs;
        _showPlunges = showPlunges;
        _showCompleted = showCompleted;
        _showRemaining = showRemaining;
    }

    public void SetGridVisibility(bool showGrid, bool showStockBox)
    {
        _pendingShowGrid = showGrid;
        _pendingShowStockBox = showStockBox;
        _gridDirty = true;
    }

    public void SetToolpathPointsVisible(bool showToolpathPoints) => _showToolpathPoints = showToolpathPoints;

    public void SetPlaybackState(string mode, double stepDurationMs)
    {
        _previewPlaybackMode = mode;
        _previewPlaybackStepDurationMs = stepDurationMs;
    }

    public void SetTheme(bool isDark)
    {
        if (_isDark == isDark) return;
        _isDark = isDark;
        _pendingIsDark = isDark;
    }

    public void FitCamera()
    {
        var doc = _document;
        if (doc != null && doc.HasGeometry)
            _camera.FitToBounds((float)doc.MinX, (float)doc.MaxX, (float)doc.MinY, (float)doc.MaxY, (float)doc.MinZ, (float)doc.MaxZ, _viewportWidth / Math.Max(_viewportHeight, 1f));
    }

    public void SetCameraPreset(string preset)
    {
        _cameraPreset = string.IsNullOrWhiteSpace(preset) ? "Iso" : preset;
        _camera.SetPreset(_cameraPreset);
    }
    public void Orbit(float dx, float dy) => _camera.Orbit(dx, dy);
    public void Pan(float dx, float dy) => _camera.Pan(dx, dy, _viewportWidth, _viewportHeight);
    public void Zoom(float delta) => _camera.Zoom(delta);
    public bool WantsAnimationFrame => _needsAnimationFrame;

    // -- GL thread render --

    public unsafe void Render(int framebuffer, float width, float height)
    {
        if (!_initialized) return;

        SetViewport(width, height);

        // Apply pending document update
        if (_documentDirty)
        {
            _documentDirty = false;
            _document = _pendingDocument;
            _isDark = _pendingIsDark;
            _lines.UploadGeometry(_document);
            BuildPlaybackFrames(_document);
            RebuildGrid();
            _markerInitialized = false;
            _markerAnimationFrameIndex = -1;
            _markerAnimationDirection = 0;
            if (_document != null && _document.HasGeometry)
            {
                _camera.SetPreset(_cameraPreset);
                _camera.FitToBounds((float)_document.MinX, (float)_document.MaxX,
                                    (float)_document.MinY, (float)_document.MaxY,
                                    (float)_document.MinZ, (float)_document.MaxZ,
                                    width / Math.Max(height, 1f));
            }
        }
        else if (_gridDirty)
        {
            _gridDirty = false;
            RebuildGrid();
        }

        // Clear
        float bgR = _isDark ? 0.08f : 0.88f;
        float bgG = _isDark ? 0.09f : 0.90f;
        float bgB = _isDark ? 0.10f : 0.91f;
        _gl.ClearColor(bgR, bgG, bgB, 1.0f);
        _gl.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);
        _gl.Viewport(0, 0, (uint)width, (uint)height);

        float aspect = width / Math.Max(height, 1f);
        float[] vp = _camera.GetViewProjection(aspect);

        // Draw grid first (behind toolpath)
        _grid.Draw(_lineProgram, vp, _isDark);

        // Draw toolpath
        int completed = _lines.ComputeCompletedCount(_currentLine);
        _lines.Draw(_lineProgram, vp, _isDark, completed,
            _showRapids, _showCuts, _showArcs, _showPlunges,
            _showCompleted, _showRemaining);
        if (_showToolpathPoints)
        {
            _lines.DrawPoints(_lineProgram, vp, _isDark, completed,
                _showRapids, _showCuts, _showArcs, _showPlunges,
                _showCompleted, _showRemaining);
        }

        // Draw marker at current tool position
        if (_document != null && _document.HasGeometry)
        {
            UpdateMarkerState(completed);
            _marker.Draw(_meshProgram, vp, _markerDisplayPosition, _isDark);
        }
        else
        {
            _needsAnimationFrame = false;
        }
    }

    private void RebuildGrid()
    {
        var doc = _document;
        if (doc != null && doc.HasGeometry)
        {
            _grid.BuildGeometry(doc.MinX, doc.MaxX, doc.MinY, doc.MaxY, doc.MinZ, doc.MaxZ,
                _pendingShowGrid, _pendingShowStockBox);
        }
        else
        {
            _grid.BuildGeometry(0, 0, 0, 0, 0, 0, false, false);
        }
    }

    private void BuildPlaybackFrames(GCodeDocument? doc)
    {
        if (doc == null || doc.Segments.Length == 0)
        {
            _playbackFrames = Array.Empty<PlaybackFrame>();
            return;
        }

        var segments = doc.Segments;
        var frames = new List<PlaybackFrame>(segments.Length + 1)
        {
            new PlaybackFrame(
                SourceLine: 0,
                StartSegmentIndex: -1,
                EndSegmentIndex: -1,
                Position: ToVector3(segments[0].Start))
        };

        int frameStartIndex = 0;
        int currentLine = segments[0].SourceLine;

        for (int i = 1; i < segments.Length; i++)
        {
            if (segments[i].SourceLine == currentLine)
            {
                continue;
            }

            frames.Add(CreatePlaybackFrame(segments, currentLine, frameStartIndex, i - 1));
            frameStartIndex = i;
            currentLine = segments[i].SourceLine;
        }

        frames.Add(CreatePlaybackFrame(segments, currentLine, frameStartIndex, segments.Length - 1));
        _playbackFrames = frames.ToArray();
    }

    private Vector3 GetMarkerPosition(int completedCount)
    {
        if (_currentLine <= 0)
        {
            return _machinePosition;
        }

        int frameIndex = FindPlaybackFrameIndex(_currentLine);
        if (frameIndex < 0)
        {
            return _document is { Segments.Length: > 0 } doc
                ? ToVector3(doc.Segments[0].Start)
                : Vector3.Zero;
        }

        return _playbackFrames[frameIndex].Position;
    }

    private void UpdateMarkerState(int completedCount)
    {
        Vector3 target = GetMarkerPosition(completedCount);
        int targetFrameIndex = FindPlaybackFrameIndex(_currentLine);
        int previousFrameIndex = FindPlaybackFrameIndex(_lastMarkerLine);
        bool targetChanged = !_markerInitialized
            || _lastMarkerLine != _currentLine
            || Vector3.DistanceSquared(_lastMachinePosition, _machinePosition) > 0.0001f
            || Vector3.DistanceSquared(_markerTargetPosition, target) > 0.0001f;
        bool animatePlayback = string.Equals(_previewPlaybackMode, "forward", StringComparison.OrdinalIgnoreCase)
            || string.Equals(_previewPlaybackMode, "reverse", StringComparison.OrdinalIgnoreCase);

        if (!_markerInitialized)
        {
            _markerDisplayPosition = target;
            _markerTargetPosition = target;
            _markerStartPosition = target;
            _markerInitialized = true;
            _needsAnimationFrame = false;
            _markerAnimationFrameIndex = -1;
            _markerAnimationDirection = 0;
        }
        else if (targetChanged)
        {
            bool steppedToDifferentFrame = targetFrameIndex != previousFrameIndex && targetFrameIndex >= 0;
            if (animatePlayback && _currentLine >= 0 && steppedToDifferentFrame)
            {
                _markerStartPosition = _markerDisplayPosition;
                _markerTargetPosition = target;
                _markerTransitionStartUtc = DateTime.UtcNow;
                _markerAnimationDirection = _currentLine >= _lastMarkerLine ? 1 : -1;
                _markerAnimationFrameIndex = _markerAnimationDirection > 0 ? targetFrameIndex : previousFrameIndex;
            }
            else
            {
                _markerDisplayPosition = target;
                _markerTargetPosition = target;
                _markerStartPosition = target;
                _markerAnimationFrameIndex = -1;
                _markerAnimationDirection = 0;
            }
        }

        if (animatePlayback &&
            _markerAnimationFrameIndex >= 0 &&
            _markerAnimationDirection != 0 &&
            Vector3.DistanceSquared(_markerDisplayPosition, _markerTargetPosition) > 0.0001f)
        {
            float durationSeconds = (float)Math.Max(_previewPlaybackStepDurationMs / 1000.0, 0.001);
            float t = Math.Clamp((float)(DateTime.UtcNow - _markerTransitionStartUtc).TotalSeconds / durationSeconds, 0f, 1f);
            _markerDisplayPosition = InterpolateAlongPlaybackFrame(_markerAnimationFrameIndex, _markerAnimationDirection, t);
            _needsAnimationFrame = t < 1f;
        }
        else
        {
            _markerDisplayPosition = _markerTargetPosition;
            _needsAnimationFrame = false;
            _markerAnimationFrameIndex = -1;
            _markerAnimationDirection = 0;
        }

        _lastMarkerLine = _currentLine;
        _lastMachinePosition = _machinePosition;
    }

    private int FindPlaybackFrameIndex(int sourceLine)
    {
        if (_playbackFrames.Length == 0)
        {
            return -1;
        }

        int lo = 0;
        int hi = _playbackFrames.Length - 1;
        int result = 0;

        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (_playbackFrames[mid].SourceLine <= sourceLine)
            {
                result = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }

        return result;
    }

    private Vector3 InterpolateAlongPlaybackFrame(int frameIndex, int direction, float t)
    {
        if (_document == null || frameIndex < 0 || frameIndex >= _playbackFrames.Length)
        {
            return Vector3.Lerp(_markerStartPosition, _markerTargetPosition, t);
        }

        var frame = _playbackFrames[frameIndex];
        if (frame.StartSegmentIndex < 0 || frame.EndSegmentIndex < frame.StartSegmentIndex)
        {
            return frame.Position;
        }

        int pointCount = frame.EndSegmentIndex - frame.StartSegmentIndex + 1;
        if (pointCount <= 0)
        {
            return frame.Position;
        }

        float raw = direction > 0 ? t * pointCount : (1f - t) * pointCount;
        int pointIndex = Math.Min((int)MathF.Floor(raw), pointCount - 1);
        float pointT = raw - pointIndex;

        Vector3 p0 = pointIndex == 0
            ? ToVector3(_document.Segments[frame.StartSegmentIndex].Start)
            : ToVector3(_document.Segments[frame.StartSegmentIndex + pointIndex - 1].End);
        Vector3 p1 = ToVector3(_document.Segments[frame.StartSegmentIndex + pointIndex].End);
        return Vector3.Lerp(p0, p1, pointT);
    }

    private static PlaybackFrame CreatePlaybackFrame(
        System.Collections.Immutable.ImmutableArray<ToolpathSegment> segments,
        int sourceLine,
        int startSegmentIndex,
        int endSegmentIndex)
    {
        return new PlaybackFrame(
            SourceLine: sourceLine,
            StartSegmentIndex: startSegmentIndex,
            EndSegmentIndex: endSegmentIndex,
            Position: ToVector3(segments[endSegmentIndex].End));
    }

    private static Vector3 ToVector3(Point3D point)
        => new((float)point.X, (float)point.Y, (float)point.Z);

    private readonly record struct PlaybackFrame(
        int SourceLine,
        int StartSegmentIndex,
        int EndSegmentIndex,
        Vector3 Position);

    public void Dispose()
    {
        _lines.Dispose();
        _grid.Dispose();
        _marker.Dispose();
        if (_lineProgram != 0) _gl.DeleteProgram(_lineProgram);
        if (_meshProgram != 0) _gl.DeleteProgram(_meshProgram);
    }
}
