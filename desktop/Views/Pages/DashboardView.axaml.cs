using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using PortableCncApp.Rendering;

namespace PortableCncApp.Views.Pages;

public partial class DashboardView : UserControl
{
    private bool _isOrbiting;
    private bool _isPanning;
    private Point _lastPointerPos;

    public DashboardView()
    {
        InitializeComponent();
    }

    private void OnViewerHostPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (ViewerHost is null || Viewer is null || Viewer.Document is null)
        {
            return;
        }

        var point = e.GetCurrentPoint(ViewerHost);
        _lastPointerPos = point.Position;
        ViewerHost.Focus();

        if (point.Properties.IsLeftButtonPressed)
        {
            _isPanning = true;
            _isOrbiting = false;
            e.Pointer.Capture(ViewerHost);
            e.Handled = true;
        }
        else if (point.Properties.IsRightButtonPressed || point.Properties.IsMiddleButtonPressed)
        {
            _isOrbiting = true;
            _isPanning = false;
            e.Pointer.Capture(ViewerHost);
            e.Handled = true;
        }
    }

    private void OnViewerHostPointerMoved(object? sender, PointerEventArgs e)
    {
        if (Viewer is null || Viewer.Document is null || (!_isOrbiting && !_isPanning))
        {
            return;
        }

        var point = e.GetCurrentPoint(ViewerHost);
        if (!point.Properties.IsLeftButtonPressed && !point.Properties.IsRightButtonPressed && !point.Properties.IsMiddleButtonPressed)
        {
            StopViewerPointerInteraction(e.Pointer);
            return;
        }

        var delta = point.Position - _lastPointerPos;
        _lastPointerPos = point.Position;

        if (_isOrbiting)
        {
            Viewer.OrbitBy((float)delta.X, (float)delta.Y);
        }
        else if (_isPanning)
        {
            Viewer.PanBy((float)delta.X, (float)delta.Y);
        }

        e.Handled = true;
    }

    private void OnViewerHostPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        StopViewerPointerInteraction(e.Pointer);
    }

    private void OnViewerHostPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        StopViewerPointerInteraction(e.Pointer);
    }

    private void OnViewerHostPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (Viewer is null || Viewer.Document is null)
        {
            return;
        }

        Viewer.ZoomBy((float)-e.Delta.Y * 0.12f);
        e.Handled = true;
    }

    private void StopViewerPointerInteraction(IPointer pointer)
    {
        _isOrbiting = false;
        _isPanning = false;
        if (pointer.Captured == ViewerHost)
        {
            pointer.Capture(null);
        }
    }
}
