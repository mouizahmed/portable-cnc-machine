using PortableCncApp.Services.GCode;

namespace PortableCncApp.Rendering;

/// <summary>
/// Renderer-neutral contract for toolpath viewport interaction.
/// Implemented by NativeGlToolpathView and any future renderer.
/// </summary>
public interface IToolpathViewportBridge
{
    void LoadScene(GCodeDocument? document);
    void ApplyState(ToolpathViewportState state);
    void ResetCamera();
    void SetCameraPreset(string preset);
}
