using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using PortableCncApp.Protocol;
using PortableCncApp.ViewModels;

namespace PortableCncApp.Services;

public record struct PicoPos(double MX, double MY, double MZ, double WX, double WY, double WZ);

public record PicoInfo(string Firmware, string Board, bool TeensyConnected);
public readonly record struct PicoMachineSettings(
    double StepsPerMmX,
    double StepsPerMmY,
    double StepsPerMmZ,
    double MaxFeedRateX,
    double MaxFeedRateY,
    double MaxFeedRateZ,
    double AccelerationX,
    double AccelerationY,
    double AccelerationZ,
    double MaxTravelX,
    double MaxTravelY,
    double MaxTravelZ,
    bool SoftLimitsEnabled,
    bool HardLimitsEnabled,
    double SpindleMinRpm,
    double SpindleMaxRpm,
    double WarningTemperature,
    double MaxTemperature);

public readonly record struct FileTransferReady(string Name, long Size, byte TransferId, int ChunkSize);
public readonly record struct FileTransferComplete(byte TransferId, string Name, long Size);
public readonly record struct FileDownloadComplete(byte TransferId, string Name, string Crc32Hex);

public sealed class PicoProtocolService
{
    public const int BinaryTransferChunkSize = 4096;

    private const byte UploadDataFrameType = 1;
    private const byte UploadAckFrameType = 2;
    private const byte DownloadDataFrameType = 3;
    private const byte DownloadAckFrameType = 4;
    private const byte CommandFrameType = 5;
    private const byte ResponseFrameType = 6;
    private const byte EventFrameType = 7;

    private static readonly IReadOnlyDictionary<string, string> EmptyMetadata =
        new Dictionary<string, string>(0);

    private readonly SerialService _serial;
    private uint _nextRequestSequence = 1;

    public event Action<MachineOperationState>? StateChanged;
    public event Action<CapsFlags>? CapsChanged;
    public event Action<SafetyLevel>? SafetyChanged;
    public event Action<string, IReadOnlyDictionary<string, string>>? EventReceived;
    public event Action<PicoPos>? PositionChanged;
    public event Action<PicoInfo>? InfoReceived;
    public event Action<PicoMachineSettings>? MachineSettingsReceived;
    public event Action? PongReceived;
    public event Action<string, IReadOnlyDictionary<string, string>>? OkReceived;
    public event Action<string>? ErrorReceived;
    public event Action<string, long>? FileListEntryReceived;
    public event Action<int, long>? FileListEndReceived;
    public event Action<FileTransferReady>? UploadReadyReceived;
    public event Action<byte, uint, uint>? UploadChunkAckReceived;
    public event Action<FileTransferComplete>? UploadCompleteReceived;
    public event Action? UploadAbortedReceived;
    public event Action<string>? UploadFileExistsReceived;
    public event Action<string>? UploadFailedReceived;
    public event Action<FileTransferReady>? DownloadReadyReceived;
    public event Action<byte, uint, byte[]>? DownloadChunkReceived;
    public event Action<FileDownloadComplete>? DownloadCompleteReceived;
    public event Action? DownloadAbortedReceived;
    public event Action<string>? DownloadFailedReceived;
    public event Action<string>? WaitReceived;
    public event Action<string?>? JobChanged;
    public event Action<string>? FileDeleteConfirmed;

    public PicoProtocolService(SerialService serial)
    {
        _serial = serial;
        _serial.FrameReceived += OnFrameReceived;
    }

    public void SendPing()   => SendCommandFrame(new CmdPing   { MessageType = (byte)CommandMessageType.Ping });
    public void SendInfo()   => SendCommandFrame(new CmdInfo   { MessageType = (byte)CommandMessageType.Info });
    public void SendStatus() => SendCommandFrame(new CmdStatus { MessageType = (byte)CommandMessageType.Status });

    public void SendBeginJob() => SendCommandFrame(new CmdBeginJob { MessageType = (byte)CommandMessageType.BeginJob });
    public void SendEndJob()   => SendCommandFrame(new CmdEndJob   { MessageType = (byte)CommandMessageType.EndJob });
    public void SendClearJob() => SendCommandFrame(new CmdClearJob { MessageType = (byte)CommandMessageType.ClearJob });
    public void SendSettingsGet() => SendCommandFrame(new CmdSettingsGet { MessageType = (byte)CommandMessageType.SettingsGet });
    public void SendSettingsSet(PicoMachineSettings settings)
        => SendCommandFrame(new CmdSettingsSet
        {
            MessageType = (byte)CommandMessageType.SettingsSet,
            StepsPerMmX = (float)settings.StepsPerMmX,
            StepsPerMmY = (float)settings.StepsPerMmY,
            StepsPerMmZ = (float)settings.StepsPerMmZ,
            MaxFeedRateX = (float)settings.MaxFeedRateX,
            MaxFeedRateY = (float)settings.MaxFeedRateY,
            MaxFeedRateZ = (float)settings.MaxFeedRateZ,
            AccelerationX = (float)settings.AccelerationX,
            AccelerationY = (float)settings.AccelerationY,
            AccelerationZ = (float)settings.AccelerationZ,
            MaxTravelX = (float)settings.MaxTravelX,
            MaxTravelY = (float)settings.MaxTravelY,
            MaxTravelZ = (float)settings.MaxTravelZ,
            SoftLimitsEnabled = settings.SoftLimitsEnabled ? (byte)1 : (byte)0,
            HardLimitsEnabled = settings.HardLimitsEnabled ? (byte)1 : (byte)0,
            SpindleMinRpm = (float)settings.SpindleMinRpm,
            SpindleMaxRpm = (float)settings.SpindleMaxRpm,
            WarningTemperature = (float)settings.WarningTemperature,
            MaxTemperature = (float)settings.MaxTemperature
        });

