using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using PortableCncApp.Services;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.ViewModels;

public sealed class FilesViewModel : PageViewModelBase
{
    private const int PreviewMaxLines = 200;

    // SD transfer protocol is stop-and-wait in both directions:
    // - upload/download are session-based
    // - binary frames carry chunk data and chunk acks
    // - text control lines only start/end/abort the transfer
    // Keep one shared chunk size and timeout policy for both flows.
    private static readonly int TransferRawChunkSize = PicoProtocolService.BinaryTransferChunkSize;
    private static readonly TimeSpan TransferInitTimeout = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan TransferChunkTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan TransferFinalizeTimeout = TimeSpan.FromSeconds(10);
    private const int TransferMaxRetries = 3;

    // ── Preview fields ────────────────────────────────────────────────────────
    private CancellationTokenSource? _parseCancellation;
    private bool   _toolpathHasGeometry;
    private bool   _toolpathHasError;
    private int    _previewLineCount;
    private bool   _isParsingToolpath;
    private string _toolpathStatusMessage = "Use 'Preview local file' to inspect a file.";
    private string _toolpathWarningSummary = "";
    private string _globalWarningSummary   = "";
    private string? _previewDisplayName;
    private bool   _isLocalPreviewFile;
    private string? _localPreviewPath;
    private string? _downloadPreviewPath;

    // ── SD-list fields ────────────────────────────────────────────────────────
    private readonly List<GCodeFileInfo> _pendingFileList = new();
    private readonly SemaphoreSlim _storageOperationGate = new(1, 1);
    private GCodeFileInfo? _selectedFile;
    private long    _sdFreeBytes = -1;

    // ── Delete tracking ───────────────────────────────────────────────────────
    private readonly Dictionary<string, GCodeFileInfo> _pendingDeletes = new();

    // ── Upload fields ─────────────────────────────────────────────────────────
    private bool   _isUploading;
    private double _uploadProgress;
    private string _uploadStatusText = "";
    private string? _uploadFileExistsName;
    private CancellationTokenSource?        _uploadCancellation;
    private Channel<UploadAck>?             _uploadChannel;
    private TaskCompletionSource<bool>?     _overwriteTcs;

    // Download preview flow
    private CancellationTokenSource? _previewDownloadCancellation;
    private Channel<DownloadPacket>? _downloadChannel;
    private TaskCompletionSource<bool>? _replaceLoadTcs;

    // Upload ACK discriminated union
    private enum UploadAckType { Ready, ChunkOk, Complete, Aborted, Failed, FileExists }
    private record UploadAck(UploadAckType Type, uint Seq = 0, string Name = "",
                              long Size = 0, string Reason = "", byte TransferId = 0, int ChunkSize = 0,
                              uint BytesCommitted = 0);

    private enum DownloadPacketType { Ready, Chunk, Complete, Failed }
    private record DownloadPacket(DownloadPacketType Type, uint Seq = 0, string Name = "", long Size = 0, byte[]? Data = null, string Reason = "", byte TransferId = 0, int ChunkSize = 0);

    // ─────────────────────────────────────────────────────────────────────────
    // Constructor
    // ─────────────────────────────────────────────────────────────────────────

    public FilesViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;
        Files.CollectionChanged += (_, _) =>
        {
            RaisePropertyChanged(nameof(HasNoFiles));
            RaiseFileStateCardProperties();
        };

        UploadCommand             = new RelayCommand(StartUpload,            CanUpload);
        CancelUploadCommand       = new RelayCommand(CancelUpload,           () => IsUploading);
        PreviewLocalCommand       = new RelayCommand(PreviewLocalFile,       () => !IsUploading);
        UploadLocalPreviewCommand = new RelayCommand(StartUploadLocalPreview, CanUploadLocalPreview);
        RefreshCommand            = new RelayCommand(RefreshFileList,        () => !IsUploading && !IsPreviewDownloadActive);
        LoadSelectedFileCommand   = new RelayCommand(LoadSelectedFile,       CanLoadSelectedFile);
        UnloadLoadedFileCommand   = new RelayCommand(UnloadLoadedFile,       CanUnloadLoadedFile);
        DeleteCommand             = new RelayCommand<GCodeFileInfo>(DeleteFile);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Events
    // ─────────────────────────────────────────────────────────────────────────

    public event EventHandler<ParseErrorDialogRequest>? ParseErrorDialogRequested;

    /// <summary>
    /// Raised when the Pico reports FILE_EXISTS during upload.
    /// View shows confirm dialog; call ConfirmOverwrite() or CancelOverwrite() in response.
    /// </summary>
    public event EventHandler<string>? UploadFileExistsRequested;
    public event EventHandler<LoadReplaceDialogRequest>? LoadReplaceRequested;

    // ─────────────────────────────────────────────────────────────────────────
    // Collections
    // ─────────────────────────────────────────────────────────────────────────

    public ObservableCollection<GCodeFileInfo>    Files            { get; } = new();
    public ObservableCollection<GCodeWarningInfo> ToolpathWarnings { get; } = new();
    public ObservableCollection<GCodePreviewLine> PreviewLines     { get; } = new();

    // ─────────────────────────────────────────────────────────────────────────
    // SD card
    // ─────────────────────────────────────────────────────────────────────────

    public long SdFreeBytes
    {
        get => _sdFreeBytes;
        private set
        {
            if (SetProperty(ref _sdFreeBytes, value))
                RaisePropertyChanged(nameof(SdFreeSummary));
        }
    }

    public string SdFreeSummary => _sdFreeBytes < 0 ? "" : $"{FormatSize(_sdFreeBytes)} free";

