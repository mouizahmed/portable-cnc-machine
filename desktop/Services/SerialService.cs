using System;
using System.IO.Ports;
using System.Text;
using Avalonia.Threading;

namespace PortableCncApp.Services;

/// <summary>
/// Manages the USB CDC serial connection to the Pico 2W.
/// The Pico relays all commands transparently to the Teensy 4.1 (GRBL).
/// LineReceived is always fired on the UI thread.
/// </summary>
public sealed class SerialService : IDisposable
{
    private SerialPort? _port;
    private readonly StringBuilder _lineBuffer = new();

    public bool IsConnected => _port?.IsOpen == true;
    public string? PortName => _port?.PortName;

    /// <summary>Fired on the UI thread for every complete line received from the device.</summary>
    public event Action<string>? LineReceived;

    /// <summary>Fired on the UI thread when a serial error occurs.</summary>
    public event Action<string>? ErrorOccurred;

    public bool Connect(string portName, int baudRate)
    {
        if (IsConnected) Disconnect();

        try
        {
            _port = new SerialPort(portName, baudRate)
            {
                Encoding = Encoding.ASCII,
                DtrEnable = true,
                RtsEnable = true,
                WriteTimeout = 1000
            };

            _port.DataReceived += OnDataReceived;
            _port.Open();

            return true;
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
            CleanupPort();
            return false;
        }
    }

    public void Disconnect()
    {
        CleanupPort();
    }

    /// <summary>Send a GRBL command. A newline is appended automatically.</summary>
    public void SendCommand(string command)
    {
        if (!IsConnected) return;
        try
        {
            _port!.Write(command + "\n");
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }
    }

    /// <summary>Send a single real-time byte (e.g. '?' 0x18 '!' '~') without a newline.</summary>
    public void SendRealtime(byte b)
    {
        if (!IsConnected) return;
        try
        {
            _port!.BaseStream.WriteByte(b);
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            var data = _port!.ReadExisting();
            foreach (var ch in data)
            {
                if (ch == '\n')
                {
                    var line = _lineBuffer.ToString().TrimEnd('\r');
                    _lineBuffer.Clear();
                    if (line.Length > 0)
                    {
                        var captured = line;
                        Dispatcher.UIThread.Post(() => LineReceived?.Invoke(captured));
                    }
                }
                else
                {
                    _lineBuffer.Append(ch);
                }
            }
        }
        catch { }
    }

    private void CleanupPort()
    {
        if (_port != null)
        {
            _port.DataReceived -= OnDataReceived;
            try { _port.Close(); } catch { }
            _port.Dispose();
            _port = null;
        }
        _lineBuffer.Clear();
    }

    public void Dispose() => CleanupPort();
}
