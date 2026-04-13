using System;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.Rendering;

/// <summary>
/// Backend contract for the toolpath renderer. The view/control layer should only
/// depend on this narrow scene/state/camera/frame interface.
/// </summary>
internal interface IToolpathRenderBackend : IDisposable
{
    string BackendName { get; }
    ToolpathRenderBackendDiagnostics Diagnostics { get; }
    bool WantsAnimationFrame { get; }

    void Initialize();
    void LoadScene(GCodeDocument? document, bool isDark);
    void SetCurrentLine(int line);
    void SetMachinePosition(double x, double y, double z);
    void SetVisibility(bool showRapids, bool showCuts, bool showArcs, bool showPlunges,
        bool showCompleted, bool showRemaining);
    void SetGridVisibility(bool showGrid, bool showStockBox);
    void SetToolpathPointsVisible(bool showToolpathPoints);
    void SetPlaybackState(string mode, double stepDurationMs);
    void SetTheme(bool isDark);
    void ResetCamera();
    void SetCameraPreset(string preset);
    void Orbit(float dx, float dy);
    void Pan(float dx, float dy);
    void Zoom(float delta);
    void Render(int framebuffer, float width, float height);
}
