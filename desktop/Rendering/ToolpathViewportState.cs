namespace PortableCncApp.Rendering;

/// <summary>
/// Renderer-neutral viewport state for the native toolpath viewer.
/// </summary>
public sealed record ToolpathViewportState(
    int CurrentLine,
    double PositionX,
    double PositionY,
    double PositionZ,
    string CameraPreset,
    int ResetViewToken,
    bool ShowRapids,
    bool ShowCuts,
    bool ShowArcs,
    bool ShowPlunges,
    bool ShowCompletedPath,
    bool ShowRemainingPath,
    bool ShowStockBox,
    bool ShowGrid,
    bool ShowToolpathPoints,
    string PreviewPlaybackMode,
    double PreviewPlaybackStepDurationMs,
    string ThemeMode);
