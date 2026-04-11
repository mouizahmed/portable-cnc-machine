using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using PortableCncApp.Services.GCode;
using PortableCncApp.Services.Web;
using WebViewControl;

namespace PortableCncApp.Controls;

public sealed class WebToolpathView : UserControl
{
    public static readonly StyledProperty<GCodeDocument?> DocumentProperty =
        AvaloniaProperty.Register<WebToolpathView, GCodeDocument?>(nameof(Document));

    public static readonly StyledProperty<int> CurrentLineProperty =
        AvaloniaProperty.Register<WebToolpathView, int>(nameof(CurrentLine));

    public static readonly StyledProperty<double> PositionXProperty =
        AvaloniaProperty.Register<WebToolpathView, double>(nameof(PositionX));

    public static readonly StyledProperty<double> PositionYProperty =
        AvaloniaProperty.Register<WebToolpathView, double>(nameof(PositionY));

    public static readonly StyledProperty<double> PositionZProperty =
        AvaloniaProperty.Register<WebToolpathView, double>(nameof(PositionZ));

    public static readonly StyledProperty<string> CameraPresetProperty =
        AvaloniaProperty.Register<WebToolpathView, string>(nameof(CameraPreset), "Iso");

    public static readonly StyledProperty<int> ResetViewTokenProperty =
        AvaloniaProperty.Register<WebToolpathView, int>(nameof(ResetViewToken));

    public static readonly StyledProperty<bool> ShowRapidsProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowRapids), true);

    public static readonly StyledProperty<bool> ShowCutsProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowCuts), true);

    public static readonly StyledProperty<bool> ShowArcsProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowArcs), true);

    public static readonly StyledProperty<bool> ShowPlungesProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowPlunges), true);

    public static readonly StyledProperty<bool> ShowCompletedPathProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowCompletedPath), true);

    public static readonly StyledProperty<bool> ShowRemainingPathProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowRemainingPath), true);

    public static readonly StyledProperty<bool> ShowStockBoxProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowStockBox), false);

    public static readonly StyledProperty<bool> ShowGridProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowGrid), true);

    public static readonly StyledProperty<bool> ShowToolpathPointsProperty =
        AvaloniaProperty.Register<WebToolpathView, bool>(nameof(ShowToolpathPoints), false);

    public static readonly StyledProperty<string> PreviewPlaybackModeProperty =
        AvaloniaProperty.Register<WebToolpathView, string>(nameof(PreviewPlaybackMode), "stopped");

    public static readonly StyledProperty<double> PreviewPlaybackStepDurationMsProperty =
        AvaloniaProperty.Register<WebToolpathView, double>(nameof(PreviewPlaybackStepDurationMs), 0d);

    private readonly WebView _webView;
    private readonly DispatcherTimer _bridgeTimer;
    private int _lastPushedResetToken = int.MinValue;
    private int _lastPushedSceneVersion = int.MinValue;
    private string? _lastPushedStateJson;

    public WebToolpathView()
    {
        _webView = new WebView
        {
            Address = ToolpathWebViewerServer.Instance.ViewerUrl
        };

        Content = _webView;
        _bridgeTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(300)
        };
        _bridgeTimer.Tick += (_, _) => PushBridgeState();
        _bridgeTimer.Start();
        UpdateViewerState();
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

    public bool ShowGrid
    {
        get => GetValue(ShowGridProperty);
        set => SetValue(ShowGridProperty, value);
    }

    public bool ShowToolpathPoints
    {
        get => GetValue(ShowToolpathPointsProperty);
        set => SetValue(ShowToolpathPointsProperty, value);
    }

    public string PreviewPlaybackMode
    {
        get => GetValue(PreviewPlaybackModeProperty);
        set => SetValue(PreviewPlaybackModeProperty, value);
    }

    public double PreviewPlaybackStepDurationMs
    {
        get => GetValue(PreviewPlaybackStepDurationMsProperty);
        set => SetValue(PreviewPlaybackStepDurationMsProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        UpdateViewerState();
    }

    private void UpdateViewerState()
    {
        ToolpathWebViewerServer.Instance.UpdateState(new ToolpathViewerState(
            0,
            CurrentLine,
            PositionX,
            PositionY,
            PositionZ,
            CameraPreset,
            ResetViewToken,
            ShowRapids,
            ShowCuts,
            ShowArcs,
            ShowPlunges,
            ShowCompletedPath,
            ShowRemainingPath,
            ShowStockBox,
            ShowGrid,
            ShowToolpathPoints,
            PreviewPlaybackMode,
            PreviewPlaybackStepDurationMs));

        PushBridgeState();
    }

    private void PushBridgeState()
    {
        var server = ToolpathWebViewerServer.Instance;
        int currentSceneVersion = server.CurrentSceneVersion;
        string sceneJson = server.SceneJson;
        string stateJson = server.StateJson;

        if (currentSceneVersion != _lastPushedSceneVersion)
        {
            ExecuteBridgeCall($"window.setToolpathScene && window.setToolpathScene({sceneJson});");
            _lastPushedSceneVersion = currentSceneVersion;
        }

        if (!string.Equals(_lastPushedStateJson, stateJson, StringComparison.Ordinal))
        {
            ExecuteBridgeCall($"window.setToolpathState && window.setToolpathState({stateJson});");
            _lastPushedStateJson = stateJson;
        }
    }

    private void ExecuteBridgeCall(string script)
    {
        try
        {
            var method = _webView.GetType().GetMethod("EvaluateJavaScript", new[] { typeof(string) });
            method?.Invoke(_webView, new object[] { script });
        }
        catch
        {
        }
    }
}