    public void SendHome() => SendCommandFrame(new CmdHome { MessageType = (byte)CommandMessageType.Home });

    public void SendJog(char axis, float dist, float feed)
        => SendCommandFrame(new CmdJog
        {
            MessageType = (byte)CommandMessageType.Jog,
            Axis = AxisToProtocol(axis),
            Dist = dist,
            Feed = ToUInt16Clamped(feed)
        });

    public void SendJogCancel()
        => SendCommandFrame(new CmdJogCancel { MessageType = (byte)CommandMessageType.JogCancel });

    public void SendZero(string axes)
        => SendCommandFrame(new CmdZero
        {
            MessageType = (byte)CommandMessageType.Zero,
            AxesMask = (byte)AxesToProtocol(axes)
        });

    public void SendFileList()
        => SendCommandFrame(new CmdFileList { MessageType = (byte)CommandMessageType.FileList });

    public unsafe void SendFileLoad(string name)
    {
        var cmd = new CmdFileLoad { MessageType = (byte)CommandMessageType.FileLoad };
        WriteFixedString(cmd.Name, ProtocolConstants.MaxFilenameBytes, name);
        SendCommandFrame(cmd);
    }

    public void SendFileUnload()
        => SendCommandFrame(new CmdFileUnload { MessageType = (byte)CommandMessageType.FileUnload });

    public unsafe void SendFileDelete(string name)
    {
        var cmd = new CmdFileDelete { MessageType = (byte)CommandMessageType.FileDelete };
        WriteFixedString(cmd.Name, ProtocolConstants.MaxFilenameBytes, name);
        SendCommandFrame(cmd);
    }

    public unsafe void SendFileDownload(string name)
    {
        var cmd = new CmdFileDownload { MessageType = (byte)CommandMessageType.FileDownload };
        WriteFixedString(cmd.Name, ProtocolConstants.MaxFilenameBytes, name);
        SendCommandFrame(cmd);
    }

    public unsafe void SendFileUpload(string name, long sizeBytes, bool overwrite = false)
    {
        var cmd = new CmdFileUpload
        {
            MessageType = (byte)CommandMessageType.FileUpload,
            Size = checked((uint)sizeBytes),
            Overwrite = overwrite ? (byte)1 : (byte)0
        };
        WriteFixedString(cmd.Name, ProtocolConstants.MaxFilenameBytes, name);
        SendCommandFrame(cmd);
    }

    public void SendFileUploadEnd(string crc32Hex)
        => SendCommandFrame(new CmdFileUploadEnd
        {
            MessageType = (byte)CommandMessageType.FileUploadEnd,
            Crc32 = uint.Parse(crc32Hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture)
        });

    public void SendFileUploadAbort()
        => SendCommandFrame(new CmdFileUploadAbort { MessageType = (byte)CommandMessageType.FileUploadAbort });

    public void SendFileDownloadAbort()
        => SendCommandFrame(new CmdFileDownloadAbort { MessageType = (byte)CommandMessageType.FileDownloadAbort });

    public void SendUploadDataFrame(byte transferId, uint sequence, ReadOnlySpan<byte> payload)
        => _serial.SendFrame(UploadDataFrameType, transferId, sequence, payload);

    public void BuildUploadDataFrame(byte transferId, uint sequence, ReadOnlySpan<byte> payload,
                                     System.IO.MemoryStream batch)
        => _serial.BuildFrameInto(UploadDataFrameType, transferId, sequence, payload, batch);

    public void SendPrebuiltFrames(System.IO.MemoryStream frames, int frameCount)
        => _serial.SendPrebuiltFrames(frames, frameCount);

    public SerialService.TransferWriteStats SnapshotTransferWriteStats(bool reset = false)
        => _serial.SnapshotTransferWriteStats(reset);

    public void SendDownloadAckFrame(byte transferId, uint sequence)
        => _serial.SendFrame(DownloadAckFrameType, transferId, sequence, ReadOnlySpan<byte>.Empty);

    public void SendJobStart()  => SendCommandFrame(new CmdStart  { MessageType = (byte)CommandMessageType.Start });
    public void SendJobPause()  => SendCommandFrame(new CmdPause  { MessageType = (byte)CommandMessageType.Pause });
    public void SendJobResume() => SendCommandFrame(new CmdResume { MessageType = (byte)CommandMessageType.Resume });
    public void SendJobAbort()  => SendCommandFrame(new CmdAbort  { MessageType = (byte)CommandMessageType.Abort });

