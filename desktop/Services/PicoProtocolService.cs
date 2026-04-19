using System;
using System.Collections.Generic;
using System.Globalization;
using PortableCncApp.ViewModels;

namespace PortableCncApp.Services;

/// <summary>Machine position snapshot from @POS.</summary>
public record struct PicoPos(double MX, double MY, double MZ, double WX, double WY, double WZ);

/// <summary>Device info from @INFO response.</summary>
public record PicoInfo(string Firmware, string Board, bool TeensyConnected);

/// <summary>Transfer session metadata from FILE_UPLOAD_READY / FILE_DOWNLOAD_READY.</summary>
public readonly record struct FileTransferReady(string Name, long Size, byte TransferId, int ChunkSize);
public readonly record struct FileTransferComplete(byte TransferId, string Name, long Size);
public readonly record struct FileDownloadComplete(byte TransferId, string Name, string Crc32Hex);

/// <summary>
/// Parses the Pico @-protocol from SerialService and fires typed events.
/// Exposes typed Send*() methods for all outbound commands.
/// All events fire on the UI thread (inherited from SerialService.LineReceived).
/// </summary>
public sealed class PicoProtocolService
{
    public const int FileTransferChunkSize = 96;
    public const int BinaryTransferChunkSize = FileTransferChunkSize;

    private const byte UploadDataFrameType = 1;
    private const byte UploadAckFrameType = 2;
    private const byte DownloadDataFrameType = 3;
    private const byte DownloadAckFrameType = 4;

    private readonly SerialService _serial;

    // ── Incoming events ──────────────────────────────────────────────────────

    /// <summary>Fired when a @STATE line is received and successfully parsed.</summary>
    public event Action<MachineOperationState>? StateChanged;

    /// <summary>Fired when a @CAPS line is received.</summary>
    public event Action<CapsFlags>? CapsChanged;

    /// <summary>Fired when a @SAFETY line is received.</summary>
    public event Action<SafetyLevel>? SafetyChanged;

    /// <summary>
    /// Fired when a @EVENT line is received.
    /// First arg is the event name (e.g. "JOB_PROGRESS", "JOB_COMPLETE").
    /// Second arg contains any key=value parameters on that line.
    /// </summary>
    public event Action<string, IReadOnlyDictionary<string, string>>? EventReceived;

    /// <summary>Fired when a @POS line is received.</summary>
    public event Action<PicoPos>? PositionChanged;

    /// <summary>Fired when @INFO ... is received (response to SendInfo).</summary>
    public event Action<PicoInfo>? InfoReceived;

    /// <summary>Fired when @OK PONG is received (response to SendPing).</summary>
    public event Action? PongReceived;

    /// <summary>
    /// Fired for every @OK token, including generic command acknowledgements
    /// like HOME/JOG/START as well as specialized file-transfer tokens.
    /// </summary>
    public event Action<string, IReadOnlyDictionary<string, string>>? OkReceived;

    /// <summary>Fired when an @ERROR line is received. Arg is the reason string.</summary>
    public event Action<string>? ErrorReceived;

    /// <summary>
    /// Fired once per file entry during a @FILE_LIST response.
    /// Args: file name on SD card, size in bytes.
    /// </summary>
    public event Action<string, long>? FileListEntryReceived;

    /// <summary>Fired when @OK FILE_LIST_END is received. Args: file count, free bytes on SD.</summary>
    public event Action<int, long>? FileListEndReceived;

    /// <summary>Fired when @OK FILE_UPLOAD_READY is received — Pico ready to accept chunks.</summary>
    public event Action<FileTransferReady>? UploadReadyReceived;

    /// <summary>Fired when a binary upload-ack frame is received.</summary>
    public event Action<byte, uint, uint>? UploadChunkAckReceived;

    /// <summary>Fired when @OK FILE_UPLOAD_END is received — upload complete and verified. Args: name, final size.</summary>
    public event Action<FileTransferComplete>? UploadCompleteReceived;

    /// <summary>Fired when @OK FILE_UPLOAD_ABORT is received.</summary>
    public event Action? UploadAbortedReceived;

    /// <summary>Fired when @ERROR FILE_EXISTS is received during upload initiation. Arg is the filename.</summary>
    public event Action<string>? UploadFileExistsReceived;

