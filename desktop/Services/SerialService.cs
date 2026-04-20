using System;
using System.Diagnostics;
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
    private const int MaxTransferPayloadSize = 4096;
    private static readonly int[] UsbCdcBaudRates = { 12_000_000, 2_000_000, 921_600, 115_200 };
    private const int MaxTransferRawFrameSize = TransferFrameHeaderSize + MaxTransferPayloadSize + sizeof(uint);
    private const int MaxTransferEncodedFrameSize = MaxTransferRawFrameSize + ((MaxTransferRawFrameSize + 253) / 254);
    private static readonly uint[] Crc32Table = BuildCrc32Table();

    public readonly record struct TransferFrame(byte FrameType, byte TransferId, byte Flags, uint Sequence, byte[] Payload);
    public readonly record struct TransferWriteStats(
        long BuildMs,
        long CobsMs,
        long WireMs,
        long WriteMs,
        long MaxWriteMs,
        int Count,
        long Bytes);

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
    private readonly object _transferStatsSync = new();
    private SerialPort? _port;
    private readonly StringBuilder _lineBuffer = new();
    private readonly byte[] _framePacket = new byte[MaxTransferEncodedFrameSize];
    private readonly byte[] _decodedFrame = new byte[MaxTransferRawFrameSize];
    private DateTime _connectedAtUtc = DateTime.MinValue;
    private ReceiveMode _receiveMode = ReceiveMode.Idle;
    private int _framePacketBytesRead;
    private long _transferBuildTicks;
    private long _transferCobsTicks;
    private long _transferWireTicks;
    private long _transferWriteTicks;
    private long _transferMaxWriteTicks;
    private long _transferBytesWritten;
    private int _transferFrameWrites;

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

        Exception? lastError = null;
        foreach (int baudRate in UsbCdcBaudRates)
        {
            try
            {
                _port = new SerialPort(portName, baudRate)
                {
                    Encoding = Encoding.ASCII,
                    Handshake = Handshake.None,
                    // Pico USB CDC generally expects DTR to be asserted for normal traffic,
                    // but forcing RTS high has proven destabilizing on reopen.
                    DtrEnable = true,
                    RtsEnable = false,
                    ReadBufferSize = 262144,
                    WriteBufferSize = 1048576,
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
                lastError = ex;
                CleanupPort();
            }
        }

        if (lastError != null)
        {
            var msg = lastError.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }

        return false;
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
            long stageStart = Stopwatch.GetTimestamp();
            byte[] buffer = new byte[TransferFrameHeaderSize + payload.Length + sizeof(uint)];
            byte[] encoded = new byte[buffer.Length + ((buffer.Length + 253) / 254)];
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
            long buildTicks = Stopwatch.GetTimestamp() - stageStart;

            stageStart = Stopwatch.GetTimestamp();
            int encodedLength = CobsEncode(buffer.AsSpan(0, rawOffset), encoded);
            if (encodedLength <= 0)
                throw new InvalidOperationException("Failed to encode transfer frame.");
            long cobsTicks = Stopwatch.GetTimestamp() - stageStart;

            stageStart = Stopwatch.GetTimestamp();
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
            long wireTicks = Stopwatch.GetTimestamp() - stageStart;

            stageStart = Stopwatch.GetTimestamp();
            lock (_writeSync)
            {
                _port!.Write(wire, 0, offset);
            }
            long writeTicks = Stopwatch.GetTimestamp() - stageStart;

            lock (_transferStatsSync)
            {
                _transferBuildTicks += buildTicks;
                _transferCobsTicks += cobsTicks;
                _transferWireTicks += wireTicks;
                _transferWriteTicks += writeTicks;
                _transferMaxWriteTicks = Math.Max(_transferMaxWriteTicks, writeTicks);
                _transferBytesWritten += offset;
                _transferFrameWrites++;
            }
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
        }
    }

    /// <summary>
    /// Builds one wire frame and appends it to <paramref name="output"/> without writing to the port.
    /// Call <see cref="SendPrebuiltFrames"/> once per window to flush all frames in a single Write().
    /// </summary>
    public void BuildFrameInto(byte frameType, byte transferId, uint sequence,
                               ReadOnlySpan<byte> payload, System.IO.MemoryStream output)
    {
        if (payload.Length > MaxTransferPayloadSize)
            throw new ArgumentOutOfRangeException(nameof(payload));

        long stageStart = Stopwatch.GetTimestamp();
        byte[] rawBuf = new byte[TransferFrameHeaderSize + payload.Length + sizeof(uint)];
        int rawOffset = 0;
        rawBuf[rawOffset++] = frameType;
        rawBuf[rawOffset++] = transferId;
        rawBuf[rawOffset++] = 0;
        WriteUInt32LittleEndian(rawBuf, ref rawOffset, sequence);
        WriteUInt16LittleEndian(rawBuf, ref rawOffset, (ushort)payload.Length);
        payload.CopyTo(rawBuf.AsSpan(rawOffset, payload.Length));
        rawOffset += payload.Length;
        uint crc = ComputeCrc32(rawBuf.AsSpan(0, TransferFrameHeaderSize + payload.Length));
        WriteUInt32LittleEndian(rawBuf, ref rawOffset, crc);
        long buildTicks = Stopwatch.GetTimestamp() - stageStart;

        stageStart = Stopwatch.GetTimestamp();
        byte[] encoded = new byte[rawBuf.Length + ((rawBuf.Length + 253) / 254)];
        int encodedLength = CobsEncode(rawBuf.AsSpan(0, rawOffset), encoded);
        if (encodedLength <= 0)
            throw new InvalidOperationException("Failed to COBS-encode transfer frame.");
        long cobsTicks = Stopwatch.GetTimestamp() - stageStart;

        stageStart = Stopwatch.GetTimestamp();
        output.WriteByte(TransferFrameMarker);
        for (int i = 0; i < encodedLength; i++)
        {
            if (ShouldEscapeFrameByte(encoded[i]))
            {
                output.WriteByte(TransferFrameEscape);
                output.WriteByte((byte)(encoded[i] ^ TransferFrameEscapeXor));
            }
            else
            {
                output.WriteByte(encoded[i]);
            }
        }
        output.WriteByte(TransferFrameMarker);
        long wireTicks = Stopwatch.GetTimestamp() - stageStart;

        lock (_transferStatsSync)
        {
            _transferBuildTicks += buildTicks;
            _transferCobsTicks += cobsTicks;
            _transferWireTicks += wireTicks;
        }
    }

    /// <summary>
    /// Writes all data accumulated in <paramref name="frames"/> to the port in a single Write() call.
    /// This is the key win: N frames → 1 syscall instead of N, eliminating per-call USB CDC latency.
    /// </summary>
    public void SendPrebuiltFrames(System.IO.MemoryStream frames, int frameCount)
    {
        if (!IsConnected || frames.Length == 0) return;
        int length = (int)frames.Length;
        byte[] buffer = frames.GetBuffer();
        long stageStart = Stopwatch.GetTimestamp();
        try
        {
            lock (_writeSync)
            {
                _port!.BaseStream.Write(buffer, 0, length);
            }
        }
        catch (Exception ex)
        {
            var msg = ex.Message;
            Dispatcher.UIThread.Post(() => ErrorOccurred?.Invoke(msg));
            return;
        }
        long writeTicks = Stopwatch.GetTimestamp() - stageStart;
        lock (_transferStatsSync)
        {
            _transferWriteTicks += writeTicks;
            _transferMaxWriteTicks = Math.Max(_transferMaxWriteTicks, writeTicks);
            _transferBytesWritten += length;
            _transferFrameWrites += frameCount;
        }
    }

    public TransferWriteStats SnapshotTransferWriteStats(bool reset = false)
    {
        lock (_transferStatsSync)
        {
            var stats = new TransferWriteStats(
                StopwatchTicksToMs(_transferBuildTicks),
                StopwatchTicksToMs(_transferCobsTicks),
                StopwatchTicksToMs(_transferWireTicks),
                StopwatchTicksToMs(_transferWriteTicks),
                StopwatchTicksToMs(_transferMaxWriteTicks),
                _transferFrameWrites,
                _transferBytesWritten);

            if (reset)
            {
                _transferBuildTicks = 0;
                _transferCobsTicks = 0;
                _transferWireTicks = 0;
                _transferWriteTicks = 0;
                _transferMaxWriteTicks = 0;
                _transferBytesWritten = 0;
                _transferFrameWrites = 0;
            }

            return stats;
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
            crc = Crc32Table[(int)((crc ^ data[index]) & 0xFFu)] ^ (crc >> 8);
        return ~crc;
    }

    private static uint[] BuildCrc32Table()
    {
        var table = new uint[256];
        for (uint value = 0; value < table.Length; value++)
        {
            uint crc = value;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
            table[value] = crc;
        }
        return table;
    }

    private static long StopwatchTicksToMs(long ticks)
        => (long)(ticks * 1000.0 / Stopwatch.Frequency);

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
                if (codeIndex >= output.Length || write >= output.Length)
                    return -1;
                output[codeIndex] = code;
                code = 1;
                codeIndex = write++;
                read++;
                continue;
            }

            if (write >= output.Length)
                return -1;
            output[write++] = input[read++];
            code++;
            if (code == 0xFF)
            {
                if (codeIndex >= output.Length || write >= output.Length)
                    return -1;
                output[codeIndex] = code;
                code = 1;
                codeIndex = write++;
            }
        }

        if (codeIndex >= output.Length)
            return -1;
        output[codeIndex] = code;
        return write;
    }

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

    private static bool ShouldEscapeFrameByte(byte value)
        => value is TransferFrameMarker or TransferFrameEscape or (byte)'\r' or (byte)'\n';
}