    public void SendSpindleOn(int rpm)
        => SendCommandFrame(new CmdSpindleOn
        {
            MessageType = (byte)CommandMessageType.SpindleOn,
            Rpm = ToUInt16Clamped(rpm)
        });

    public void SendSpindleOff()
        => SendCommandFrame(new CmdSpindleOff { MessageType = (byte)CommandMessageType.SpindleOff });

    public void SendEstop() => SendCommandFrame(new CmdEstop { MessageType = (byte)CommandMessageType.Estop });
    public void SendReset() => SendCommandFrame(new CmdReset { MessageType = (byte)CommandMessageType.Reset });

    public void SendOverrideFeed(int percent)    => SendOverride(OverrideTarget.Feed, percent);
    public void SendOverrideSpindle(int percent) => SendOverride(OverrideTarget.Spindle, percent);
    public void SendOverrideRapid(int percent)   => SendOverride(OverrideTarget.Rapid, percent);

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

            case ResponseFrameType:
                ParseResponseFrame(frame.Payload);
                break;

            case EventFrameType:
                ParseEventFrame(frame.Payload);
                break;
        }
    }

    private unsafe void ParseResponseFrame(byte[] payload)
    {
        if (payload.Length == 0)
            return;

        switch ((ResponseMessageType)payload[0])
        {
            case ResponseMessageType.Pong:
                if (!TryReadPayload(payload, out RespPong _)) return;
                EmitOk("PONG");
                PongReceived?.Invoke();
                break;

            case ResponseMessageType.Info:
                if (!TryReadPayload(payload, out RespInfo info)) return;
                InfoReceived?.Invoke(new PicoInfo(
                    ReadFixedString(info.Firmware, ProtocolConstants.MaxFirmwareBytes),
                    ReadFixedString(info.Board, ProtocolConstants.MaxBoardBytes),
                    info.TeensyConnected != 0));
                break;

            case ResponseMessageType.State:
                if (!TryReadPayload(payload, out RespState state)) return;
                StateChanged?.Invoke(MapMachineState(state.State));
                break;

            case ResponseMessageType.Caps:
                if (!TryReadPayload(payload, out RespCaps caps)) return;
                CapsChanged?.Invoke(MapCaps(caps.Caps));
                break;

            case ResponseMessageType.Safety:
                if (!TryReadPayload(payload, out RespSafety safety)) return;
                SafetyChanged?.Invoke(MapSafety(safety.Safety));
                break;

            case ResponseMessageType.Job:
                if (!TryReadPayload(payload, out RespJob job)) return;
                JobChanged?.Invoke(job.HasJob != 0
                    ? ReadFixedString(job.Name, ProtocolConstants.MaxFilenameBytes)
                    : null);
                break;

            case ResponseMessageType.Pos:
                if (!TryReadPayload(payload, out RespPos pos)) return;
                PositionChanged?.Invoke(new PicoPos(pos.Mx, pos.My, pos.Mz, pos.Wx, pos.Wy, pos.Wz));
                break;

            case ResponseMessageType.CommandAck:
                if (!TryReadPayload(payload, out RespCommandAck ack)) return;
                var token = OkTokenFromCommandType(ack.CommandType);
                if (token.Length > 0)
                    EmitOk(token);
                break;

            case ResponseMessageType.Error:
                if (!TryReadPayload(payload, out RespError error)) return;
                HandleBinaryError((ProtocolErrorCode)error.Error,
                    ReadFixedString(error.Reason, ProtocolConstants.MaxReasonBytes));
                break;

            case ResponseMessageType.FileEntry:
                if (!TryReadPayload(payload, out RespFileEntry entry)) return;
                FileListEntryReceived?.Invoke(
                    ReadFixedString(entry.Name, ProtocolConstants.MaxFilenameBytes),
                    entry.Size);
                break;

            case ResponseMessageType.FileListEnd:
                if (!TryReadPayload(payload, out RespFileListEnd listEnd)) return;
                EmitOk("FILE_LIST_END", Kv(
                    ("COUNT", listEnd.Count.ToString(CultureInfo.InvariantCulture)),
                    ("FREE", ToLongClamped(listEnd.FreeBytes).ToString(CultureInfo.InvariantCulture))));
                FileListEndReceived?.Invoke((int)Math.Min(listEnd.Count, int.MaxValue), ToLongClamped(listEnd.FreeBytes));
                break;

            case ResponseMessageType.FileLoad:
                if (!TryReadPayload(payload, out RespFileLoad fileLoad)) return;
                EmitOk("FILE_LOAD", Kv(("NAME", ReadFixedString(fileLoad.Name, ProtocolConstants.MaxFilenameBytes))));
                break;

            case ResponseMessageType.FileUnload:
                if (!TryReadPayload(payload, out RespFileUnload _)) return;
                EmitOk("FILE_UNLOAD");
                break;

            case ResponseMessageType.FileDelete:
                if (!TryReadPayload(payload, out RespFileDelete fileDelete)) return;
                var deletedName = ReadFixedString(fileDelete.Name, ProtocolConstants.MaxFilenameBytes);
                EmitOk("FILE_DELETE", Kv(("NAME", deletedName)));
                FileDeleteConfirmed?.Invoke(deletedName);
                break;

            case ResponseMessageType.FileUploadReady:
                if (!TryReadPayload(payload, out RespFileUploadReady uploadReady)) return;
                var uploadName = ReadFixedString(uploadReady.Name, ProtocolConstants.MaxFilenameBytes);
                EmitOk("FILE_UPLOAD_READY", Kv(
                    ("NAME", uploadName),
                    ("SIZE", uploadReady.Size.ToString(CultureInfo.InvariantCulture)),
                    ("ID", uploadReady.TransferId.ToString(CultureInfo.InvariantCulture)),
                    ("CHUNK", uploadReady.ChunkSize.ToString(CultureInfo.InvariantCulture))));
                UploadReadyReceived?.Invoke(new FileTransferReady(
                    uploadName,
                    uploadReady.Size,
                    uploadReady.TransferId,
                    uploadReady.ChunkSize));
                break;

            case ResponseMessageType.FileUploadEnd:
                if (!TryReadPayload(payload, out RespFileUploadEnd uploadEnd)) return;
                var uploadedName = ReadFixedString(uploadEnd.Name, ProtocolConstants.MaxFilenameBytes);
                EmitOk("FILE_UPLOAD_END", Kv(
                    ("NAME", uploadedName),
                    ("SIZE", uploadEnd.Size.ToString(CultureInfo.InvariantCulture)),
                    ("ID", uploadEnd.TransferId.ToString(CultureInfo.InvariantCulture))));
                UploadCompleteReceived?.Invoke(new FileTransferComplete(uploadEnd.TransferId, uploadedName, uploadEnd.Size));
                break;

            case ResponseMessageType.FileUploadAbort:
                if (!TryReadPayload(payload, out RespFileUploadAbort _)) return;
                EmitOk("FILE_UPLOAD_ABORT");
                UploadAbortedReceived?.Invoke();
                break;

            case ResponseMessageType.FileDownloadReady:
                if (!TryReadPayload(payload, out RespFileDownloadReady downloadReady)) return;
                var downloadName = ReadFixedString(downloadReady.Name, ProtocolConstants.MaxFilenameBytes);
                EmitOk("FILE_DOWNLOAD_READY", Kv(
                    ("NAME", downloadName),
                    ("SIZE", downloadReady.Size.ToString(CultureInfo.InvariantCulture)),
                    ("ID", downloadReady.TransferId.ToString(CultureInfo.InvariantCulture)),
                    ("CHUNK", downloadReady.ChunkSize.ToString(CultureInfo.InvariantCulture))));
                DownloadReadyReceived?.Invoke(new FileTransferReady(
                    downloadName,
                    downloadReady.Size,
                    downloadReady.TransferId,
                    downloadReady.ChunkSize));
                break;

            case ResponseMessageType.FileDownloadEnd:
                if (!TryReadPayload(payload, out RespFileDownloadEnd downloadEnd)) return;
                var downloadedName = ReadFixedString(downloadEnd.Name, ProtocolConstants.MaxFilenameBytes);
                var crc = downloadEnd.Crc32.ToString("x8", CultureInfo.InvariantCulture);
                EmitOk("FILE_DOWNLOAD_END", Kv(
                    ("NAME", downloadedName),
                    ("ID", downloadEnd.TransferId.ToString(CultureInfo.InvariantCulture)),
                    ("CRC", crc)));
                DownloadCompleteReceived?.Invoke(new FileDownloadComplete(downloadEnd.TransferId, downloadedName, crc));
                break;

            case ResponseMessageType.FileDownloadAbort:
                if (!TryReadPayload(payload, out RespFileDownloadAbort _)) return;
                EmitOk("FILE_DOWNLOAD_ABORT");
                DownloadAbortedReceived?.Invoke();
                break;

            case ResponseMessageType.StorageError:
                if (!TryReadPayload(payload, out RespStorageError storageError)) return;
                HandleBinaryStorageError(storageError);
                break;

            case ResponseMessageType.Wait:
                if (!TryReadPayload(payload, out RespWait wait)) return;
                WaitReceived?.Invoke(ReadFixedString(wait.Reason, ProtocolConstants.MaxReasonBytes));
                break;

            case ResponseMessageType.MachineSettings:
                if (!TryReadPayload(payload, out RespMachineSettings settings)) return;
                MachineSettingsReceived?.Invoke(new PicoMachineSettings(
                    settings.StepsPerMmX,
                    settings.StepsPerMmY,
                    settings.StepsPerMmZ,
                    settings.MaxFeedRateX,
                    settings.MaxFeedRateY,
                    settings.MaxFeedRateZ,
                    settings.AccelerationX,
                    settings.AccelerationY,
                    settings.AccelerationZ,
                    settings.MaxTravelX,
                    settings.MaxTravelY,
                    settings.MaxTravelZ,
                    settings.SoftLimitsEnabled != 0,
                    settings.HardLimitsEnabled != 0,
                    settings.SpindleMinRpm,
                    settings.SpindleMaxRpm,
                    settings.WarningTemperature,
                    settings.MaxTemperature));
                break;
        }
    }

    private unsafe void ParseEventFrame(byte[] payload)
    {
        if (payload.Length == 0)
            return;

        switch ((EventMessageType)payload[0])
        {
            case EventMessageType.State:
                if (!TryReadPayload(payload, out EventState state)) return;
                StateChanged?.Invoke(MapMachineState(state.State));
                break;

            case EventMessageType.Caps:
                if (!TryReadPayload(payload, out EventCaps caps)) return;
                CapsChanged?.Invoke(MapCaps(caps.Caps));
                break;

            case EventMessageType.Safety:
                if (!TryReadPayload(payload, out EventSafety safety)) return;
                SafetyChanged?.Invoke(MapSafety(safety.Safety));
                break;

            case EventMessageType.Job:
                if (!TryReadPayload(payload, out EventJob job)) return;
                JobChanged?.Invoke(job.HasJob != 0
                    ? ReadFixedString(job.Name, ProtocolConstants.MaxFilenameBytes)
                    : null);
                break;

            case EventMessageType.Pos:
                if (!TryReadPayload(payload, out EventPos pos)) return;
                PositionChanged?.Invoke(new PicoPos(pos.Mx, pos.My, pos.Mz, pos.Wx, pos.Wy, pos.Wz));
                break;

            case EventMessageType.StateChanged:
                if (!TryReadPayload(payload, out EventStateChanged changed)) return;
                EventReceived?.Invoke("STATE_CHANGED", Kv(("STATE", StateToken(changed.State))));
                break;

            case EventMessageType.JobProgress:
                if (!TryReadPayload(payload, out EventJobProgress progress)) return;
                EventReceived?.Invoke("JOB_PROGRESS", Kv(
                    ("LINE", progress.Line.ToString(CultureInfo.InvariantCulture)),
                    ("TOTAL", progress.Total.ToString(CultureInfo.InvariantCulture))));
                break;

            case EventMessageType.JobComplete:
                if (!TryReadPayload(payload, out EventJobComplete _)) return;
                EventReceived?.Invoke("JOB_COMPLETE", EmptyMetadata);
                break;

            case EventMessageType.JobError:
                if (!TryReadPayload(payload, out EventJobError jobError)) return;
                EventReceived?.Invoke("JOB_ERROR", Kv(("REASON", ReadFixedString(jobError.Reason, ProtocolConstants.MaxReasonBytes))));
                break;

            case EventMessageType.SdMounted:
                if (!TryReadPayload(payload, out EventSdMounted _)) return;
                EventReceived?.Invoke("SD_MOUNTED", EmptyMetadata);
                break;

            case EventMessageType.SdRemoved:
                if (!TryReadPayload(payload, out EventSdRemoved _)) return;
                EventReceived?.Invoke("SD_REMOVED", EmptyMetadata);
                break;

            case EventMessageType.TeensyConnected:
                if (!TryReadPayload(payload, out EventTeensyConnected _)) return;
                EventReceived?.Invoke("TEENSY_CONNECTED", EmptyMetadata);
                break;

            case EventMessageType.TeensyDisconnected:
                if (!TryReadPayload(payload, out EventTeensyDisconnected _)) return;
                EventReceived?.Invoke("TEENSY_DISCONNECTED", EmptyMetadata);
                break;

            case EventMessageType.EstopActive:
                if (!TryReadPayload(payload, out EventEstopActive _)) return;
                EventReceived?.Invoke("ESTOP_ACTIVE", EmptyMetadata);
                break;

            case EventMessageType.EstopCleared:
                if (!TryReadPayload(payload, out EventEstopCleared _)) return;
                EventReceived?.Invoke("ESTOP_CLEARED", EmptyMetadata);
                break;

            case EventMessageType.Limit:
                if (!TryReadPayload(payload, out EventLimit limit)) return;
                EventReceived?.Invoke("LIMIT", Kv(("AXIS", AxesToken(limit.AxesMask))));
                break;

            case EventMessageType.StorageUploadProfile:
                if (!TryReadPayload(payload, out EventStorageUploadProfile profile)) return;
                EventReceived?.Invoke("STORAGE_UPLOAD_PROFILE", Kv(
                    ("SIZE", profile.Size.ToString(CultureInfo.InvariantCulture)),
                    ("TOTAL_MS", profile.TotalMs.ToString(CultureInfo.InvariantCulture)),
                    ("PREALLOC_MS", profile.PreallocMs.ToString(CultureInfo.InvariantCulture)),
                    ("WRITE_MS", profile.WriteMs.ToString(CultureInfo.InvariantCulture)),
                    ("MAX_WRITE_MS", profile.MaxWriteMs.ToString(CultureInfo.InvariantCulture)),
                    ("CLOSE_MS", profile.CloseMs.ToString(CultureInfo.InvariantCulture)),
                    ("CHUNKS", profile.Chunks.ToString(CultureInfo.InvariantCulture)),
                    ("QUEUE_MAX", profile.QueueMax.ToString(CultureInfo.InvariantCulture)),
                    ("BPS", profile.Bps.ToString(CultureInfo.InvariantCulture))));
                break;

            case EventMessageType.StorageUploadChunkProfile:
                if (!TryReadPayload(payload, out EventStorageUploadChunkProfile chunkProfile)) return;
                EventReceived?.Invoke("STORAGE_UPLOAD_CHUNK_PROFILE", Kv(
                    ("SEQ", chunkProfile.Seq.ToString(CultureInfo.InvariantCulture)),
                    ("BYTES", chunkProfile.Bytes.ToString(CultureInfo.InvariantCulture)),
                    ("TOTAL_MS", chunkProfile.TotalMs.ToString(CultureInfo.InvariantCulture)),
                    ("WRITE_MS", chunkProfile.WriteMs.ToString(CultureInfo.InvariantCulture)),
                    ("LAST_WRITE_MS", chunkProfile.LastWriteMs.ToString(CultureInfo.InvariantCulture)),
                    ("MAX_WRITE_MS", chunkProfile.MaxWriteMs.ToString(CultureInfo.InvariantCulture)),
                    ("CHUNKS", chunkProfile.Chunks.ToString(CultureInfo.InvariantCulture)),
                    ("QUEUE", chunkProfile.Queue.ToString(CultureInfo.InvariantCulture)),
                    ("QUEUE_MAX", chunkProfile.QueueMax.ToString(CultureInfo.InvariantCulture)),
                    ("BPS", chunkProfile.Bps.ToString(CultureInfo.InvariantCulture))));
                break;

            case EventMessageType.Alarm:
                if (!TryReadPayload(payload, out EventAlarm alarm)) return;
                EventReceived?.Invoke("ALARM", Kv(
                    ("CODE", alarm.Code.ToString(CultureInfo.InvariantCulture)),
                    ("MSG", ReadFixedString(alarm.Message, ProtocolConstants.MaxMessageBytes))));
                break;
        }
    }

    private void EmitOk(string token, IReadOnlyDictionary<string, string>? metadata = null)
        => OkReceived?.Invoke(token, metadata ?? EmptyMetadata);

    private void HandleBinaryError(ProtocolErrorCode code, string reason)
    {
        var token = string.IsNullOrWhiteSpace(reason) ? ErrorToken(code) : reason;

        switch (code)
        {
            case ProtocolErrorCode.UploadFileExists:
                UploadFileExistsReceived?.Invoke(token);
                return;

            case ProtocolErrorCode.UploadMissingParam:
                UploadFailedReceived?.Invoke(token);
                ErrorReceived?.Invoke(token);
                return;

            case ProtocolErrorCode.DownloadMissingParam:
                DownloadFailedReceived?.Invoke(token);
                ErrorReceived?.Invoke(token);
                return;
        }

        if (token.StartsWith("UPLOAD_", StringComparison.OrdinalIgnoreCase))
            UploadFailedReceived?.Invoke(token);

        if (token.StartsWith("DOWNLOAD_", StringComparison.OrdinalIgnoreCase))
            DownloadFailedReceived?.Invoke(token);

        ErrorReceived?.Invoke(token);
    }

    private unsafe void HandleBinaryStorageError(RespStorageError error)
    {
        var code = (ProtocolErrorCode)error.Error;
        var operation = (ProtocolStorageOperation)error.Operation;
        var token = ErrorToken(code);
        var detail = ReadFixedString(error.Detail, ProtocolConstants.MaxReasonBytes);
        var message = FormatStorageErrorDetail(token, detail);

        if (operation == ProtocolStorageOperation.Upload)
        {
            UploadFailedReceived?.Invoke(code == ProtocolErrorCode.StorageNoSd
                ? "SD card not present"
                : FriendlyStorageMessage(code, detail, message));
            if (code == ProtocolErrorCode.StorageBusy)
                return;
        }
        else if (operation == ProtocolStorageOperation.Download)
        {
            DownloadFailedReceived?.Invoke(FriendlyStorageMessage(code, detail, message));
        }

        ErrorReceived?.Invoke(message);
    }

    private static string FriendlyStorageMessage(ProtocolErrorCode code, string detail, string fallback)
    {
        if (code == ProtocolErrorCode.StorageNoSpace &&
            TryGetMetadataValue(detail, "FREE", out var freeText) &&
            long.TryParse(freeText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var freeBytes))
        {
            return freeBytes > 0
                ? $"SD card full ({freeBytes / 1024} KB free)"
                : "SD card full";
        }

        return fallback;
    }

    private static string FormatStorageErrorDetail(string token, string detail)
    {
        if (string.IsNullOrWhiteSpace(detail) || string.Equals(detail, token, StringComparison.OrdinalIgnoreCase))
            return token;

        if (detail.Contains('=', StringComparison.Ordinal))
            return $"{token} {detail}";

        return detail;
    }

    private static bool TryGetMetadataValue(string text, string key, out string value)
    {
        foreach (var part in text.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            var eq = part.IndexOf('=');
            if (eq <= 0)
                continue;

            if (string.Equals(part[..eq], key, StringComparison.OrdinalIgnoreCase))
            {
                value = DecodeValue(part[(eq + 1)..]);
                return true;
            }
        }

        value = "";
        return false;
    }

    private static unsafe bool TryReadPayload<T>(byte[] payload, out T value) where T : unmanaged
    {
        if (payload.Length != sizeof(T))
        {
            value = default;
            return false;
        }

        value = MemoryMarshal.Read<T>(payload);
        return true;
    }

    private static unsafe string ReadFixedString(byte* source, int capacity)
    {
        if (source == null || capacity <= 0)
            return "";

        var bytes = new ReadOnlySpan<byte>(source, capacity);
        var length = bytes.IndexOf((byte)0);
        if (length < 0)
            length = capacity;
        return Encoding.UTF8.GetString(bytes[..length]);
    }

    private static IReadOnlyDictionary<string, string> Kv(params (string Key, string Value)[] values)
    {
        if (values.Length == 0)
            return EmptyMetadata;

        var dict = new Dictionary<string, string>(values.Length, StringComparer.OrdinalIgnoreCase);
        foreach (var (key, value) in values)
            dict[key] = value;
        return dict;
    }

    private static MachineOperationState MapMachineState(byte state) => (ProtocolMachineState)state switch
    {
        ProtocolMachineState.Booting            => MachineOperationState.Booting,
        ProtocolMachineState.Syncing            => MachineOperationState.Syncing,
        ProtocolMachineState.TeensyDisconnected => MachineOperationState.TeensyDisconnected,
        ProtocolMachineState.Idle               => MachineOperationState.Idle,
        ProtocolMachineState.Homing             => MachineOperationState.Homing,
        ProtocolMachineState.Jog                => MachineOperationState.Jog,
        ProtocolMachineState.Starting           => MachineOperationState.Starting,
        ProtocolMachineState.Running            => MachineOperationState.Running,
        ProtocolMachineState.Hold               => MachineOperationState.Hold,
        ProtocolMachineState.Fault              => MachineOperationState.Fault,
        ProtocolMachineState.Estop              => MachineOperationState.Estop,
        ProtocolMachineState.CommsFault         => MachineOperationState.CommsFault,
        ProtocolMachineState.Uploading          => MachineOperationState.Uploading,
        _                                       => MachineOperationState.Booting
    };

    private static string StateToken(byte state) => (ProtocolMachineState)state switch
    {
        ProtocolMachineState.Booting            => "BOOTING",
        ProtocolMachineState.Syncing            => "SYNCING",
        ProtocolMachineState.TeensyDisconnected => "TEENSY_DISCONNECTED",
        ProtocolMachineState.Idle               => "IDLE",
        ProtocolMachineState.Homing             => "HOMING",
        ProtocolMachineState.Jog                => "JOG",
        ProtocolMachineState.Starting           => "STARTING",
        ProtocolMachineState.Running            => "RUNNING",
        ProtocolMachineState.Hold               => "HOLD",
        ProtocolMachineState.Fault              => "FAULT",
        ProtocolMachineState.Estop              => "ESTOP",
        ProtocolMachineState.CommsFault         => "COMMS_FAULT",
        ProtocolMachineState.Uploading          => "UPLOADING",
        _                                       => "UNKNOWN"
    };

    private static SafetyLevel MapSafety(byte safety) => (ProtocolSafetyLevel)safety switch
    {
        ProtocolSafetyLevel.Safe       => SafetyLevel.Safe,
        ProtocolSafetyLevel.Monitoring => SafetyLevel.Monitoring,
        ProtocolSafetyLevel.Warning    => SafetyLevel.Warning,
        ProtocolSafetyLevel.Critical   => SafetyLevel.Critical,
        _                              => SafetyLevel.Safe
    };

    private static CapsFlags MapCaps(ushort caps)
    {
        var flags = (CapabilityFlags)caps;
        return new CapsFlags(
            Motion:    flags.HasFlag(CapabilityFlags.Motion),
            Probe:     flags.HasFlag(CapabilityFlags.Probe),
            Spindle:   flags.HasFlag(CapabilityFlags.Spindle),
            FileLoad:  flags.HasFlag(CapabilityFlags.FileLoad),
            JobStart:  flags.HasFlag(CapabilityFlags.JobStart),
            JobPause:  flags.HasFlag(CapabilityFlags.JobPause),
            JobResume: flags.HasFlag(CapabilityFlags.JobResume),
            JobAbort:  flags.HasFlag(CapabilityFlags.JobAbort),
            Overrides: flags.HasFlag(CapabilityFlags.Overrides),
            Reset:     flags.HasFlag(CapabilityFlags.Reset));
    }

    private static string OkTokenFromCommandType(byte commandType) => (CommandMessageType)commandType switch
    {
        CommandMessageType.Ping              => "PONG",
        CommandMessageType.Home              => "HOME",
        CommandMessageType.Jog               => "JOG",
        CommandMessageType.JogCancel         => "JOG_CANCEL",
        CommandMessageType.Zero              => "ZERO",
        CommandMessageType.Start             => "START",
        CommandMessageType.Pause             => "PAUSE",
        CommandMessageType.Resume            => "RESUME",
        CommandMessageType.Abort             => "ABORT",
        CommandMessageType.Estop             => "ESTOP",
        CommandMessageType.Reset             => "RESET",
        CommandMessageType.SpindleOn         => "SPINDLE_ON",
        CommandMessageType.SpindleOff        => "SPINDLE_OFF",
        CommandMessageType.Override          => "OVERRIDE",
        CommandMessageType.SettingsSet       => "SETTINGS_SET",
        CommandMessageType.FileUnload        => "FILE_UNLOAD",
        CommandMessageType.FileUploadAbort   => "FILE_UPLOAD_ABORT",
        CommandMessageType.FileDownloadAbort => "FILE_DOWNLOAD_ABORT",
        _                                    => ""
    };

    private static string ErrorToken(ProtocolErrorCode code) => code switch
    {
        ProtocolErrorCode.None                   => "NONE",
        ProtocolErrorCode.InvalidState           => "INVALID_STATE",
        ProtocolErrorCode.MissingParam           => "MISSING_PARAM",
        ProtocolErrorCode.NoJobLoaded            => "NO_JOB_LOADED",
        ProtocolErrorCode.UploadFileExists       => "UPLOAD_FILE_EXISTS",
        ProtocolErrorCode.StorageBusy            => "STORAGE_BUSY",
        ProtocolErrorCode.StorageNotAllowed      => "STORAGE_NOT_ALLOWED",
        ProtocolErrorCode.StorageNoSd            => "STORAGE_NO_SD",
        ProtocolErrorCode.StorageFileNotFound    => "STORAGE_FILE_NOT_FOUND",
        ProtocolErrorCode.StorageInvalidFilename => "STORAGE_INVALID_FILENAME",
        ProtocolErrorCode.StorageInvalidSession  => "STORAGE_INVALID_SESSION",
        ProtocolErrorCode.StorageBadSequence     => "STORAGE_BAD_SEQUENCE",
        ProtocolErrorCode.StorageSizeMismatch    => "STORAGE_SIZE_MISMATCH",
        ProtocolErrorCode.StorageCrcFail         => "STORAGE_CRC_FAIL",
        ProtocolErrorCode.StorageReadFail        => "STORAGE_READ_FAIL",
        ProtocolErrorCode.StorageWriteFail       => "STORAGE_WRITE_FAIL",
        ProtocolErrorCode.StorageNoSpace         => "STORAGE_NO_SPACE",
        ProtocolErrorCode.StorageAborted         => "STORAGE_ABORTED",
        ProtocolErrorCode.DownloadMissingParam   => "DOWNLOAD_MISSING_PARAM",
        ProtocolErrorCode.UploadMissingParam     => "UPLOAD_MISSING_PARAM",
        _                                        => "UNKNOWN"
    };

    private static string AxesToken(byte axesMask)
    {
        var axes = new StringBuilder(3);
        if ((axesMask & (byte)AxesMask.X) != 0) axes.Append('X');
        if ((axesMask & (byte)AxesMask.Y) != 0) axes.Append('Y');
        if ((axesMask & (byte)AxesMask.Z) != 0) axes.Append('Z');
        return axes.ToString();
    }

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

    private static long ToLongClamped(ulong value)
        => value > long.MaxValue ? long.MaxValue : (long)value;

    private void SendOverride(OverrideTarget target, int percent)
        => SendCommandFrame(new CmdOverride
        {
            MessageType = (byte)CommandMessageType.Override,
            Target = (byte)target,
            Percent = ToUInt16Clamped(percent)
        });

    private unsafe void SendCommandFrame<T>(in T payload) where T : unmanaged
    {
        Span<byte> bytes = stackalloc byte[sizeof(T)];
        MemoryMarshal.Write(bytes, in payload);
        _serial.SendFrame(CommandFrameType, ProtocolConstants.TransferIdNone, NextRequestSequence(), bytes);
    }

    private uint NextRequestSequence()
    {
        uint seq = _nextRequestSequence++;
        if (_nextRequestSequence == 0)
            _nextRequestSequence = 1;
        return seq;
    }

    private static byte AxisToProtocol(char axis) => char.ToUpperInvariant(axis) switch
    {
        'X' => (byte)AxisId.X,
        'Y' => (byte)AxisId.Y,
        'Z' => (byte)AxisId.Z,
        _ => throw new ArgumentOutOfRangeException(nameof(axis), axis, "Axis must be X, Y, or Z.")
    };

    private static AxesMask AxesToProtocol(string axes) => axes.Trim().ToUpperInvariant() switch
    {
        "X" => AxesMask.X,
        "Y" => AxesMask.Y,
        "Z" => AxesMask.Z,
        "ALL" => AxesMask.All,
        _ => throw new ArgumentOutOfRangeException(nameof(axes), axes, "Axes must be X, Y, Z, or ALL.")
    };

    private static ushort ToUInt16Clamped(float value)
        => ToUInt16Clamped((int)MathF.Round(value));

    private static ushort ToUInt16Clamped(int value)
        => (ushort)Math.Clamp(value, ushort.MinValue, ushort.MaxValue);

    private static unsafe void WriteFixedString(byte* destination, int capacity, string value)
    {
        var buffer = new Span<byte>(destination, capacity);
        buffer.Clear();
        if (capacity <= 1 || string.IsNullOrEmpty(value))
            return;

        byte[] encoded = Encoding.UTF8.GetBytes(value);
        encoded.AsSpan(0, Math.Min(encoded.Length, capacity - 1)).CopyTo(buffer);
    }

    private static uint ReadUInt32LittleEndian(byte[] buffer, int offset)
        => (uint)(buffer[offset]
                | (buffer[offset + 1] << 8)
                | (buffer[offset + 2] << 16)
                | (buffer[offset + 3] << 24));
}