    /// <summary>
    /// Fired when the upload is terminated by the Pico due to an unrecoverable error
    /// (SD_WRITE_FAIL, UPLOAD_SD_REMOVED, CRC_FAIL, etc.). Arg is the REASON string.
    /// The desktop must cancel the upload and reset state — no abort command needed.
    /// </summary>
    public event Action<string>? UploadFailedReceived;

    /// <summary>Fired when @OK FILE_DOWNLOAD_READY is received. Args: name, size in bytes.</summary>
    public event Action<FileTransferReady>? DownloadReadyReceived;

    /// <summary>Fired when a download data chunk is received.</summary>
    public event Action<byte, uint, byte[]>? DownloadChunkReceived;

    /// <summary>Fired when @OK FILE_DOWNLOAD_END is received. Args: name, crc32 hex.</summary>
    public event Action<FileDownloadComplete>? DownloadCompleteReceived;

    /// <summary>Fired when @OK FILE_DOWNLOAD_ABORT is received.</summary>
    public event Action? DownloadAbortedReceived;

    /// <summary>Fired when the Pico rejects or aborts a file download.</summary>
    public event Action<string>? DownloadFailedReceived;

    /// <summary>Fired when @WAIT is received — Pico is busy, caller should retry shortly. Arg is optional reason.</summary>
    public event Action<string>? WaitReceived;

    /// <summary>Fired when a @JOB NAME=<filename|NONE> snapshot is received.</summary>
    public event Action<string?>? JobChanged;

    /// <summary>Fired when @OK FILE_DELETE NAME=<filename> is received.</summary>
    public event Action<string>? FileDeleteConfirmed;

    /// <summary>Passthrough for any line the service does not recognise — useful for diagnostics.</summary>
    public event Action<string>? UnknownLineReceived;

    public PicoProtocolService(SerialService serial)
    {
        _serial = serial;
        _serial.LineReceived += OnLineReceived;
        _serial.FrameReceived += OnFrameReceived;
    }

    // ── Outbound commands ────────────────────────────────────────────────────

    public void SendPing()   => Send("@PING");
    public void SendInfo()   => Send("@INFO");
    public void SendStatus() => Send("@STATUS");

    public void SendBeginJob()  => Send("@BEGIN_JOB");
    public void SendEndJob()    => Send("@END_JOB");
    public void SendClearJob()  => Send("@CLEAR_JOB");

    public void SendHome() => Send("@HOME");

    public void SendJog(char axis, float dist, float feed)
        => Send(FormattableString.Invariant($"@JOG AXIS={axis} DIST={dist:F3} FEED={feed:F0}"));

    public void SendJogCancel() => Send("@JOG_CANCEL");

    /// <param name="axes">One of: X, Y, Z, ALL</param>
    public void SendZero(string axes) => Send($"@ZERO AXIS={axes}");

    public void SendFileList()              => Send("@FILE_LIST");
    public void SendFileLoad(string name)   => Send($"@FILE_LOAD NAME={EncodeValue(name)}");
    public void SendFileUnload()            => Send("@FILE_UNLOAD");
    public void SendFileDelete(string name) => Send($"@FILE_DELETE NAME={EncodeValue(name)}");
    public void SendFileDownload(string name) => Send($"@FILE_DOWNLOAD NAME={EncodeValue(name)}");
    /// <param name="overwrite">Set true if user confirmed overwrite of an existing file.</param>
    public void SendFileUpload(string name, long sizeBytes, bool overwrite = false)
        => Send(overwrite
            ? $"@FILE_UPLOAD NAME={EncodeValue(name)} SIZE={sizeBytes} OVERWRITE=1"
            : $"@FILE_UPLOAD NAME={EncodeValue(name)} SIZE={sizeBytes}");

    /// <param name="crc32Hex">CRC-32 of the full raw file, 8 lowercase hex digits.</param>
    public void SendFileUploadEnd(string crc32Hex)
        => Send($"@FILE_UPLOAD_END CRC={crc32Hex}");

    public void SendFileUploadAbort() => Send("@FILE_UPLOAD_ABORT");
    public void SendFileDownloadAbort() => Send("@FILE_DOWNLOAD_ABORT");

    public void SendUploadDataFrame(byte transferId, uint sequence, ReadOnlySpan<byte> payload)
        => _serial.SendFrame(UploadDataFrameType, transferId, sequence, payload);

    public void SendUploadChunk(byte transferId, uint sequence, ReadOnlySpan<byte> payload)
        => Send(FormattableString.Invariant($"@FILE_UPLOAD_CHUNK ID={transferId} SEQ={sequence} HEX={Convert.ToHexString(payload)}"));

