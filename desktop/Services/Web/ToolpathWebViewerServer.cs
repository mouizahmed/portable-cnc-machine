using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.Services.Web;

public sealed class ToolpathWebViewerServer
{
    private static readonly Lazy<ToolpathWebViewerServer> LazyInstance = new(() => new ToolpathWebViewerServer());
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    private readonly TcpListener _listener;
    private readonly CancellationTokenSource _cancellation = new();
    private readonly string _assetRoot;
    private readonly object _gate = new();

    private int _sceneVersion;
    private int _reportedKeyframeIndex = -1;
    private bool _reportedPlaybackDone;
    private ToolpathViewerState _state = new(
        0,
        0,
        0,
        0,
        0,
        "Iso",
        0,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        true,
        false,
        "stopped",
        0);
    private string _sceneJson = "{\"sceneVersion\":0,\"hasScene\":false}";
    private string _stateJson = "{\"sceneVersion\":0,\"currentLine\":0,\"cameraPreset\":\"Iso\",\"resetViewToken\":0}";

    public static ToolpathWebViewerServer Instance => LazyInstance.Value;

    public string ViewerUrl { get; }

    public int ReportedKeyframeIndex
    {
        get { lock (_gate) { return _reportedKeyframeIndex; } }
    }

    public bool ReportedPlaybackDone
    {
        get { lock (_gate) { return _reportedPlaybackDone; } }
    }

    public void ClearReportedPosition()
    {
        lock (_gate)
        {
            _reportedKeyframeIndex = -1;
            _reportedPlaybackDone = false;
        }
    }

    private ToolpathWebViewerServer()
    {
        _assetRoot = Path.Combine(AppContext.BaseDirectory, "Assets", "WebView", "Toolpath3D");

        _listener = new TcpListener(IPAddress.Loopback, 0);
        _listener.Start();
        int port = ((IPEndPoint)_listener.LocalEndpoint).Port;
        ViewerUrl = $"http://127.0.0.1:{port}/index.html";

        _ = Task.Run(() => AcceptLoopAsync(_cancellation.Token));
    }

    public int CurrentSceneVersion
    {
        get
        {
            lock (_gate)
            {
                return _sceneVersion;
            }
        }
    }

    public string SceneJson
    {
        get
        {
            lock (_gate)
            {
                return _sceneJson;
            }
        }
    }

    public string StateJson
    {
        get
        {
            lock (_gate)
            {
                return _stateJson;
            }
        }
    }

    public void UpdateScene(GCodeDocument? document)
    {
        int sceneVersion;
        lock (_gate)
        {
            _sceneVersion++;
            sceneVersion = _sceneVersion;
        }

        ToolpathScenePayload payload = document == null || !document.HasGeometry
            ? ToolpathScenePayload.Empty(sceneVersion)
            : ToolpathScenePayload.FromDocument(document, sceneVersion);

        string json = JsonSerializer.Serialize(payload, JsonOptions);
        lock (_gate)
        {
            _sceneJson = json;
            _state = _state with { SceneVersion = _sceneVersion };
            _stateJson = JsonSerializer.Serialize(_state, JsonOptions);
        }
    }

    public void UpdateState(ToolpathViewerState state)
    {
        lock (_gate)
        {
            _state = state with { SceneVersion = _sceneVersion };
            _stateJson = JsonSerializer.Serialize(_state, JsonOptions);
        }
    }

    private async Task AcceptLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            TcpClient? client = null;

            try
            {
                client = await _listener.AcceptTcpClientAsync(cancellationToken);
                _ = Task.Run(() => HandleClientAsync(client, cancellationToken), cancellationToken);
            }
            catch (OperationCanceledException)
            {
                client?.Dispose();
                break;
            }
            catch
            {
                client?.Dispose();
            }
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        using (client)
        await using (NetworkStream stream = client.GetStream())
        using (var reader = new StreamReader(stream, Encoding.ASCII, false, 4096, leaveOpen: true))
        {
            string? requestLine = await reader.ReadLineAsync(cancellationToken);
            if (string.IsNullOrWhiteSpace(requestLine))
            {
                return;
            }

            string path = "/";
            string[] parts = requestLine.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length >= 2)
            {
                path = parts[1];
            }

            while (!string.IsNullOrEmpty(await reader.ReadLineAsync(cancellationToken)))
            {
            }

            string route = NormalizeRoute(path);
            if (route == "/state")
            {
                ProcessPlaybackReport(path);
                await WriteJsonAsync(stream, GetStateJson(), cancellationToken);
                return;
            }

            if (route == "/scene")
            {
                await WriteJsonAsync(stream, GetSceneJson(), cancellationToken);
                return;
            }

            string? assetPath = ResolveAssetPath(route);
            if (assetPath == null || !File.Exists(assetPath))
            {
                await WriteResponseAsync(stream, "404 Not Found", "text/plain; charset=utf-8", Encoding.UTF8.GetBytes("Not Found"), cancellationToken);
                return;
            }

