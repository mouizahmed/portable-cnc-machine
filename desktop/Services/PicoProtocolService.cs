using System;
using System.Collections.Generic;
using System.Globalization;
using PortableCncApp.ViewModels;

namespace PortableCncApp.Services;

/// <summary>Machine position snapshot from @POS.</summary>
public record struct PicoPos(double MX, double MY, double MZ, double WX, double WY, double WZ);

/// <summary>Device info from @INFO response.</summary>
public record PicoInfo(string Firmware, string Board, bool TeensyConnected);

/// <summary>
/// Parses the Pico @-protocol from SerialService and fires typed events.
/// Exposes typed Send*() methods for all outbound commands.
/// All events fire on the UI thread (inherited from SerialService.LineReceived).
/// </summary>
public sealed class PicoProtocolService
{
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
    public event Action<string>? UploadReadyReceived;

    /// <summary>Fired when @OK CHUNK SEQ=n is received. Arg is the acknowledged sequence number.</summary>
    public event Action<int>? ChunkAckReceived;

    /// <summary>Fired when @OK FILE_UPLOAD_END is received — upload complete and verified. Args: name, final size.</summary>
    public event Action<string, long>? UploadCompleteReceived;

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
    public event Action<string, long>? DownloadReadyReceived;

    /// <summary>Fired when @CHUNK SEQ=n DATA=... is received during a file download.</summary>
    public event Action<int, string>? DownloadChunkReceived;

    /// <summary>Fired when @OK FILE_DOWNLOAD_END is received. Args: name, crc32 hex.</summary>
    public event Action<string, string>? DownloadCompleteReceived;

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
    public void SendFileLoad(string name)   => Send($"@FILE_LOAD NAME={name}");
    public void SendFileUnload()            => Send("@FILE_UNLOAD");
    public void SendFileDelete(string name) => Send($"@FILE_DELETE NAME={name}");
    public void SendFileDownload(string name) => Send($"@FILE_DOWNLOAD NAME={name}");
    public void SendDownloadAck(int seq)      => Send($"@ACK SEQ={seq}");

    /// <param name="overwrite">Set true if user confirmed overwrite of an existing file.</param>
    public void SendFileUpload(string name, long sizeBytes, bool overwrite = false)
        => Send(overwrite
            ? $"@FILE_UPLOAD NAME={name} SIZE={sizeBytes} OVERWRITE=1"
            : $"@FILE_UPLOAD NAME={name} SIZE={sizeBytes}");

    /// <param name="seq">Zero-based chunk sequence number.</param>
    /// <param name="base64Data">Up to 256 base64 characters (192 raw bytes).</param>
    public void SendChunk(int seq, string base64Data)
        => Send($"@CHUNK SEQ={seq} DATA={base64Data}");

    /// <param name="crc32Hex">CRC-32 of the full raw file, 8 lowercase hex digits.</param>
    public void SendFileUploadEnd(string crc32Hex)
        => Send($"@FILE_UPLOAD_END CRC={crc32Hex}");

    public void SendFileUploadAbort() => Send("@FILE_UPLOAD_ABORT");

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
            case "@CHUNK":  ParseChunk(parts);     break;
            case "@ERROR":  ParseError(parts);     break;
            case "@FILE":   ParseFileEntry(parts); break;
            case "@WAIT":   ParseWait(parts);      break;
            default:
                UnknownLineReceived?.Invoke(line);
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
                UploadReadyReceived?.Invoke(kv.GetValueOrDefault("NAME", ""));
                break;

            case "CHUNK":
                int.TryParse(kv.GetValueOrDefault("SEQ", "-1"), out var seq);
                ChunkAckReceived?.Invoke(seq);
                break;

            case "FILE_UPLOAD_END":
                long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var uploadedSize);
                UploadCompleteReceived?.Invoke(kv.GetValueOrDefault("NAME", ""), uploadedSize);
                break;

            case "FILE_UPLOAD_ABORT":
                UploadAbortedReceived?.Invoke();
                break;

            case "FILE_DOWNLOAD_READY":
                long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var downloadSize);
                DownloadReadyReceived?.Invoke(kv.GetValueOrDefault("NAME", ""), downloadSize);
                break;

            case "FILE_DOWNLOAD_END":
                DownloadCompleteReceived?.Invoke(
                    kv.GetValueOrDefault("NAME", ""),
                    kv.GetValueOrDefault("CRC", ""));
                break;

            case "FILE_DELETE":
                FileDeleteConfirmed?.Invoke(kv.GetValueOrDefault("NAME", ""));
                break;
        }
    }

    private void ParseFileEntry(string[] parts)
    {
        if (parts.Length < 2) return;
        var name = parts[1];
        var kv   = ParseKeyValues(parts, startIndex: 2);
        long.TryParse(kv.GetValueOrDefault("SIZE", "0"), out var size);
        FileListEntryReceived?.Invoke(name, size);
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

    private void ParseChunk(string[] parts)
    {
        var kv = ParseKeyValues(parts, startIndex: 1);
        int.TryParse(kv.GetValueOrDefault("SEQ", "-1"), out var seq);
        DownloadChunkReceived?.Invoke(seq, kv.GetValueOrDefault("DATA", ""));
    }

    private void ParseError(string[] parts)
    {
        if (parts.Length < 2) { ErrorReceived?.Invoke("UNKNOWN"); return; }

        var errorKv = ParseKeyValues(parts, startIndex: 2);

        switch (parts[1])
        {
            // Upload-specific errors routed to dedicated events so the upload
            // state machine can handle them without touching the generic error path.
            case "FILE_EXISTS":
                UploadFileExistsReceived?.Invoke(errorKv.GetValueOrDefault("NAME", ""));
                return;

            case "FILE_UPLOAD_CRC_FAIL":
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
            {
                long.TryParse(errorKv.GetValueOrDefault("FREE", "0"), out var sdFree);
                var msg = sdFree > 0
                    ? $"SD card full ({sdFree / 1024} KB free)"
                    : "SD card full";
                UploadFailedReceived?.Invoke(msg);
                return;
            }

            case "SD_NOT_MOUNTED":
                UploadFailedReceived?.Invoke("SD card not present");
                return;

            case "SD_READ_FAIL":
                DownloadFailedReceived?.Invoke(parts[1]);
                return;
        }

        // Check for upload-fatal errors that can arrive mid-stream.
        if (parts[1] is "SD_WRITE_FAIL" or "UPLOAD_BUSY" or "INVALID_STATE")
        {
            UploadFailedReceived?.Invoke(parts[1]);
            return;
        }

        if (parts[1] is "FILE_NOT_FOUND")
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
                dict[parts[i][..eq]] = parts[i][(eq + 1)..];
        }
        return dict;
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
}