    public void SendDownloadAckFrame(byte transferId, uint sequence)
        => _serial.SendFrame(DownloadAckFrameType, transferId, sequence, ReadOnlySpan<byte>.Empty);

    public void SendDownloadAck(byte transferId, uint sequence)
        => Send(FormattableString.Invariant($"@FILE_DOWNLOAD_ACK ID={transferId} SEQ={sequence}"));

    public void SendJobStart()  => Send("@START");
    public void SendJobPause()  => Send("@PAUSE");
    public void SendJobResume() => Send("@RESUME");
    public void SendJobAbort()  => Send("@ABORT");

    public void SendSpindleOn(int rpm) => Send($"@SPINDLE_ON RPM={rpm}");
    public void SendSpindleOff()       => Send("@SPINDLE_OFF");

    public void SendEstop() => Send("@ESTOP");
    public void SendReset() => Send("@RESET");

    /// <param name="percent">Override percentage (e.g. 100 = normal, 50 = half speed)</param>
    public void SendOverrideFeed(int percent)    => Send($"@OVERRIDE FEED={percent}");
    public void SendOverrideSpindle(int percent) => Send($"@OVERRIDE SPINDLE={percent}");
    public void SendOverrideRapid(int percent)   => Send($"@OVERRIDE RAPID={percent}");

    // ── Incoming line router ─────────────────────────────────────────────────

    private void OnLineReceived(string line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;

        if (!line.StartsWith('@'))
        {
            UnknownLineReceived?.Invoke(line);
            return;
        }

        var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0) return;

