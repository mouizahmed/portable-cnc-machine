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
    private const byte TransferFrameMarker = 0x7E;
    private const int TransferFrameHeaderSize = 9;
    private const int MaxTransferPayloadSize = 256;

    public readonly record struct TransferFrame(byte FrameType, byte TransferId, byte Flags, uint Sequence, byte[] Payload);

    private enum ReceiveMode
    {
        Idle,
        Line,
        FrameHeader,
        FrameBody
    }

    private static readonly TimeSpan StartupErrorGracePeriod = TimeSpan.FromSeconds(2);
    private readonly object _readSync = new();
    private readonly object _writeSync = new();
    private SerialPort? _port;
    private readonly StringBuilder _lineBuffer = new();
    private readonly byte[] _frameHeader = new byte[TransferFrameHeaderSize];
    private readonly byte[] _frameBody = new byte[MaxTransferPayloadSize + sizeof(uint)];
    private DateTime _connectedAtUtc = DateTime.MinValue;
    private ReceiveMode _receiveMode = ReceiveMode.Idle;
    private int _frameHeaderBytesRead;
    private int _frameBodyBytesRead;
    private int _frameExpectedBodyBytes;
    private ushort _framePayloadLength;

    public bool IsConnected => _port?.IsOpen == true;
    public string? PortName => _port?.PortName;

    /// <summary>Fired on the UI thread for every complete line received from the device.</summary>
    public event Action<string>? LineReceived;

    /// <summary>Fired on the UI thread for each validated transfer frame received from the device.</summary>
    public event Action<TransferFrame>? FrameReceived;

    /// <summary>Fired on the UI thread when a serial error occurs.</summary>
    public event Action<string>? ErrorOccurred;

    public bool Connect(string portName)
    {
        if (IsConnected) Disconnect();

        try
        {
            _port = new SerialPort(portName, 115200)
            {
                Encoding = Encoding.ASCII,
                // Pico USB CDC generally expects DTR to be asserted for normal traffic,
                // but forcing RTS high has proven destabilizing on reopen.
                DtrEnable = true,
                RtsEnable = false,
                WriteTimeout = 5000
            };

            _port.DataReceived += OnDataReceived;
            _port.ErrorReceived += OnSerialErrorReceived;
            _port.PinChanged += OnSerialPinChanged;
            _port.Open();
            _port.DiscardInBuffer();
            _port.DiscardOutBuffer();
            _connectedAtUtc = DateTime.UtcNow;

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
            lock (_writeSync)
            {
                _port!.Write(command + "\n");
            }
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }
    }

    public void SendFrame(byte frameType, byte transferId, uint sequence, ReadOnlySpan<byte> payload)
    {
        if (!IsConnected) return;
        if (payload.Length > MaxTransferPayloadSize)
            throw new ArgumentOutOfRangeException(nameof(payload), $"Payload exceeds {MaxTransferPayloadSize} bytes.");

        try
        {
            byte[] buffer = new byte[1 + TransferFrameHeaderSize + payload.Length + sizeof(uint)];
            int offset = 0;
            buffer[offset++] = TransferFrameMarker;
            buffer[offset++] = frameType;
            buffer[offset++] = transferId;
            buffer[offset++] = 0; // flags/reserved
            WriteUInt32LittleEndian(buffer, ref offset, sequence);
            WriteUInt16LittleEndian(buffer, ref offset, (ushort)payload.Length);
            payload.CopyTo(buffer.AsSpan(offset, payload.Length));
            offset += payload.Length;
            uint crc = ComputeCrc32(buffer.AsSpan(1, TransferFrameHeaderSize + payload.Length));
            WriteUInt32LittleEndian(buffer, ref offset, crc);

            lock (_writeSync)
            {
                _port!.BaseStream.Write(buffer, 0, buffer.Length);
                _port.BaseStream.Flush();
            }
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
            lock (_writeSync)
            {
                _port!.BaseStream.WriteByte(b);
            }
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
            lock (_readSync)
            {
                int bytesToRead = _port!.BytesToRead;
                if (bytesToRead <= 0)
                    return;

                byte[] buffer = new byte[bytesToRead];
                int read = _port.Read(buffer, 0, buffer.Length);
                for (int index = 0; index < read; index++)
                {
                    ProcessIncomingByte(buffer[index]);
                }
            }
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }
    }

    private void ProcessIncomingByte(byte value)
    {
        switch (_receiveMode)
        {
            case ReceiveMode.Idle:
                if (value == TransferFrameMarker)
                {
                    ResetFrameParse();
                    _receiveMode = ReceiveMode.FrameHeader;
                }
                else if (value == (byte)'@')
                {
                    _lineBuffer.Clear();
                    _lineBuffer.Append('@');
                    _receiveMode = ReceiveMode.Line;
                }
                break;

            case ReceiveMode.Line:
                if (value == (byte)'\r')
                {
                    return;
                }
                if (value == (byte)'\n')
                {
                    var line = _lineBuffer.ToString();
                    _lineBuffer.Clear();
                    _receiveMode = ReceiveMode.Idle;
                    if (line.Length > 0)
                    {
                        Dispatcher.UIThread.Post(() => LineReceived?.Invoke(line));
                    }
                    return;
                }

                _lineBuffer.Append((char)value);
                break;

            case ReceiveMode.FrameHeader:
                _frameHeader[_frameHeaderBytesRead++] = value;
                if (_frameHeaderBytesRead < TransferFrameHeaderSize)
                    return;

                _framePayloadLength = ReadUInt16LittleEndian(_frameHeader, 7);
                if (_framePayloadLength > MaxTransferPayloadSize)
                {
                    ResetReceiveState();
                    return;
                }

                _frameExpectedBodyBytes = _framePayloadLength + sizeof(uint);
                _frameBodyBytesRead = 0;
                _receiveMode = ReceiveMode.FrameBody;
                break;

            case ReceiveMode.FrameBody:
                if (_frameBodyBytesRead >= _frameBody.Length)
                {
                    ResetReceiveState();
                    return;
                }

                _frameBody[_frameBodyBytesRead++] = value;
                if (_frameBodyBytesRead < _frameExpectedBodyBytes)
                    return;

                CompleteFrame();
                break;
        }
    }

    private void CompleteFrame()
    {
        uint expectedCrc = ReadUInt32LittleEndian(_frameBody, _framePayloadLength);

        byte[] crcBuffer = new byte[TransferFrameHeaderSize + _framePayloadLength];
        Buffer.BlockCopy(_frameHeader, 0, crcBuffer, 0, TransferFrameHeaderSize);
        if (_framePayloadLength > 0)
        {
            Buffer.BlockCopy(_frameBody, 0, crcBuffer, TransferFrameHeaderSize, _framePayloadLength);
        }

        if (ComputeCrc32(crcBuffer) == expectedCrc)
        {
            byte[] payload = new byte[_framePayloadLength];
            if (_framePayloadLength > 0)
            {
                Buffer.BlockCopy(_frameBody, 0, payload, 0, _framePayloadLength);
            }

            var frame = new TransferFrame(
                FrameType: _frameHeader[0],
                TransferId: _frameHeader[1],
                Flags: _frameHeader[2],
                Sequence: ReadUInt32LittleEndian(_frameHeader, 3),
                Payload: payload);

            Dispatcher.UIThread.Post(() => FrameReceived?.Invoke(frame));
        }

        ResetReceiveState();
    }

    private void ResetFrameParse()
    {
        _frameHeaderBytesRead = 0;
        _frameBodyBytesRead = 0;
        _frameExpectedBodyBytes = 0;
        _framePayloadLength = 0;
    }

    private void ResetReceiveState()
    {
        _receiveMode = ReceiveMode.Idle;
        _lineBuffer.Clear();
        ResetFrameParse();
    }

    public void NotifyPortRemoved(string portName)
    {
        if (_port == null || !IsConnected)
            return;

        if (IsWithinStartupErrorGrace())
            return;

        if (!string.Equals(_port.PortName, portName, StringComparison.OrdinalIgnoreCase))
            return;

        Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke($"Serial device removed: {portName}"));
    }

    private void CleanupPort()
    {
        if (_port != null)
        {
            _port.DataReceived -= OnDataReceived;
            _port.ErrorReceived -= OnSerialErrorReceived;
            _port.PinChanged -= OnSerialPinChanged;
            try { _port.Close(); } catch { }
            _port.Dispose();
            _port = null;
        }
        _connectedAtUtc = DateTime.MinValue;
        ResetReceiveState();
    }

    private bool IsWithinStartupErrorGrace()
        => _connectedAtUtc != DateTime.MinValue &&
           DateTime.UtcNow - _connectedAtUtc < StartupErrorGracePeriod;

    private void OnSerialErrorReceived(object sender, SerialErrorReceivedEventArgs e)
    {
        if (IsWithinStartupErrorGrace())
            return;

        Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke($"Serial error: {e.EventType}"));
    }

    private void OnSerialPinChanged(object sender, SerialPinChangedEventArgs e)
    {
        if (IsWithinStartupErrorGrace())
            return;

        if (e.EventType == SerialPinChange.Break)
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke($"Serial pin change: {e.EventType}"));
    }

    public void Dispose() => CleanupPort();

    private static void WriteUInt16LittleEndian(byte[] buffer, ref int offset, ushort value)
    {
        buffer[offset++] = (byte)(value & 0xFF);
        buffer[offset++] = (byte)(value >> 8);
    }

    private static void WriteUInt32LittleEndian(byte[] buffer, ref int offset, uint value)
    {
        buffer[offset++] = (byte)(value & 0xFF);
        buffer[offset++] = (byte)((value >> 8) & 0xFF);
        buffer[offset++] = (byte)((value >> 16) & 0xFF);
        buffer[offset++] = (byte)((value >> 24) & 0xFF);
    }

    private static ushort ReadUInt16LittleEndian(byte[] buffer, int offset)
        => (ushort)(buffer[offset] | (buffer[offset + 1] << 8));

    private static uint ReadUInt32LittleEndian(byte[] buffer, int offset)
        => (uint)(buffer[offset]
                | (buffer[offset + 1] << 8)
                | (buffer[offset + 2] << 16)
                | (buffer[offset + 3] << 24));

    private static uint ComputeCrc32(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFFu;
        for (int index = 0; index < data.Length; index++)
        {
            crc ^= data[index];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return ~crc;
    }
}