            byte[] content = await File.ReadAllBytesAsync(assetPath, cancellationToken);
            await WriteResponseAsync(stream, "200 OK", GetContentType(assetPath), content, cancellationToken);
        }
    }

    private string GetSceneJson()
    {
        lock (_gate)
        {
            return _sceneJson;
        }
    }

    private string GetStateJson()
    {
        lock (_gate)
        {
            return _stateJson;
        }
    }

    private string? ResolveAssetPath(string route)
    {
        string relative = route == "/" ? "index.html" : route.TrimStart('/');
        relative = relative.Replace('/', Path.DirectorySeparatorChar);
        string candidate = Path.GetFullPath(Path.Combine(_assetRoot, relative));
        string rootedAssetPath = Path.TrimEndingDirectorySeparator(_assetRoot) + Path.DirectorySeparatorChar;
        return candidate.StartsWith(rootedAssetPath, StringComparison.OrdinalIgnoreCase) ? candidate : null;
    }

    private void ProcessPlaybackReport(string fullPath)
    {
        int qIdx = fullPath.IndexOf('?');
        if (qIdx < 0) return;
        int? kf = null;
        bool? done = null;
        foreach (string param in fullPath.AsSpan(qIdx + 1).ToString().Split('&'))
        {
            int eqIdx = param.IndexOf('=');
            if (eqIdx <= 0) continue;
            string key = param[..eqIdx];
            string val = param[(eqIdx + 1)..];
            if (key == "kf" && int.TryParse(val, out int kfVal)) kf = kfVal;
            else if (key == "done") done = val == "1";
        }
        if (kf.HasValue)
        {
            lock (_gate)
            {
                _reportedKeyframeIndex = kf.Value;
                if (done.HasValue) _reportedPlaybackDone = done.Value;
            }
        }
    }

    private static string NormalizeRoute(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return "/";
        }

        string withoutQuery = path.Split('?', '#')[0];
        return string.IsNullOrWhiteSpace(withoutQuery) ? "/" : withoutQuery;
    }

    private static string GetContentType(string assetPath)
        => Path.GetExtension(assetPath).ToLowerInvariant() switch
        {
            ".html" => "text/html; charset=utf-8",
            ".js" => "text/javascript; charset=utf-8",
            ".css" => "text/css; charset=utf-8",
            ".json" => "application/json; charset=utf-8",
            _ => "application/octet-stream"
        };

    private static async Task WriteJsonAsync(NetworkStream stream, string json, CancellationToken cancellationToken)
        => await WriteResponseAsync(stream, "200 OK", "application/json; charset=utf-8", Encoding.UTF8.GetBytes(json), cancellationToken);

    private static async Task WriteResponseAsync(
        NetworkStream stream,
        string status,
        string contentType,
        byte[] content,
        CancellationToken cancellationToken)
    {
        string header = string.Format(
            CultureInfo.InvariantCulture,
            "HTTP/1.1 {0}\r\nContent-Type: {1}\r\nContent-Length: {2}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
            status,
            contentType,
            content.Length);
        byte[] headerBytes = Encoding.ASCII.GetBytes(header);
        await stream.WriteAsync(headerBytes, cancellationToken);
        await stream.WriteAsync(content, cancellationToken);
        await stream.FlushAsync(cancellationToken);
    }

    private sealed record ToolpathScenePayload(
        int SceneVersion,
        bool HasScene,
        ToolpathBoundsPayload? Bounds,
        double StockTop,
        IReadOnlyList<ToolpathSegmentPayload> Segments)
    {
        public static ToolpathScenePayload Empty(int sceneVersion)
            => new(sceneVersion, false, null, 0, Array.Empty<ToolpathSegmentPayload>());

        public static ToolpathScenePayload FromDocument(GCodeDocument document, int sceneVersion)
        {
            var segments = new ToolpathSegmentPayload[document.Segments.Length];
            for (int i = 0; i < document.Segments.Length; i++)
            {
                ToolpathSegment segment = document.Segments[i];
                segments[i] = new ToolpathSegmentPayload(
                    new[] { segment.Start.X, segment.Start.Y, segment.Start.Z },
                    new[] { segment.End.X, segment.End.Y, segment.End.Z },
                    (int)segment.Motion,
                    segment.SourceLine,
                    segment.IsRapid,
                    segment.IsArc,
                    segment.IsPlungeOrRetract,
                    segment.AverageZ);
            }

            return new ToolpathScenePayload(
                sceneVersion,
                true,
                new ToolpathBoundsPayload(document.MinX, document.MaxX, document.MinY, document.MaxY, document.MinZ, document.MaxZ),
                Math.Max(document.MaxZ, 0),
                segments);
        }
    }

    private sealed record ToolpathBoundsPayload(
        double MinX,
        double MaxX,
        double MinY,
        double MaxY,
        double MinZ,
        double MaxZ);

    private sealed record ToolpathSegmentPayload(
        double[] Start,
        double[] End,
        int Motion,
        int SourceLine,
        bool IsRapid,
        bool IsArc,
        bool IsPlungeOrRetract,
        double AverageZ);
}

public sealed record ToolpathViewerState(
    int SceneVersion,
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
    double PreviewPlaybackStepDurationMs);