        switch (parts[0])
        {
            case "@STATE":  ParseState(parts);     break;
            case "@CAPS":   ParseCaps(parts);      break;
            case "@SAFETY": ParseSafety(parts);    break;
            case "@EVENT":  ParseEvent(parts);     break;
            case "@POS":    ParsePos(parts);       break;
            case "@OK":     ParseOk(parts);        break;
            case "@INFO":   ParseInfo(parts);      break;
            case "@JOB":    ParseJob(parts);       break;
            case "@ERROR":  ParseError(parts);     break;
            case "@FILE":   ParseFileEntry(parts); break;
            case "@FILE_DOWNLOAD_CHUNK": ParseDownloadChunk(parts); break;
            case "@WAIT":   ParseWait(parts);      break;
            default:
                UnknownLineReceived?.Invoke(line);
                break;
        }
    }

    private void OnFrameReceived(SerialService.TransferFrame frame)
    {
        switch (frame.FrameType)
        {
            case UploadAckFrameType:
                UploadChunkAckReceived?.Invoke(
                    frame.TransferId,
                    frame.Sequence,
                    frame.Payload.Length >= sizeof(uint) ? ReadUInt32LittleEndian(frame.Payload, 0) : 0u);
                break;

            case DownloadDataFrameType:
                DownloadChunkReceived?.Invoke(frame.TransferId, frame.Sequence, frame.Payload);
                break;
        }
    }

    // ── Per-verb parsers ─────────────────────────────────────────────────────

    private void ParseState(string[] parts)
    {
        if (parts.Length < 2) return;
        if (TryParseState(parts[1], out var state))
            StateChanged?.Invoke(state);
    }

    private void ParseCaps(string[] parts)
    {
        var kv = ParseKeyValues(parts, startIndex: 1);
        var caps = new CapsFlags(
            Motion:     GetBool(kv, "MOTION"),
            Probe:      GetBool(kv, "PROBE"),
            Spindle:    GetBool(kv, "SPINDLE"),
            FileLoad:   GetBool(kv, "FILE_LOAD"),
            JobStart:   GetBool(kv, "JOB_START"),
            JobPause:   GetBool(kv, "JOB_PAUSE"),
            JobResume:  GetBool(kv, "JOB_RESUME"),
            JobAbort:   GetBool(kv, "JOB_ABORT"),
            Overrides:  GetBool(kv, "OVERRIDES"),
            Reset:      GetBool(kv, "RESET"));
        CapsChanged?.Invoke(caps);
    }

    private void ParseSafety(string[] parts)
    {
        if (parts.Length < 2) return;
        if (TryParseSafety(parts[1], out var level))
            SafetyChanged?.Invoke(level);
    }

    private void ParseEvent(string[] parts)
    {
        if (parts.Length < 2) return;
        var name = parts[1];
        var kv   = ParseKeyValues(parts, startIndex: 2);
        EventReceived?.Invoke(name, kv);
    }

    private void ParsePos(string[] parts)
    {
        var kv  = ParseKeyValues(parts, startIndex: 1);
        var pos = new PicoPos(
            MX: GetDouble(kv, "MX"),
            MY: GetDouble(kv, "MY"),
            MZ: GetDouble(kv, "MZ"),
            WX: GetDouble(kv, "WX"),
            WY: GetDouble(kv, "WY"),
            WZ: GetDouble(kv, "WZ"));
        PositionChanged?.Invoke(pos);
    }

    private void ParseOk(string[] parts)
    {
        if (parts.Length < 2) return;
        var token = parts[1];
        var kv = ParseKeyValues(parts, startIndex: 2);
        OkReceived?.Invoke(token, kv);

        switch (token)
        {
            case "PONG":
                PongReceived?.Invoke();
                break;

            case "FILE_LIST_END":
                int.TryParse(kv.GetValueOrDefault("COUNT", "0"), out var count);
                long.TryParse(kv.GetValueOrDefault("FREE",  "0"), out var free);
                FileListEndReceived?.Invoke(count, free);
                break;

            case "FILE_UPLOAD_READY":
                long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var uploadSize);
                byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var uploadTransferId);
                int.TryParse(kv.GetValueOrDefault("CHUNK", "0"), out var uploadChunkSize);
                UploadReadyReceived?.Invoke(new FileTransferReady(
                    kv.GetValueOrDefault("NAME", ""),
                    uploadSize,
                    uploadTransferId,
                    uploadChunkSize));
                break;

            case "FILE_UPLOAD_END":
                long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var uploadedSize);
                byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var uploadCompleteTransferId);
                UploadCompleteReceived?.Invoke(new FileTransferComplete(
                    uploadCompleteTransferId,
                    kv.GetValueOrDefault("NAME", ""),
                    uploadedSize));
                break;

            case "FILE_UPLOAD_ABORT":
                UploadAbortedReceived?.Invoke();
                break;

            case "FILE_UPLOAD_CHUNK":
                byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var uploadChunkTransferId);
                uint.TryParse(kv.GetValueOrDefault("SEQ", "0"), out var uploadChunkSeq);
                uint.TryParse(kv.GetValueOrDefault("BYTES", "0"), out var uploadBytesCommitted);
                UploadChunkAckReceived?.Invoke(uploadChunkTransferId, uploadChunkSeq, uploadBytesCommitted);
                break;

            case "FILE_DOWNLOAD_READY":
                long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var downloadSize);
                byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var downloadTransferId);
                int.TryParse(kv.GetValueOrDefault("CHUNK", "0"), out var downloadChunkSize);
                DownloadReadyReceived?.Invoke(new FileTransferReady(
                    kv.GetValueOrDefault("NAME", ""),
                    downloadSize,
                    downloadTransferId,
                    downloadChunkSize));
                break;

            case "FILE_DOWNLOAD_END":
                byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var downloadCompleteTransferId);
                DownloadCompleteReceived?.Invoke(new FileDownloadComplete(
                    downloadCompleteTransferId,
                    kv.GetValueOrDefault("NAME", ""),
                    kv.GetValueOrDefault("CRC", "")));
                break;

            case "FILE_DOWNLOAD_ABORT":
                DownloadAbortedReceived?.Invoke();
                break;

            case "FILE_DELETE":
                FileDeleteConfirmed?.Invoke(kv.GetValueOrDefault("NAME", ""));
                break;
        }
    }

    private void ParseFileEntry(string[] parts)
    {
        if (parts.Length < 2) return;
        var kv = ParseKeyValues(parts, startIndex: 1);
        var name = kv.GetValueOrDefault("NAME", parts.Length > 1 ? DecodeValue(parts[1]) : "");
        long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var size);
        FileListEntryReceived?.Invoke(name, size);
    }

    private void ParseDownloadChunk(string[] parts)
    {
        var kv = ParseKeyValues(parts, startIndex: 1);
        if (!byte.TryParse(kv.GetValueOrDefault("ID", "0"), out var transferId) ||
            !uint.TryParse(kv.GetValueOrDefault("SEQ", "0"), out var sequence) ||
            !TryDecodeHex(kv.GetValueOrDefault("HEX", ""), out var payload))
        {
            DownloadFailedReceived?.Invoke("DOWNLOAD_BAD_CHUNK");
            return;
        }

        DownloadChunkReceived?.Invoke(transferId, sequence, payload);
    }

    private void ParseInfo(string[] parts)
    {
        var kv   = ParseKeyValues(parts, startIndex: 1);
        var info = new PicoInfo(
            Firmware:        kv.GetValueOrDefault("FIRMWARE", "?"),
            Board:           kv.GetValueOrDefault("BOARD",    "?"),
            TeensyConnected: kv.GetValueOrDefault("TEENSY",   "") == "CONNECTED");
        InfoReceived?.Invoke(info);
    }

    private void ParseJob(string[] parts)
    {
        var kv = ParseKeyValues(parts, startIndex: 1);
        var name = kv.GetValueOrDefault("NAME", "NONE");
        JobChanged?.Invoke(string.Equals(name, "NONE", StringComparison.OrdinalIgnoreCase) ? null : name);
    }

    private void ParseError(string[] parts)
    {
        if (parts.Length < 2) { ErrorReceived?.Invoke("UNKNOWN"); return; }

        var errorKv = ParseKeyValues(parts, startIndex: 2);

        switch (parts[1])
        {
            // Upload-specific errors routed to dedicated events so the upload
            // state machine can handle them without touching the generic error path.
            case "UPLOAD_FILE_EXISTS":
                UploadFileExistsReceived?.Invoke(errorKv.GetValueOrDefault("NAME", ""));
                return;

            case "FILE_UPLOAD_CRC_FAIL":
            case "UPLOAD_CRC_FAIL":
            case "UPLOAD_SIZE_MISMATCH":
            case "UPLOAD_SD_WRITE_FAIL":
            case "UPLOAD_SD_REMOVED":
                // Pico has already cleaned up — desktop just resets upload state.
                UploadFailedReceived?.Invoke(parts[1]);
                return;

            case "CHUNK":
                // @ERROR CHUNK SEQ=n REASON=r — handled by the chunk retry logic in FilesViewModel.
                int.TryParse(errorKv.GetValueOrDefault("SEQ", "-1"), out var errSeq);
                var chunkReason = errorKv.GetValueOrDefault("REASON", "UNKNOWN");
                UploadFailedReceived?.Invoke($"CHUNK SEQ={errSeq} REASON={chunkReason}");
                return;

            case "SD_FULL":
            case "UPLOAD_SD_FULL":
            {
                long.TryParse(errorKv.GetValueOrDefault("FREE", "0"), out var sdFree);
                var msg = sdFree > 0
                    ? $"SD card full ({sdFree / 1024} KB free)"
                    : "SD card full";
                UploadFailedReceived?.Invoke(msg);
                return;
            }

            case "SD_NOT_MOUNTED":
            case "UPLOAD_SD_NOT_MOUNTED":
                UploadFailedReceived?.Invoke("SD card not present");
                ErrorReceived?.Invoke(parts[1]);
                return;

            case "SD_READ_FAIL":
                ErrorReceived?.Invoke(parts[1]);
                DownloadFailedReceived?.Invoke(parts[1]);
                return;

            case "DOWNLOAD_SD_NOT_MOUNTED":
            case "DOWNLOAD_SD_READ_FAIL":
            case "DOWNLOAD_FILE_NOT_FOUND":
            case "DOWNLOAD_INVALID_STATE":
            case "DOWNLOAD_BUSY":
            case "DOWNLOAD_BAD_SEQUENCE":
            case "DOWNLOAD_INVALID_SESSION":
            case "DOWNLOAD_SD_REMOVED":
                DownloadFailedReceived?.Invoke(parts[1]);
                return;

            case "TRANSFER_BUSY":
            case "INVALID_TRANSFER":
            case "UPLOAD_BUSY":
            case "UPLOAD_INVALID_STATE":
            case "UPLOAD_BAD_SEQUENCE":
            case "UPLOAD_INVALID_SESSION":
            case "UPLOAD_NO_SESSION":
                UploadFailedReceived?.Invoke(parts[1]);
                DownloadFailedReceived?.Invoke(parts[1]);
                ErrorReceived?.Invoke(parts[1]);
                return;
        }

        // Check for upload-fatal errors that can arrive mid-stream.
        if (parts[1] is "SD_WRITE_FAIL" or "INVALID_STATE" or "FILE_UPLOAD_SIZE_MISMATCH")
        {
            UploadFailedReceived?.Invoke(parts[1]);
            DownloadFailedReceived?.Invoke(parts[1]);
            return;
        }

        if (parts[1] is "FILE_NOT_FOUND")
        {
            DownloadFailedReceived?.Invoke(parts[1]);
            return;
        }

        if (parts[1].StartsWith("UPLOAD_", StringComparison.OrdinalIgnoreCase))
        {
            UploadFailedReceived?.Invoke(parts[1]);
            return;
        }

        if (parts[1].StartsWith("DOWNLOAD_", StringComparison.OrdinalIgnoreCase))
        {
            DownloadFailedReceived?.Invoke(parts[1]);
            return;
        }

        var reason = string.Join(' ', parts[1..]);
        ErrorReceived?.Invoke(reason);
    }

    // ── Parse helpers ────────────────────────────────────────────────────────

    private static Dictionary<string, string> ParseKeyValues(string[] parts, int startIndex)
    {
        var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = startIndex; i < parts.Length; i++)
        {
            int eq = parts[i].IndexOf('=');
            if (eq > 0)
                dict[parts[i][..eq]] = DecodeValue(parts[i][(eq + 1)..]);
        }
        return dict;
    }

    private static string EncodeValue(string value) => Uri.EscapeDataString(value);

    private static string DecodeValue(string value)
    {
        try
        {
            return Uri.UnescapeDataString(value);
        }
        catch
        {
            return value;
        }
    }

    private static bool TryDecodeHex(string hex, out byte[] payload)
    {
        payload = Array.Empty<byte>();
        if (hex.Length % 2 != 0)
            return false;

        var bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; i++)
        {
            int hi = HexValue(hex[i * 2]);
            int lo = HexValue(hex[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            bytes[i] = (byte)((hi << 4) | lo);
        }

        payload = bytes;
        return true;
    }

    private static int HexValue(char c)
    {
        if (c is >= '0' and <= '9') return c - '0';
        if (c is >= 'A' and <= 'F') return c - 'A' + 10;
        if (c is >= 'a' and <= 'f') return c - 'a' + 10;
        return -1;
    }

    private static bool GetBool(Dictionary<string, string> kv, string key)
        => kv.TryGetValue(key, out var v) && v == "1";

    private static double GetDouble(Dictionary<string, string> kv, string key)
        => kv.TryGetValue(key, out var v)
           && double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out var d)
           ? d : 0.0;

    private static bool TryParseState(string s, out MachineOperationState state) => s switch
    {
        "BOOTING"             => Out(MachineOperationState.Booting,            out state),
        "TEENSY_DISCONNECTED" => Out(MachineOperationState.TeensyDisconnected, out state),
        "SYNCING"             => Out(MachineOperationState.Syncing,            out state),
        "IDLE"                => Out(MachineOperationState.Idle,               out state),
        "HOMING"              => Out(MachineOperationState.Homing,             out state),
        "JOG"                 => Out(MachineOperationState.Jog,                out state),
        "STARTING"            => Out(MachineOperationState.Starting,           out state),
        "RUNNING"             => Out(MachineOperationState.Running,            out state),
        "HOLD"                => Out(MachineOperationState.Hold,               out state),
        "FAULT"               => Out(MachineOperationState.Fault,              out state),
        "ESTOP"               => Out(MachineOperationState.Estop,              out state),
        "COMMS_FAULT"         => Out(MachineOperationState.CommsFault,         out state),
        "UPLOADING"           => Out(MachineOperationState.Uploading,          out state),
        _                     => Fail(out state)
    };

    private static bool TryParseSafety(string s, out SafetyLevel level) => s switch
    {
        "SAFE"       => Out(SafetyLevel.Safe,       out level),
        "MONITORING" => Out(SafetyLevel.Monitoring, out level),
        "WARNING"    => Out(SafetyLevel.Warning,    out level),
        "CRITICAL"   => Out(SafetyLevel.Critical,   out level),
        _            => Fail(out level)
    };

    private void ParseWait(string[] parts)
    {
        var reason = parts.Length > 1 ? string.Join(' ', parts[1..]) : "";
        WaitReceived?.Invoke(reason);
    }

    private static bool Out<T>(T value, out T result) { result = value; return true; }
    private static bool Fail<T>(out T result)          { result = default!; return false; }

    private void Send(string cmd) => _serial.SendCommand(cmd);

    private static uint ReadUInt32LittleEndian(byte[] buffer, int offset)
        => (uint)(buffer[offset]
                | (buffer[offset + 1] << 8)
                | (buffer[offset + 2] << 16)
                | (buffer[offset + 3] << 24));
}
