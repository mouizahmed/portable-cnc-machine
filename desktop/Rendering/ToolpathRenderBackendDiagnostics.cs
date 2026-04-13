namespace PortableCncApp.Rendering;

internal sealed record ToolpathRenderBackendDiagnostics(
    string BackendName,
    string ApiVersion,
    string ShadingLanguageVersion,
    string Vendor,
    string Renderer)
{
    public static ToolpathRenderBackendDiagnostics Empty { get; } =
        new("unknown", "unknown", "unknown", "unknown", "unknown");

    public string ToLogString()
        => $"{BackendName} | API={ApiVersion} | GLSL={ShadingLanguageVersion} | Vendor={Vendor} | Renderer={Renderer}";
}
