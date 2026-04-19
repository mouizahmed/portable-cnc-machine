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
    private const byte TransferFrameEscape = 0x7D;
    private const byte TransferFrameEscapeXor = 0x20;
    private const int TransferFrameHeaderSize = 9;
    private const int MaxTransferPayloadSize = 256;

    public readonly record struct TransferFrame(byte FrameType, byte TransferId, byte Flags, uint Sequence, byte[] Payload);

    private enum ReceiveMode
    {
        Idle,
        Line,
        FramePacket,
        FrameEscape
    }

    private static readonly TimeSpan StartupErrorGracePeriod = TimeSpan.FromSeconds(2);
    private readonly object _readSync = new();
    private readonly object _writeSync = new();
    private SerialPort? _port;
    private readonly StringBuilder _lineBuffer = new();
    private readonly byte[] _framePacket = new byte[TransferFrameHeaderSize + MaxTransferPayloadSize + sizeof(uint) + 4];
    private readonly byte[] _decodedFrame = new byte[TransferFrameHeaderSize + MaxTransferPayloadSize + sizeof(uint)];
    private DateTime _connectedAtUtc = DateTime.MinValue;
    private ReceiveMode _receiveMode = ReceiveMode.Idle;
    private int _framePacketBytesRead;

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
            byte[] encoded = new byte[TransferFrameHeaderSize + payload.Length + sizeof(uint) + 4];
            int rawOffset = 0;
            buffer[rawOffset++] = frameType;
            buffer[rawOffset++] = transferId;
            buffer[rawOffset++] = 0; // flags/reserved
            WriteUInt32LittleEndian(buffer, ref rawOffset, sequence);
            WriteUInt16LittleEndian(buffer, ref rawOffset, (ushort)payload.Length);
            payload.CopyTo(buffer.AsSpan(rawOffset, payload.Length));
            rawOffset += payload.Length;
            uint crc = ComputeCrc32(buffer.AsSpan(0, TransferFrameHeaderSize + payload.Length));
            WriteUInt32LittleEndian(buffer, ref rawOffset, crc);

            int encodedLength = CobsEncode(buffer.AsSpan(0, rawOffset), encoded);
            byte[] wire = new byte[1 + (encodedLength * 2) + 1];
            int offset = 0;
            wire[offset++] = TransferFrameMarker;
            for (int i = 0; i < encodedLength; i++)
            {
                if (ShouldEscapeFrameByte(encoded[i]))
                {
                    wire[offset++] = TransferFrameEscape;
                    wire[offset++] = (byte)(encoded[i] ^ TransferFrameEscapeXor);
                    continue;
                }

                wire[offset++] = encoded[i];
            }
            wire[offset++] = TransferFrameMarker;

            lock (_writeSync)
            {
                _port!.BaseStream.Write(wire, 0, offset);
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
                    _receiveMode = ReceiveMode.FramePacket;
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

            case ReceiveMode.FramePacket:
                if (value == TransferFrameMarker)
                {
                    if (_framePacketBytesRead > 0)
                    {
                        CompleteFramePacket();
                    }
                    else
                    {
                        ResetFrameParse();
                    }
                    return;
                }
                if (value == TransferFrameEscape)
                {
                    _receiveMode = ReceiveMode.FrameEscape;
                    return;
                }
                AppendFramePacketByte(value);
                break;

            case ReceiveMode.FrameEscape:
                AppendFramePacketByte((byte)(value ^ TransferFrameEscapeXor));
                if (_receiveMode == ReceiveMode.FrameEscape)
                {
                    _receiveMode = ReceiveMode.FramePacket;
                }
                break;
        }
    }

    private void AppendFramePacketByte(byte value)
    {
        if (_framePacketBytesRead >= _framePacket.Length)
        {
            ResetReceiveState();
            return;
        }

        _framePacket[_framePacketBytesRead++] = value;
    }

    private void CompleteFramePacket()
    {
        int decodedLength = CobsDecode(_framePacket.AsSpan(0, _framePacketBytesRead), _decodedFrame);
        if (decodedLength < TransferFrameHeaderSize + sizeof(uint))
        {
            ResetReceiveState();
            return;
        }

        ushort payloadLength = ReadUInt16LittleEndian(_decodedFrame, 7);
        if (payloadLength > MaxTransferPayloadSize ||
            decodedLength != TransferFrameHeaderSize + payloadLength + sizeof(uint))
        {
            ResetReceiveState();
            return;
        }

        uint expectedCrc = ReadUInt32LittleEndian(_decodedFrame, TransferFrameHeaderSize + payloadLength);

        if (ComputeCrc32(_decodedFrame.AsSpan(0, TransferFrameHeaderSize + payloadLength)) == expectedCrc)
        {
            byte[] payload = new byte[payloadLength];
            if (payloadLength > 0)
            {
                Buffer.BlockCopy(_decodedFrame, TransferFrameHeaderSize, payload, 0, payloadLength);
            }

            var frame = new TransferFrame(
                FrameType: _decodedFrame[0],
                TransferId: _decodedFrame[1],
                Flags: _decodedFrame[2],
                Sequence: ReadUInt32LittleEndian(_decodedFrame, 3),
                Payload: payload);

            Dispatcher.UIThread.Post(() => FrameReceived?.Invoke(frame));
        }

        ResetReceiveState();
    }

    private void ResetFrameParse()
    {
        _framePacketBytesRead = 0;
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

    private static int CobsEncode(ReadOnlySpan<byte> input, Span<byte> output)
    {
        int read = 0;
        int write = 1;
        int codeIndex = 0;
        byte code = 1;

        while (read < input.Length)
        {
            if (input[read] == 0)
            {
                output[codeIndex] = code;
                code = 1;
                codeIndex = write++;
                read++;
                continue;
            }

            output[write++] = input[read++];
            code++;
            if (code == 0xFF)
            {
                output[codeIndex] = code;
                code = 1;
                codeIndex = write++;
            }
        }

        output[codeIndex] = code;
        return write;
    }

    private static bool ShouldEscapeFrameByte(byte value)
        => value is TransferFrameMarker or TransferFrameEscape or (byte)'\r' or (byte)'\n';

    private static int CobsDecode(ReadOnlySpan<byte> input, Span<byte> output)
    {
        int read = 0;
        int write = 0;

        while (read < input.Length)
        {
            byte code = input[read++];
            if (code == 0)
                return -1;

            int copy = code - 1;
            if (read + copy > input.Length || write + copy > output.Length)
                return -1;

            input.Slice(read, copy).CopyTo(output.Slice(write));
            read += copy;
            write += copy;

            if (code < 0xFF && read < input.Length)
            {
                if (write >= output.Length)
                    return -1;
                output[write++] = 0;
            }
        }

        return write;
    }
}