    public GCodeFileInfo? SelectedFile
    {
        get => _selectedFile;
        set
        {
            if (!SetProperty(ref _selectedFile, value)) return;

            RaisePropertyChanged(nameof(HasSelectedFile));
            RaiseFileStateCardProperties();
            RaiseCanExecuteAll();

            if (value != null && MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                _ = BeginSelectedFilePreviewAsync(value);
        }
    }

    public bool HasSelectedFile => SelectedFile != null;
    public bool HasNoFiles      => Files.Count == 0;
    public bool HasLoadedJob    => !string.IsNullOrWhiteSpace(MainVm?.CurrentFileName);
    public bool HasSelectedPreview => HasSelectedFile || HasLocalPreview;
    public bool HasLocalPreview
        => IsLocalPreviewFile &&
           !string.IsNullOrWhiteSpace(_previewDisplayName) &&
           !string.IsNullOrWhiteSpace(_localPreviewPath);
    public bool IsSelectedFileLoaded
        => SelectedFile != null &&
           !string.IsNullOrWhiteSpace(MainVm?.CurrentFileName) &&
           string.Equals(SelectedFile.Name, MainVm.CurrentFileName, StringComparison.OrdinalIgnoreCase);

    public bool ShowSelectedPreviewCard => HasLocalPreview || (HasSelectedFile && !IsSelectedFileLoaded);
    public bool ShowLoadedJobCard => HasLoadedJob;

    public string? SelectedPreviewFileName => HasLocalPreview ? GetLocalPreviewName() : SelectedFile?.Name;
    public string? SelectedPreviewSizeText => HasLocalPreview ? GetLocalPreviewSizeText() : SelectedFile?.Size;
    public string SelectedPreviewLinesText => TryGetDocumentLineSummary(SelectedPreviewFileName);
    public string? LoadedJobFileName => MainVm?.CurrentFileName;
    public string LoadedJobSizeText => ResolveLoadedJobInfo()?.Size ?? "--";
    public string LoadedJobLinesText => TryGetDocumentLineSummary(MainVm?.CurrentFileName);
    public string SelectedPreviewHeaderText => HasLocalPreview
        ? "Local file. Upload it to the device before it can be loaded."
        : "Inspect only. This file is not armed for the machine.";
    public string LoadedJobHeaderText => "Machine-ready job. Start will run this file.";
    public string SelectedPreviewStatusText => HasLocalPreview ? "LOCAL" : "PREVIEW";
    public IBrush SelectedPreviewStatusBrush => HasLocalPreview
        ? ThemeResources.Brush("WarningBrush", "#E0A100")
        : ThemeResources.Brush("InfoBrush", "#5B9BD5");
    public string SelectedPreviewPrimaryActionText => HasLocalPreview ? "Upload to Device" : "Load for Job";
    public ICommand SelectedPreviewPrimaryActionCommand => HasLocalPreview
        ? UploadLocalPreviewCommand
        : LoadSelectedFileCommand;
    public string LoadedJobStatusText => "LOADED";
    public IBrush LoadedJobStatusBrush => ThemeResources.Brush("SuccessBrush", "#3BB273");

    // ─────────────────────────────────────────────────────────────────────────
    // Upload state
    // ─────────────────────────────────────────────────────────────────────────

    public bool IsUploading
    {
        get => _isUploading;
        private set
        {
            if (SetProperty(ref _isUploading, value))
                RaiseCanExecuteAll();
        }
    }

    public bool IsPreviewDownloadActive => _previewDownloadCancellation != null;

    public double UploadProgress
    {
        get => _uploadProgress;
        private set => SetProperty(ref _uploadProgress, value);
    }

    public string UploadStatusText
    {
        get => _uploadStatusText;
        private set => SetProperty(ref _uploadStatusText, value);
    }

    public string? UploadFileExistsName
    {
        get => _uploadFileExistsName;
        private set => SetProperty(ref _uploadFileExistsName, value);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Local preview state
    // ─────────────────────────────────────────────────────────────────────────

    public bool IsLocalPreviewFile
    {
        get => _isLocalPreviewFile;
        private set
        {
            if (SetProperty(ref _isLocalPreviewFile, value))
            {
                RaisePropertyChanged(nameof(ShowLocalPreviewBanner));
                RaisePropertyChanged(nameof(FilePreviewTitle));
                RaisePropertyChanged(nameof(PreviewEmptyMessage));
                RaisePropertyChanged(nameof(ToolpathStateLabel));
                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
            }
        }
    }

    /// <summary>True when a local file is loaded for preview and the machine is connected (upload is possible).</summary>
    public bool ShowLocalPreviewBanner
        => IsLocalPreviewFile && MainVm?.PiConnectionStatus == ConnectionStatus.Connected;

    // ─────────────────────────────────────────────────────────────────────────
    // Toolpath / preview display properties
    // ─────────────────────────────────────────────────────────────────────────

    public int PreviewLineCount
    {
        get => _previewLineCount;
        set
        {
            if (SetProperty(ref _previewLineCount, value))
            {
                RaisePropertyChanged(nameof(SelectedFileLineSummary));
                RaisePropertyChanged(nameof(PreviewViewportSummary));
            }
        }
    }

    public bool IsParsingToolpath
    {
        get => _isParsingToolpath;
        set
        {
            if (SetProperty(ref _isParsingToolpath, value))
            {
                RaisePropertyChanged(nameof(ToolpathStateLabel));
                RaisePropertyChanged(nameof(ToolpathStateBrush));
            }
        }
    }

    public string ToolpathStatusMessage
    {
        get => _toolpathStatusMessage;
        set => SetProperty(ref _toolpathStatusMessage, value);
    }

    public string ToolpathWarningSummary
    {
        get => _toolpathWarningSummary;
        set
        {
            if (SetProperty(ref _toolpathWarningSummary, value))
                RaisePropertyChanged(nameof(WarningDetail));
        }
    }

    public string FilePreviewTitle => _previewDisplayName ?? "No preview loaded";

    public bool HasPreviewContent => PreviewLines.Count > 0;
    public bool IsPreviewEmpty    => !HasPreviewContent;

    public string PreviewEmptyMessage
        => _previewDisplayName != null ? "No source lines to display." : "Select a device file or preview a local file.";

    public string SelectedFileLineSummary
        => PreviewLineCount > 0 ? $"{PreviewLineCount} total lines" : "--";

    public string PreviewViewportSummary
    {
        get
        {
            if (!IsLocalPreviewFile) return "Load a local file to inspect its source.";
            if (PreviewLineCount <= 0) return "Source preview unavailable.";
            return PreviewLineCount > PreviewMaxLines
                ? $"Showing first {PreviewMaxLines} of {PreviewLineCount} lines"
                : $"{PreviewLineCount} line{(PreviewLineCount == 1 ? "" : "s")} shown";
        }
    }

    public string ToolpathStateLabel
    {
        get
        {
            if (!IsLocalPreviewFile && MainVm?.ActiveGCodeDocument == null) return "NO FILE";
            if (IsParsingToolpath)  return "PARSING";
            if (_toolpathHasError)  return "ERROR";
            return _toolpathHasGeometry ? "READY" : "NO PATH";
        }
    }

    public IBrush ToolpathStateBrush => ToolpathStateLabel switch
    {
        "READY"   => ThemeResources.Brush("SuccessBrush",      "#3BB273"),
        "PARSING" => ThemeResources.Brush("InfoBrush",         "#5B9BD5"),
        "ERROR"   => ThemeResources.Brush("DangerBrush",       "#D83B3B"),
        "NO PATH" => ThemeResources.Brush("WarningBrush",      "#E0A100"),
        _         => ThemeResources.Brush("NeutralStateBrush", "#808080")
    };

    public string WarningDetail
    {
        get
        {
            if (ToolpathWarnings.Count == 0) return "No parse warnings.";
            var highlighted = ToolpathWarnings.Count(w => w.LineNumber > 0 && w.IsVisibleInPreview);
            var hidden      = ToolpathWarnings.Count(w => w.LineNumber > 0 && !w.IsVisibleInPreview);
            var global      = ToolpathWarnings.Count(w => w.LineNumber <= 0);
            var parts = new List<string> { ToolpathWarningSummary };
            if (highlighted > 0) parts.Add("Click warning icons in the source preview to inspect them.");
            if (hidden > 0)      parts.Add($"{hidden} warning{(hidden == 1 ? "" : "s")} fall outside the visible preview.");
            if (global > 0)      parts.Add($"{global} warning{(global == 1 ? "" : "s")} are file-wide.");
            return string.Join(" ", parts);
        }
    }

    public bool   HasGlobalWarnings => !string.IsNullOrWhiteSpace(GlobalWarningSummary);
    public string GlobalWarningSummary
    {
        get => _globalWarningSummary;
        private set => SetProperty(ref _globalWarningSummary, value);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Commands
    // ─────────────────────────────────────────────────────────────────────────

    public ICommand UploadCommand             { get; }
    public ICommand CancelUploadCommand       { get; }
    public ICommand PreviewLocalCommand       { get; }
    public ICommand UploadLocalPreviewCommand { get; }
    public ICommand RefreshCommand            { get; }
    public ICommand LoadSelectedFileCommand   { get; }
    public ICommand UnloadLoadedFileCommand   { get; }
    public ICommand DeleteCommand             { get; }

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    protected override void OnMainViewModelSet()
    {
        if (MainVm == null) return;

        // File list
        MainVm.Protocol.FileListEntryReceived += HandleFileListEntry;
        MainVm.Protocol.FileListEndReceived   += HandleFileListEnd;
        MainVm.Protocol.EventReceived         += HandleProtocolEvent;

        // Upload ACKs — all routed into the channel so DoUploadAsync can await them
        MainVm.Protocol.UploadReadyReceived      += ready        => RouteUpload(new UploadAck(UploadAckType.Ready, Name: ready.Name, Size: ready.Size, TransferId: ready.TransferId, ChunkSize: ready.ChunkSize));
        MainVm.Protocol.UploadChunkAckReceived   += (transferId, seq, bytesCommitted) => RouteUpload(new UploadAck(UploadAckType.ChunkOk, Seq: seq, TransferId: transferId, BytesCommitted: bytesCommitted));
        MainVm.Protocol.UploadCompleteReceived   += complete     => RouteUpload(new UploadAck(UploadAckType.Complete,  Name: complete.Name, Size: complete.Size, TransferId: complete.TransferId));
        MainVm.Protocol.UploadAbortedReceived    += ()           => RouteUpload(new UploadAck(UploadAckType.Aborted));
        MainVm.Protocol.UploadFailedReceived     += reason       => RouteUpload(new UploadAck(UploadAckType.Failed,    Reason: reason));
        MainVm.Protocol.UploadFileExistsReceived += name         => RouteUpload(new UploadAck(UploadAckType.FileExists, Name: name));

        // Download packets for SD-file preview
        MainVm.Protocol.DownloadReadyReceived    += ready        => RouteDownload(new DownloadPacket(DownloadPacketType.Ready, Name: ready.Name, Size: ready.Size, TransferId: ready.TransferId, ChunkSize: ready.ChunkSize));
        MainVm.Protocol.DownloadChunkReceived    += (transferId, seq, data)  => RouteDownload(new DownloadPacket(DownloadPacketType.Chunk, Seq: seq, Data: data, TransferId: transferId));
        MainVm.Protocol.DownloadCompleteReceived += complete     => RouteDownload(new DownloadPacket(DownloadPacketType.Complete, Name: complete.Name, Reason: complete.Crc32Hex, TransferId: complete.TransferId));
        MainVm.Protocol.DownloadAbortedReceived  += ()           => RouteDownload(new DownloadPacket(DownloadPacketType.Failed, Reason: "DOWNLOAD_ABORTED"));
        MainVm.Protocol.DownloadFailedReceived   += reason       => RouteDownload(new DownloadPacket(DownloadPacketType.Failed, Reason: reason));

        // File operation confirmations
        MainVm.Protocol.FileDeleteConfirmed += HandleFileDeleteConfirmed;
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        switch (propertyName)
        {
            case nameof(MainWindowViewModel.PiConnectionStatus):
            {
                var status = MainVm?.PiConnectionStatus;

                if (status == ConnectionStatus.Connected)
                {
                    _ = RequestFileListAsync();
                }
                else
                {
                    if (IsUploading)
                        _uploadCancellation?.Cancel(); // USB disconnect mid-upload — no abort command, just cancel

                    ClearDeviceFileStateOnDisconnect();
                }

                RaisePropertyChanged(nameof(ShowLocalPreviewBanner));
                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
                break;
            }

            case nameof(MainWindowViewModel.CurrentFileName):
            case nameof(MainWindowViewModel.TotalLines):
            case nameof(MainWindowViewModel.ActiveGCodeDocument):
            case nameof(MainWindowViewModel.Caps):
                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
                break;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // File list (SD card)
    // ─────────────────────────────────────────────────────────────────────────

    private async Task RequestFileListAsync()
    {
        if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
            return;

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            _pendingFileList.Clear();
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_LIST_END",
                MainVm.Protocol.SendFileList,
                TimeSpan.FromSeconds(5),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "File list refresh failed");
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private void HandleFileListEntry(string name, long sizeBytes)
    {
        _pendingFileList.Add(new GCodeFileInfo
        {
            Name     = name,
            FullPath = string.Empty,
            Size     = FormatSize(sizeBytes),
            Modified = string.Empty
        });
    }

    private void HandleFileListEnd(int count, long freeBytes)
    {
        var previousSelectionName = SelectedFile?.Name;
        Files.Clear();
        foreach (var entry in _pendingFileList)
            Files.Add(entry);
        _pendingFileList.Clear();
        SdFreeBytes = freeBytes;
        SelectedFile = previousSelectionName == null
            ? null
            : Files.FirstOrDefault(file => string.Equals(file.Name, previousSelectionName, StringComparison.OrdinalIgnoreCase));
        RaiseFileStateCardProperties();
        // Toolpath preview is independent — do not reset it here.
    }

    private void HandleProtocolEvent(string name, IReadOnlyDictionary<string, string> metadata)
    {
        switch (name)
        {
            case "SD_MOUNTED":
                _ = RequestFileListAsync();
                break;

            case "SD_REMOVED":
                Files.Clear();
                SelectedFile = null;
                SdFreeBytes  = -1;
                RaiseFileStateCardProperties();
                break;
        }
    }

    private void RefreshFileList()
    {
        if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
            _ = RequestFileListAsync();
    }

    private bool CanLoadSelectedFile()
        => !IsUploading &&
           !IsPreviewDownloadActive &&
           MainVm?.PiConnectionStatus == ConnectionStatus.Connected &&
           SelectedFile != null &&
           !IsSelectedFileLoaded;

    private async void LoadSelectedFile()
    {
        if (!CanLoadSelectedFile() || SelectedFile == null || MainVm == null)
            return;

        if (!string.IsNullOrWhiteSpace(MainVm.CurrentFileName))
        {
            if (LoadReplaceRequested == null)
                return;

            _replaceLoadTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            LoadReplaceRequested?.Invoke(this, new LoadReplaceDialogRequest(MainVm.CurrentFileName!, SelectedFile.Name));
            var shouldReplace = await _replaceLoadTcs.Task;
            _replaceLoadTcs = null;
            if (!shouldReplace)
                return;
        }

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            MainVm.StatusMessage = $"Loading: {SelectedFile.Name}";
            MainVm.IsStatusError = false;
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_LOAD",
                () => MainVm.Protocol.SendFileLoad(SelectedFile.Name),
                TimeSpan.FromSeconds(3),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "Load failed");
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private bool CanUnloadLoadedFile()
        => !IsUploading &&
           !IsPreviewDownloadActive &&
           MainVm?.PiConnectionStatus == ConnectionStatus.Connected &&
           !string.IsNullOrWhiteSpace(MainVm.CurrentFileName);

    private async void UnloadLoadedFile()
    {
        if (!CanUnloadLoadedFile() || MainVm == null)
            return;

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            MainVm.StatusMessage = "Unloading active job";
            MainVm.IsStatusError = false;
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_UNLOAD",
                MainVm.Protocol.SendFileUnload,
                TimeSpan.FromSeconds(3),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "Unload failed");
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private async void DeleteFile(GCodeFileInfo? file)
    {
        if (file == null || MainVm == null) return;
        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            _pendingDeletes[file.Name] = file;
            if (SelectedFile == file)
                SelectedFile = null;
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_DELETE",
                () => MainVm.Protocol.SendFileDelete(file.Name),
                TimeSpan.FromSeconds(3),
                disconnectOnTimeout: false);
            if (!result.Success)
            {
                _pendingDeletes.Remove(file.Name);
                MainVm.ApplyCommandResult(result, "Delete failed");
            }
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private void HandleFileDeleteConfirmed(string name)
    {
        if (_pendingDeletes.TryGetValue(name, out var file))
        {
            _pendingDeletes.Remove(name);
            Files.Remove(file);
        }
    }

    private async Task<string?> DownloadSelectedFileAsync(string fileName)
    {
        if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
            return null;

        CancelPendingPreviewDownload();
        await AbortRemotePreviewDownloadAsync();
        CleanupDownloadedPreviewFile();

        await _storageOperationGate.WaitAsync();

        _previewDownloadCancellation = new CancellationTokenSource();
        var ct = _previewDownloadCancellation.Token;
        var localCancellation = _previewDownloadCancellation;
        RaisePropertyChanged(nameof(IsPreviewDownloadActive));
        RaiseCanExecuteAll();
        // Preview downloads can legitimately produce hundreds of chunk packets for
        // mid-sized files. Dropping old packets corrupts the stream and eventually
        // turns into CRC mismatches or timeouts. Keep the full packet sequence.
        _downloadChannel = Channel.CreateUnbounded<DownloadPacket>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
        var localChannel = _downloadChannel;

        ToolpathStatusMessage = $"Downloading preview: {fileName}";
        UpdateToolpathState(hasGeometry: false, hasError: false);
        ClearWarnings();
        string? tempPath = null;

        try
        {
            DownloadPacket ready = new(DownloadPacketType.Failed, Reason: "Preview download was not accepted.");
            for (int attempt = 0; attempt < TransferMaxRetries; attempt++)
            {
                MainVm.Protocol.SendFileDownload(fileName);
                try
                {
                    ready = await ReadDownloadPacketAsync(localChannel, TransferInitTimeout, ct);
                    if (ready.Type == DownloadPacketType.Failed &&
                        (IsTransferBusyReason(ready.Reason) ||
                         string.Equals(ready.Reason, "DOWNLOAD_ABORTED", StringComparison.OrdinalIgnoreCase)))
                    {
                        await AbortRemotePreviewDownloadAsync();
                        ToolpathStatusMessage = $"Downloading preview: {fileName} (clearing stale session)";
                        continue;
                    }
                    break;
                }
                catch (TimeoutException) when (attempt + 1 < TransferMaxRetries)
                {
                    await AbortRemotePreviewDownloadAsync();
                    ToolpathStatusMessage = $"Downloading preview: {fileName} (retrying session)";
                }
            }
            if (ready.Type != DownloadPacketType.Ready)
                throw new IOException(ready.Reason.Length > 0 ? ready.Reason : "Preview download was not accepted.");
            byte transferId = ready.TransferId;
            long totalBytes = ready.Size;
            long bytesReceived = 0;
            uint expectedSeq = 0;

            tempPath = Path.Combine(
                Path.GetTempPath(),
                $"portable-cnc-preview-{Guid.NewGuid():N}-{fileName}");
            uint downloadCrc = 0xFFFFFFFFu;

            await using var stream = new FileStream(
                tempPath,
                FileMode.Create,
                FileAccess.Write,
                FileShare.None,
                bufferSize: 81920,
                useAsync: true);
            int consecutiveTimeouts = 0;
            while (true)
            {
                DownloadPacket packet;
                try
                {
                    packet = await ReadDownloadPacketAsync(localChannel, TransferChunkTimeout, ct);
                }
                catch (TimeoutException)
                {
                    ++consecutiveTimeouts;
                    await AbortRemotePreviewDownloadAsync();
                    throw new TimeoutException(
                        $"Preview download timed out waiting for chunk {expectedSeq} after {consecutiveTimeouts} attempt(s).");
                }
                consecutiveTimeouts = 0;
                switch (packet.Type)
                {
                    case DownloadPacketType.Chunk:
                    {
                        if (packet.TransferId != transferId || packet.Data == null)
                            continue;

                        if (packet.Seq < expectedSeq)
                        {
                            MainVm.Protocol.SendDownloadAckFrame(transferId, packet.Seq);
                            break;
                        }

                        if (packet.Seq > expectedSeq)
                            break;

                        var bytes = packet.Data;
                        await stream.WriteAsync(bytes, ct);
                        downloadCrc = UpdateCrc32(downloadCrc, bytes, bytes.Length);
                        bytesReceived += bytes.Length;
                        expectedSeq++;
                        ToolpathStatusMessage = totalBytes > 0
                            ? $"Downloading preview: {fileName} ({(int)(100.0 * bytesReceived / totalBytes)}%)"
                            : $"Downloading preview: {fileName}";
                        MainVm.Protocol.SendDownloadAckFrame(transferId, packet.Seq);
                        break;
                    }

                    case DownloadPacketType.Complete:
                    {
                        if (packet.TransferId != transferId)
                            continue;

                        await stream.FlushAsync(ct);
                        if (bytesReceived != totalBytes)
                            throw new IOException($"Preview download ended early ({bytesReceived}/{totalBytes} bytes).");
                        var actualCrc = FinalizeCrc32(downloadCrc).ToString("x8");
                        if (!string.Equals(actualCrc, packet.Reason, StringComparison.OrdinalIgnoreCase))
                            throw new IOException("Preview download CRC mismatch.");

                        _downloadPreviewPath = tempPath;
                        return _downloadPreviewPath;
                    }

                    case DownloadPacketType.Failed:
                        throw new IOException(packet.Reason);
                }
            }
        }
        catch (OperationCanceledException)
        {
            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileDownloadAbort();
            if (tempPath != null && File.Exists(tempPath))
                File.Delete(tempPath);
            return null;
        }
        catch (ChannelClosedException)
        {
            if (tempPath != null && File.Exists(tempPath))
                File.Delete(tempPath);
            return null;
        }
        catch (Exception ex)
        {
            if (tempPath != null && File.Exists(tempPath))
                File.Delete(tempPath);
            UpdateToolpathState(hasGeometry: false, hasError: true);
            ToolpathStatusMessage = "Source preview could not be loaded.";
            if (MainVm != null)
                MainVm.ActiveGCodeDocument = null;
            RaiseParseErrorDialog("Preview Download Error", $"Unable to preview '{fileName}'.", ex.Message);
            return null;
        }
        finally
        {
            localChannel?.Writer.TryComplete();
            if (ReferenceEquals(_downloadChannel, localChannel))
                _downloadChannel = null;

            if (ReferenceEquals(_previewDownloadCancellation, localCancellation))
            {
                _previewDownloadCancellation?.Dispose();
                _previewDownloadCancellation = null;
                RaisePropertyChanged(nameof(IsPreviewDownloadActive));
                RaiseCanExecuteAll();
            }
            _storageOperationGate.Release();
        }
    }

    private async Task<DownloadPacket> ReadDownloadPacketAsync(Channel<DownloadPacket> channel, TimeSpan timeout, CancellationToken ct)
        => await ReadChannelMessageAsync(channel.Reader, timeout, ct, "Preview download timed out.");

    // ─────────────────────────────────────────────────────────────────────────
    // Local preview
    // ─────────────────────────────────────────────────────────────────────────

    private async void PreviewLocalFile()
    {
        var path = await PickGCodeFileAsync();
        if (path == null) return;
        await BeginLocalPreviewAsync(path);
    }

    private Task BeginLocalPreviewAsync(string filePath)
        => BeginPreviewFromFileAsync(filePath, $"{Path.GetFileName(filePath)} (local)", isLocalPreview: true);

    private async Task BeginSelectedFilePreviewAsync(GCodeFileInfo file)
    {
        if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
            return;

        var tempPath = await DownloadSelectedFileAsync(file.Name);
        if (tempPath == null)
            return;

        await BeginPreviewFromFileAsync(tempPath, file.Name, isLocalPreview: false);
    }

    private async Task BeginPreviewFromFileAsync(string filePath, string displayName, bool isLocalPreview)
    {
        if (isLocalPreview)
            CleanupDownloadedPreviewFile();

        _previewDisplayName = displayName;
        _localPreviewPath   = filePath;
        IsLocalPreviewFile  = isLocalPreview;
        ResetPreviewState(keepLocalPreviewFlag: true);
        _previewDisplayName = displayName;
        RaiseFileStateCardProperties();

        if (!File.Exists(filePath))
        {
            ClearPreviewIdentity();
            return;
        }

        // Source preview
        try
        {
            int total = 0;
            foreach (var line in File.ReadLines(filePath))
            {
                total++;
                if (total <= PreviewMaxLines)
                    PreviewLines.Add(new GCodePreviewLine { LineNumber = total, Text = line });
            }
            PreviewLineCount = total;
            if (total > PreviewMaxLines)
                PreviewLines.Add(new GCodePreviewLine
                {
                    LineNumber = 0,
                    Text       = $"; ... ({total - PreviewMaxLines} more lines not shown)",
                    IsMetaLine = true
                });
        }
        catch (Exception ex)
        {
            PreviewLines.Clear();
            PreviewLines.Add(new GCodePreviewLine
            {
                LineNumber = 0,
                Text       = $"; Error reading file: {ex.Message}",
                IsMetaLine = true
            });
            PreviewLineCount   = 0;
            ClearPreviewIdentity();
            UpdateToolpathState(hasGeometry: false, hasError: true);
            ToolpathStatusMessage = "Source preview could not be loaded.";
            if (MainVm != null) MainVm.ActiveGCodeDocument = null;
            RaiseParseErrorDialog("File Read Error", $"Unable to read '{Path.GetFileName(filePath)}'.", $"{filePath}\n\n{ex}");
            RaisePropertyChanged(nameof(HasPreviewContent));
            RaisePropertyChanged(nameof(IsPreviewEmpty));
            return;
        }

        RaisePropertyChanged(nameof(HasPreviewContent));
        RaisePropertyChanged(nameof(IsPreviewEmpty));
        RaisePropertyChanged(nameof(FilePreviewTitle));

        // Toolpath parse
        await BeginToolpathParseAsync(filePath);
    }

    private async Task<string?> PickGCodeFileAsync()
    {
        var lifetime = Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime;
        var window   = lifetime?.MainWindow;
        if (window == null) return null;

        var picked = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select G-code file",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("G-code") { Patterns = new[] { "*.gcode", "*.nc", "*.ngc", "*.cnc", "*.tap" } },
                new FilePickerFileType("All files") { Patterns = new[] { "*.*" } }
            }
        });

        return picked.Count > 0 ? picked[0].Path.LocalPath : null;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Upload
    // ─────────────────────────────────────────────────────────────────────────

    private bool CanUpload()
        => !IsUploading && !IsPreviewDownloadActive && MainVm?.PiConnectionStatus == ConnectionStatus.Connected;

    private bool CanUploadLocalPreview()
        => IsLocalPreviewFile && !IsUploading && !IsPreviewDownloadActive && MainVm?.PiConnectionStatus == ConnectionStatus.Connected;

    private async void StartUpload()
    {
        var path = await PickGCodeFileAsync();
        if (path == null) return;

        // Parse locally for preview (runs alongside upload prep)
        await BeginLocalPreviewAsync(path);
        await DoUploadAsync(path, overwrite: false);
    }

    private async void StartUploadLocalPreview()
    {
        if (!IsLocalPreviewFile || _localPreviewPath == null) return;
        await DoUploadAsync(_localPreviewPath, overwrite: false);
    }

    private void CancelUpload() => _uploadCancellation?.Cancel();

    /// <summary>Called by the view after the user confirms overwriting an existing file.</summary>
    public void ConfirmOverwrite() => _overwriteTcs?.TrySetResult(true);

    /// <summary>Called by the view after the user declines overwriting an existing file.</summary>
    public void CancelOverwrite()  => _overwriteTcs?.TrySetResult(false);

    public void ConfirmLoadReplace() => _replaceLoadTcs?.TrySetResult(true);
    public void CancelLoadReplace()  => _replaceLoadTcs?.TrySetResult(false);

    private void RouteUpload(UploadAck ack) => _uploadChannel?.Writer.TryWrite(ack);
    private void RouteDownload(DownloadPacket packet) => _downloadChannel?.Writer.TryWrite(packet);

    private async Task DoUploadAsync(string filePath, bool overwrite)
    {
        if (IsUploading || MainVm?.PiConnectionStatus != ConnectionStatus.Connected) return;

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();

        _uploadCancellation = new CancellationTokenSource();
        var ct = _uploadCancellation.Token;

        // Upload acknowledgements are sequential, but they can still arrive in
        // bursts around chunk retries/finalize. Never drop them.
        _uploadChannel = Channel.CreateUnbounded<UploadAck>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
        var localUploadCancellation = _uploadCancellation;
        var localUploadChannel = _uploadChannel;

        var name = Path.GetFileName(filePath);
        var uploadSucceeded = false;
        IsUploading      = true;
        UploadProgress   = 0;
        UploadStatusText = $"Reading {name}…";

        try
        {
            var fileInfo = new FileInfo(filePath);
            long fileSize = fileInfo.Length;
            uint uploadCrc = 0xFFFFFFFFu;

            // ── Phase 1: initiate ──────────────────────────────────────────
            UploadStatusText = $"Connecting: {name}";
            var initAck = await BeginUploadSessionAsync(localUploadChannel, name, fileSize, overwrite, ct);

            if (initAck.Type == UploadAckType.FileExists)
            {
                // Surface overwrite confirm dialog to the user
                UploadFileExistsName = initAck.Name;
                UploadFileExistsRequested?.Invoke(this, initAck.Name);
                _overwriteTcs = new TaskCompletionSource<bool>();

                bool confirmed = await _overwriteTcs.Task.WaitAsync(TimeSpan.FromSeconds(60), ct);
                UploadFileExistsName = null;
                _overwriteTcs        = null;

                if (!confirmed)
                {
                    UploadStatusText = "Upload cancelled.";
                    return;
                }

                // Resend with overwrite flag
                initAck = await BeginUploadSessionAsync(localUploadChannel, name, fileSize, overwrite: true, ct);
            }

            if (initAck.Type == UploadAckType.Failed)
                throw new InvalidOperationException(initAck.Reason);
            if (initAck.Type != UploadAckType.Ready)
                throw new InvalidOperationException("Upload not accepted by Pico");
            byte transferId = initAck.TransferId;
            int negotiatedChunkSize = initAck.ChunkSize > 0 ? initAck.ChunkSize : TransferRawChunkSize;

            // ── Phase 2: chunks ────────────────────────────────────────────
            int totalChunks = (int)((fileSize + negotiatedChunkSize - 1) / negotiatedChunkSize);
            var buffer = new byte[negotiatedChunkSize];
            await using var fileStream = new FileStream(
                filePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 81920,
                useAsync: true);

            for (int seq = 0; seq < totalChunks; seq++)
            {
                ct.ThrowIfCancellationRequested();

                int length = await fileStream.ReadAsync(buffer.AsMemory(0, buffer.Length), ct);
                if (length <= 0)
                    throw new IOException($"Upload stream ended early at chunk {seq}.");

                uploadCrc = UpdateCrc32(uploadCrc, buffer, length);

                int retries = 0;
                UploadAck chunkAck;
                while (true)
                {
                    MainVm!.Protocol.SendUploadDataFrame(transferId, (uint)seq, buffer.AsSpan(0, length));
                    try
                    {
                        chunkAck = await ReadUploadChunkAckAsync(localUploadChannel, TransferChunkTimeout, ct, transferId, (uint)seq);
                    }
                    catch (TimeoutException)
                    {
                        if (++retries >= TransferMaxRetries)
                            throw new IOException($"Chunk {seq} timed out after {TransferMaxRetries} retries.");
                        UploadStatusText = $"Chunk {seq} timeout — retry {retries}/3…";
                        continue;
                    }

                    // Unrecoverable SD errors — abort immediately
                    if (chunkAck.Type == UploadAckType.Failed || chunkAck.Type == UploadAckType.Aborted)
                        throw new IOException($"Upload failed at chunk {seq}: {chunkAck.Reason}");

                    break;
                }

                UploadProgress   = fileSize == 0 ? 1.0 : (double)chunkAck.BytesCommitted / fileSize;
                UploadStatusText = $"Uploading {name}: {(int)(UploadProgress * 100)}%";
            }

            // ── Phase 3: finalise ──────────────────────────────────────────
            UploadStatusText = "Verifying…";
            MainVm!.Protocol.SendFileUploadEnd($"{FinalizeCrc32(uploadCrc):x8}");

            var finalAck = await ReadAckAsync(localUploadChannel, TransferFinalizeTimeout, ct);
            if (finalAck.Type != UploadAckType.Complete || finalAck.TransferId != transferId)
                throw new IOException($"Upload verification failed: {finalAck.Reason}");

            // Success
            UploadProgress        = 1.0;
            UploadStatusText      = $"Uploaded: {name}";
            IsLocalPreviewFile    = false;           // file is now on SD
            MainVm!.StatusMessage = $"Uploaded: {name}";
            MainVm.IsStatusError  = false;
            uploadSucceeded       = true;
            _ = RequestFileListAsync();
        }
        catch (OperationCanceledException)
        {
            // User cancel or USB disconnect
            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            UploadStatusText = "Upload cancelled.";
        }
        catch (Exception ex)
        {
            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            UploadStatusText = $"Upload failed: {ex.Message}";
            if (MainVm != null)
            {
                MainVm.StatusMessage = UploadStatusText;
                MainVm.IsStatusError = true;
            }
        }
        finally
        {
            localUploadChannel?.Writer.TryComplete();
            if (ReferenceEquals(_uploadChannel, localUploadChannel))
                _uploadChannel = null;
            if (ReferenceEquals(_uploadCancellation, localUploadCancellation))
            {
                _uploadCancellation?.Dispose();
                _uploadCancellation = null;
            }
            _overwriteTcs?.TrySetResult(false);
            _overwriteTcs  = null;
            IsUploading    = false;
            if (!uploadSucceeded)
                UploadProgress = 0;
            _storageOperationGate.Release();
        }
    }

    private async Task<UploadAck> BeginUploadSessionAsync(
        Channel<UploadAck> channel,
        string name,
        long fileSize,
        bool overwrite,
        CancellationToken ct)
    {
        for (int attempt = 0; attempt < TransferMaxRetries; attempt++)
        {
            MainVm!.Protocol.SendFileUpload(name, fileSize, overwrite);
            var ack = await ReadAckAsync(channel, TransferInitTimeout, ct);
            if (ack.Type != UploadAckType.Failed || !IsTransferBusyReason(ack.Reason))
                return ack;

            MainVm.Protocol.SendFileDownloadAbort();
            UploadStatusText = "Storage busy; cancelling preview transfer and retrying...";
            await Task.Delay(250, ct);
        }

        return new UploadAck(UploadAckType.Failed, Reason: "Storage transfer is busy.");
    }

    private async Task<UploadAck> ReadAckAsync(Channel<UploadAck> channel, TimeSpan timeout, CancellationToken ct)
        => await ReadChannelMessageAsync(channel.Reader, timeout, ct, "Storage transfer timed out.");

    private async Task<UploadAck> ReadUploadChunkAckAsync(Channel<UploadAck> channel, TimeSpan timeout, CancellationToken ct, byte transferId, uint sequence)
    {
        var deadline = DateTime.UtcNow + timeout;

        while (true)
        {
            var remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                throw new TimeoutException("Storage transfer timed out.");
            }

            var ack = await ReadChannelMessageAsync(channel.Reader, remaining, ct, "Storage transfer timed out.");
            if (ack.Type == UploadAckType.Failed || ack.Type == UploadAckType.Aborted)
                return ack;

            if (ack.Type == UploadAckType.ChunkOk && ack.TransferId == transferId && ack.Seq == sequence)
                return ack;
        }
    }

    private async Task<T> ReadChannelMessageAsync<T>(ChannelReader<T> reader, TimeSpan timeout, CancellationToken ct, string timeoutMessage)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(timeout);
        try
        {
            return await reader.ReadAsync(linked.Token);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new TimeoutException(timeoutMessage);
        }
    }

    private static bool IsTransferBusyReason(string reason)
        => reason.Contains("BUSY", StringComparison.OrdinalIgnoreCase) ||
           reason.Contains("TRANSFER", StringComparison.OrdinalIgnoreCase);

    // ─────────────────────────────────────────────────────────────────────────
    // Toolpath parsing
    // ─────────────────────────────────────────────────────────────────────────

    private async Task BeginToolpathParseAsync(string filePath)
    {
        CancelPendingParse();

        var cancellation = new CancellationTokenSource();
        _parseCancellation = cancellation;

        IsParsingToolpath     = true;
        ToolpathStatusMessage = "Parsing toolpath geometry…";
        ToolpathWarningSummary = "";
        UpdateToolpathState(hasGeometry: false, hasError: false);
        ClearWarnings();

        try
        {
            var document = await GCodeParser.ParseFileAsync(filePath, cancellation.Token);
            if (cancellation.IsCancellationRequested || _localPreviewPath != filePath) return;

            if (MainVm != null)
            {
                MainVm.ActiveGCodeDocument = document;
                MainVm.TotalLines          = document.TotalLines;
            }

            UpdateToolpathState(hasGeometry: document.HasGeometry, hasError: false);
            UpdateWarnings(document.Warnings);
            ToolpathStatusMessage = document.HasGeometry
                ? $"Toolpath ready: {document.Segments.Length} segments"
                : "No motion geometry found.";
            ToolpathWarningSummary = document.WarningCount == 0
                ? "No parse warnings."
                : $"{document.WarningCount} parse warning{(document.WarningCount == 1 ? "" : "s")}.";
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            if (_localPreviewPath == filePath)
            {
                UpdateToolpathState(hasGeometry: false, hasError: true);
                ClearWarnings();
                ToolpathStatusMessage  = "Toolpath parse failed.";
                ToolpathWarningSummary = "The parser could not build a preview for this file.";
                if (MainVm != null) MainVm.ActiveGCodeDocument = null;
                RaiseParseErrorDialog(
                    "Toolpath Parse Error",
                    $"Unable to parse '{Path.GetFileName(filePath)}'.",
                    $"{filePath}\n\n{ex}");
            }
        }
        finally
        {
            if (_parseCancellation == cancellation)
            {
                _parseCancellation = null;
                IsParsingToolpath  = false;
            }
            cancellation.Dispose();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Warnings
    // ─────────────────────────────────────────────────────────────────────────

    private void UpdateWarnings(IEnumerable<GCodeParseWarning> warnings)
    {
        ToolpathWarnings.Clear();
        foreach (var w in warnings)
        {
            ToolpathWarnings.Add(new GCodeWarningInfo
            {
                LineNumber         = w.LineNumber,
                ScopeLabel         = w.LineNumber > 0 ? $"Line {w.LineNumber}" : "Global",
                Message            = w.Message,
                IsVisibleInPreview = w.LineNumber > 0 && w.LineNumber <= PreviewMaxLines
            });
        }

        var lookup = ToolpathWarnings
            .Where(w => w.LineNumber > 0)
            .GroupBy(w => w.LineNumber)
            .ToDictionary(g => g.Key, g => string.Join("\n", g.Select(w => $"- {w.Message}")));

        foreach (var line in PreviewLines.Where(l => l.LineNumber > 0))
        {
            if (lookup.TryGetValue(line.LineNumber, out var tip))
            {
                line.HasWarning     = true;
                line.WarningTooltip = tip;
            }
            else
            {
                line.HasWarning     = false;
                line.WarningTooltip = string.Empty;
            }
        }

        GlobalWarningSummary = string.Join("\n",
            ToolpathWarnings.Where(w => w.LineNumber <= 0).Select(w => w.Message));
        RaisePropertyChanged(nameof(HasGlobalWarnings));
        RaisePropertyChanged(nameof(WarningDetail));
    }

    private void ClearWarnings()
    {
        ToolpathWarnings.Clear();
        foreach (var l in PreviewLines) { l.HasWarning = false; l.WarningTooltip = string.Empty; }
        GlobalWarningSummary = "";
        RaisePropertyChanged(nameof(HasGlobalWarnings));
        RaisePropertyChanged(nameof(WarningDetail));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Reset
    // ─────────────────────────────────────────────────────────────────────────

    private void ResetPreviewState(bool keepLocalPreviewFlag = false)
    {
        PreviewLines.Clear();
        PreviewLineCount = 0;
        RaisePropertyChanged(nameof(HasPreviewContent));
        RaisePropertyChanged(nameof(IsPreviewEmpty));
        RaisePropertyChanged(nameof(PreviewEmptyMessage));
        UpdateToolpathState(hasGeometry: false, hasError: false);
        ClearWarnings();
        ToolpathStatusMessage  = "Use 'Preview local file' to inspect a file.";
        ToolpathWarningSummary = "";
        CancelPendingParse();

        if (!keepLocalPreviewFlag)
        {
            ClearPreviewIdentity();
        }

        if (MainVm != null)
        {
            MainVm.ActiveGCodeDocument       = null;
            MainVm.DashboardVm.PreviewLine   = 0;
            MainVm.DashboardVm.ScrubberValue = 0;
        }
    }

    private void CancelPendingParse()
    {
        if (_parseCancellation == null) return;
        _parseCancellation.Cancel();
        _parseCancellation.Dispose();
        _parseCancellation = null;
        IsParsingToolpath  = false;
    }

    private async Task StopPreviewDownloadBeforeStorageCommandAsync()
    {
        CancelPendingPreviewDownload();
        await AbortRemotePreviewDownloadAsync();
    }

    private async Task AbortRemotePreviewDownloadAsync()
    {
        if (MainVm?.PiConnectionStatus != ConnectionStatus.Connected)
            return;

        MainVm.Protocol.SendFileDownloadAbort();
        await Task.Delay(250);
        DrainDownloadChannel();
    }

    private void CancelPendingPreviewDownload()
    {
        if (_previewDownloadCancellation == null)
            return;

        _previewDownloadCancellation.Cancel();
        _previewDownloadCancellation.Dispose();
        _previewDownloadCancellation = null;
        _downloadChannel?.Writer.TryComplete();
        _downloadChannel = null;
        RaisePropertyChanged(nameof(IsPreviewDownloadActive));
        RaiseCanExecuteAll();
    }

    private void DrainDownloadChannel()
    {
        if (_downloadChannel == null)
            return;

        while (_downloadChannel.Reader.TryRead(out _))
        {
        }
    }

    private void ClearDeviceFileStateOnDisconnect()
    {
        CancelPendingPreviewDownload();
        _overwriteTcs?.TrySetResult(false);
        _overwriteTcs = null;
        _replaceLoadTcs?.TrySetResult(false);
        _replaceLoadTcs = null;
        _pendingFileList.Clear();
        _pendingDeletes.Clear();

        Files.Clear();
        SelectedFile = null;
        SdFreeBytes = -1;

        if (!IsLocalPreviewFile)
        {
            CleanupDownloadedPreviewFile();
            ResetPreviewState();
        }
    }

    private void UpdateToolpathState(bool hasGeometry, bool hasError)
    {
        _toolpathHasGeometry = hasGeometry;
        _toolpathHasError    = hasError;
        RaisePropertyChanged(nameof(ToolpathStateLabel));
        RaisePropertyChanged(nameof(ToolpathStateBrush));
    }

    private void ClearPreviewIdentity()
    {
        IsLocalPreviewFile = false;
        _localPreviewPath = null;
        _previewDisplayName = null;
        RaisePropertyChanged(nameof(FilePreviewTitle));
        RaisePropertyChanged(nameof(PreviewEmptyMessage));
        RaiseFileStateCardProperties();
    }

    private void CleanupDownloadedPreviewFile()
    {
        if (string.IsNullOrWhiteSpace(_downloadPreviewPath))
            return;

        try
        {
            if (File.Exists(_downloadPreviewPath))
                File.Delete(_downloadPreviewPath);
        }
        catch
        {
        }

        _downloadPreviewPath = null;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    private void RaiseCanExecuteAll()
    {
        ((RelayCommand)UploadCommand).RaiseCanExecuteChanged();
        ((RelayCommand)CancelUploadCommand).RaiseCanExecuteChanged();
        ((RelayCommand)PreviewLocalCommand).RaiseCanExecuteChanged();
        ((RelayCommand)UploadLocalPreviewCommand).RaiseCanExecuteChanged();
        ((RelayCommand)RefreshCommand).RaiseCanExecuteChanged();
        ((RelayCommand)LoadSelectedFileCommand).RaiseCanExecuteChanged();
        ((RelayCommand)UnloadLoadedFileCommand).RaiseCanExecuteChanged();
    }

    private void RaiseFileStateCardProperties()
    {
        RaisePropertyChanged(nameof(HasSelectedPreview));
        RaisePropertyChanged(nameof(HasLocalPreview));
        RaisePropertyChanged(nameof(HasLoadedJob));
        RaisePropertyChanged(nameof(IsSelectedFileLoaded));
        RaisePropertyChanged(nameof(ShowSelectedPreviewCard));
        RaisePropertyChanged(nameof(ShowLoadedJobCard));
        RaisePropertyChanged(nameof(SelectedPreviewFileName));
        RaisePropertyChanged(nameof(SelectedPreviewSizeText));
        RaisePropertyChanged(nameof(SelectedPreviewLinesText));
        RaisePropertyChanged(nameof(SelectedPreviewHeaderText));
        RaisePropertyChanged(nameof(SelectedPreviewStatusText));
        RaisePropertyChanged(nameof(SelectedPreviewStatusBrush));
        RaisePropertyChanged(nameof(SelectedPreviewPrimaryActionText));
        RaisePropertyChanged(nameof(SelectedPreviewPrimaryActionCommand));
        RaisePropertyChanged(nameof(LoadedJobFileName));
        RaisePropertyChanged(nameof(LoadedJobSizeText));
        RaisePropertyChanged(nameof(LoadedJobLinesText));
    }

    private GCodeFileInfo? ResolveLoadedJobInfo()
    {
        var loadedName = MainVm?.CurrentFileName;
        if (string.IsNullOrWhiteSpace(loadedName))
            return null;

        return Files.FirstOrDefault(file => string.Equals(file.Name, loadedName, StringComparison.OrdinalIgnoreCase));
    }

    private string TryGetDocumentLineSummary(string? fileName)
    {
        if (string.IsNullOrWhiteSpace(fileName))
            return "--";

        if (MainVm?.ActiveGCodeDocument == null || string.IsNullOrWhiteSpace(_previewDisplayName))
            return "--";

        var previewName = _previewDisplayName.EndsWith(" (local)", StringComparison.OrdinalIgnoreCase)
            ? _previewDisplayName[..^8]
            : _previewDisplayName;

        if (!string.Equals(previewName, fileName, StringComparison.OrdinalIgnoreCase))
            return "--";

        var totalLines = MainVm?.ActiveGCodeDocument?.TotalLines ?? 0;
        return totalLines > 0 ? $"{totalLines} total lines" : "--";
    }

    private string? GetLocalPreviewName()
    {
        if (string.IsNullOrWhiteSpace(_previewDisplayName))
            return null;

        return _previewDisplayName.EndsWith(" (local)", StringComparison.OrdinalIgnoreCase)
            ? _previewDisplayName[..^8]
            : _previewDisplayName;
    }

    private string GetLocalPreviewSizeText()
    {
        if (string.IsNullOrWhiteSpace(_localPreviewPath) || !File.Exists(_localPreviewPath))
            return "--";

        try
        {
            return FormatSize(new FileInfo(_localPreviewPath).Length);
        }
        catch
        {
            return "--";
        }
    }

    private void RaiseParseErrorDialog(string title, string summary, string details)
        => ParseErrorDialogRequested?.Invoke(this, new ParseErrorDialogRequest(title, summary, details));

    private static string FormatSize(long bytes)
    {
        if (bytes < 1024)           return $"{bytes} B";
        if (bytes < 1024 * 1024)    return $"{bytes / 1024.0:F1} KB";
        return $"{bytes / (1024.0 * 1024):F1} MB";
    }

    /// <summary>CRC-32 (ISO 3309 / zlib polynomial) of raw bytes.</summary>
    private static uint ComputeCrc32(byte[] data)
        => FinalizeCrc32(UpdateCrc32(0xFFFFFFFFu, data, data.Length));

    private static uint UpdateCrc32(uint crc, byte[] data, int length)
    {
        for (int index = 0; index < length; index++)
        {
            byte b = data[index];
            crc ^= b;
            for (int i = 0; i < 8; i++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }

        return crc;
    }

    private static uint FinalizeCrc32(uint crc) => ~crc;

    private void HandleThemeChanged(object? sender, EventArgs e)
    {
        RaisePropertyChanged(nameof(ToolpathStateBrush));
        foreach (var l in PreviewLines) l.RefreshThemeDependentBindings();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Data models
// ─────────────────────────────────────────────────────────────────────────────

public sealed class GCodeFileInfo
{
    public string Name     { get; set; } = "";
    public string FullPath { get; set; } = "";
    public string Size     { get; set; } = "";
    public string Modified { get; set; } = "";
}

public sealed class GCodeWarningInfo
{
    public int    LineNumber         { get; set; }
    public string ScopeLabel        { get; set; } = "";
    public string Message           { get; set; } = "";
    public bool   IsVisibleInPreview { get; set; }
}

public sealed class GCodePreviewLine : ViewModelBase
{
    private bool   _hasWarning;
    private string _warningTooltip = "";

    public int    LineNumber  { get; set; }
    public string Text        { get; set; } = "";
    public bool   IsMetaLine  { get; set; }

    public string LineNumberText => LineNumber > 0 ? LineNumber.ToString() : "";

    public bool HasWarning
    {
        get => _hasWarning;
        set => SetProperty(ref _hasWarning, value);
    }

    public string WarningTooltip
    {
        get => _warningTooltip;
        set => SetProperty(ref _warningTooltip, value);
    }

    public void RefreshThemeDependentBindings() => RaisePropertyChanged(nameof(HasWarning));
}

public sealed class ParseErrorDialogRequest : EventArgs
{
    public ParseErrorDialogRequest(string title, string summary, string details)
    {
        Title   = title;
        Summary = summary;
        Details = details;
    }

    public string Title   { get; }
    public string Summary { get; }
    public string Details { get; }
}

public sealed class LoadReplaceDialogRequest : EventArgs
{
    public LoadReplaceDialogRequest(string currentLoadedFile, string requestedFile)
    {
        CurrentLoadedFile = currentLoadedFile;
        RequestedFile = requestedFile;
    }

    public string CurrentLoadedFile { get; }
    public string RequestedFile { get; }
}
