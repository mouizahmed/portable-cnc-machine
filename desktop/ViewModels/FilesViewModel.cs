using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
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
using Avalonia.Threading;
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
    private static readonly int TransferRawChunkSize = ControllerProtocolService.BinaryTransferChunkSize;
    private static readonly TimeSpan TransferInitTimeout = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan TransferChunkTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan TransferFinalizeTimeout = TimeSpan.FromSeconds(30);
    private const int TransferMaxRetries = 3;
    // Keep the desktop burst below the controller-side queue capacity.
    // Matching the queue depth exactly leaves no headroom if the worker falls behind,
    // which can trap the controller main loop in upload wait paths and make the UI appear frozen.
    private static readonly int UploadWindowSize = 8;
    private static readonly uint[] Crc32Table = BuildCrc32Table();

    // ── Preview fields ────────────────────────────────────────────────────────
    private CancellationTokenSource? _parseCancellation;
    private bool   _toolpathHasGeometry;
    private bool   _toolpathHasError;
    private int    _previewLineCount;
    private bool   _isParsingToolpath;
    private string _toolpathStatusMessage = "";
    private string _toolpathWarningSummary = "";
    private string _globalWarningSummary   = "";
    private string? _previewDisplayName;
    private bool   _isLocalPreviewFile;
    private string? _localPreviewPath;

    // ── SD-list fields ────────────────────────────────────────────────────────
    private readonly List<GCodeFileInfo> _pendingFileList = new();
    private readonly SemaphoreSlim _storageOperationGate = new(1, 1);
    private GCodeFileInfo? _selectedFile;
    private long    _sdFreeBytes = -1;

    // ── Delete tracking ───────────────────────────────────────────────────────
    private readonly Dictionary<string, GCodeFileInfo> _pendingDeletes = new();

    // ── Upload fields ─────────────────────────────────────────────────────────
    private DesktopStorageState _storageState;
    private double _uploadProgress;
    private string _uploadStatusText = "";
    private string? _uploadFileExistsName;
    private CancellationTokenSource?        _uploadCancellation;
    private Channel<UploadAck>?             _uploadChannel;
    private TaskCompletionSource<bool>?     _overwriteTcs;
    private string? _pendingUploadVerificationName;
    private long? _pendingUploadVerificationSize;

    // Download preview flow
    private CancellationTokenSource? _previewDownloadCancellation;
    private Channel<DownloadPacket>? _downloadChannel;
    private TaskCompletionSource<bool>? _replaceLoadTcs;
    private TaskCompletionSource<bool>? _unloadLoadedJobTcs;

    // Selection debounce + preview cache
    private CancellationTokenSource? _selectionDebounceCts;
    private CancellationTokenSource? _loadedJobPreviewCts;
    private string? _loadedJobPreviewInFlightName;
    private string? _selectAfterRefreshName;
    private readonly Dictionary<string, PreviewCacheEntry> _previewCache = new(StringComparer.OrdinalIgnoreCase);

    // Upload ACK discriminated union
    private enum UploadAckType { Ready, ChunkOk, Complete, Aborted, Failed, FileExists }
    private record UploadAck(UploadAckType Type, uint Seq = 0, string Name = "",
                              long Size = 0, string Reason = "", byte TransferId = 0, int ChunkSize = 0,
                              uint BytesCommitted = 0);
    private sealed record PendingUploadChunk(uint Sequence, byte[] Data, int Length);

    private enum DownloadPacketType { Ready, Chunk, Complete, Failed }
    private record DownloadPacket(DownloadPacketType Type, uint Seq = 0, string Name = "", long Size = 0, byte[]? Data = null, string Reason = "", byte TransferId = 0, int ChunkSize = 0);
    private sealed record PreviewCacheEntry(long SizeBytes, GCodeDocument Document, IReadOnlyList<GCodePreviewLine> PreviewLines, int PreviewLineCount);
    private sealed record PreviewBuildResult(GCodeDocument Document, IReadOnlyList<GCodePreviewLine> PreviewLines, int PreviewLineCount);

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
        PreviewLocalCommand       = new RelayCommand(PreviewLocalFile,       () => !HasActiveStorageOperation);
        UploadLocalPreviewCommand = new RelayCommand(StartUploadLocalPreview, CanUploadLocalPreview);
        RefreshCommand            = new RelayCommand(RefreshFileList,        () => !HasActiveStorageOperation && !IsSdJobActive);
        LoadSelectedFileCommand   = new RelayCommand(LoadSelectedFile,       CanLoadSelectedFile);
        UnloadLoadedFileCommand   = new RelayCommand(UnloadLoadedFile,       CanUnloadLoadedFile);
        DeleteCommand             = new RelayCommand<GCodeFileInfo>(DeleteFile, file => file != null && !HasActiveStorageOperation && !IsSdJobActive);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Events
    // ─────────────────────────────────────────────────────────────────────────

    public event EventHandler<ParseErrorDialogRequest>? ParseErrorDialogRequested;

    /// <summary>
    /// Raised when the controller reports FILE_EXISTS during upload.
    /// View shows confirm dialog; call ConfirmOverwrite() or CancelOverwrite() in response.
    /// </summary>
    public event EventHandler<string>? UploadFileExistsRequested;
    public event EventHandler<LoadReplaceDialogRequest>? LoadReplaceRequested;
    public event EventHandler<string>? UnloadLoadedJobRequested;

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

            if (value == null)
            {
                CancelSelectedFilePreviewRequest();

                if (!IsLocalPreviewFile)
                {
                    ResetPreviewState();
                }
            }
            else if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected &&
                     !IsSdJobActive &&
                     (!HasActiveStorageOperation || IsPreviewDownloadActive))
            {
                // Abort the in-flight download immediately so it stops wasting bandwidth.
                if (IsPreviewDownloadActive)
                    CancelPendingPreviewDownload();

                CancelSelectedFilePreviewRequest();
                _selectionDebounceCts = new CancellationTokenSource();
                _ = BeginSelectedFilePreviewAsync(value, _selectionDebounceCts.Token);
            }
        }
    }

    public bool HasSelectedFile => SelectedFile != null;
    public bool HasNoFiles      => Files.Count == 0;
    public bool ShowDeviceFilesPanel => MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected;
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
    public string LoadedJobLinesText => GetLoadedJobLineSummary();
    public string SelectedPreviewHeaderText => "";
    public string LoadedJobHeaderText => "";
    public string SelectedPreviewStatusText => HasLocalPreview ? "LOCAL" : "PREVIEW";
    public IBrush SelectedPreviewStatusBrush => HasLocalPreview
        ? ThemeResources.Brush("WarningBrush", "#E0A100")
        : ThemeResources.Brush("InfoBrush", "#5B9BD5");
    public string SelectedPreviewPrimaryActionText => HasLocalPreview ? "Upload to Device" : "Load for Job";
    public ICommand SelectedPreviewPrimaryActionCommand => HasLocalPreview
        ? UploadLocalPreviewCommand
        : LoadSelectedFileCommand;
    public string LoadedJobPrimaryActionText => MainVm?.JobState == JobRunState.Hold ? "Resume Job" : "Start Job";
    public string LoadedJobStatusText => MainVm?.JobStateLabel ?? "NO JOB";
    public IBrush LoadedJobStatusBrush => MainVm?.JobStateBrush ?? ThemeResources.Brush("NeutralStateBrush", "#808080");

    // ─────────────────────────────────────────────────────────────────────────
    // Upload state
    // ─────────────────────────────────────────────────────────────────────────

    public DesktopStorageState StorageState
    {
        get => _storageState;
        private set
        {
            if (!SetProperty(ref _storageState, value))
                return;

            RaisePropertyChanged(nameof(IsUploading));
            RaisePropertyChanged(nameof(IsPreviewDownloadActive));
            RaisePropertyChanged(nameof(IsSdJobActive));
            RaisePropertyChanged(nameof(HasActiveStorageOperation));
            RaiseCanExecuteAll();
        }
    }

    public bool IsUploading => StorageState == DesktopStorageState.Uploading;

    public bool IsPreviewDownloadActive => StorageState == DesktopStorageState.DownloadingPreview;

    public bool IsSdJobActive => MainVm?.JobState is JobRunState.Running or JobRunState.Hold;

    public bool HasActiveStorageOperation => StorageState is DesktopStorageState.Refreshing
        or DesktopStorageState.Uploading
        or DesktopStorageState.DownloadingPreview
        or DesktopStorageState.Deleting
        or DesktopStorageState.Loading
        or DesktopStorageState.Aborting;

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

    private void SetStorageState(DesktopStorageState state) => StorageState = state;

    private void CompleteStorageState(bool success)
        => SetStorageState(success ? DesktopStorageState.Idle : DesktopStorageState.Failed);

    private void ClearTransferStateToIdle()
    {
        if (StorageState is DesktopStorageState.Uploading
            or DesktopStorageState.DownloadingPreview
            or DesktopStorageState.Aborting)
        {
            SetStorageState(DesktopStorageState.Idle);
        }
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
        => IsLocalPreviewFile && MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected;

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
        => _previewDisplayName != null
            ? "No source lines to display."
            : ShowDeviceFilesPanel
                ? "Select a device file or preview a local file."
                : "Preview a local file to inspect its source.";

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
            case nameof(MainWindowViewModel.ControllerConnectionStatus):
            {
                var status = MainVm?.ControllerConnectionStatus;

                if (status == ConnectionStatus.Connected)
                {
                    _ = RequestFileListAsync();
                }
                else
                {
                    if (IsUploading)
                        _uploadCancellation?.Cancel(); // USB disconnect mid-upload — no abort command, just cancel

                    _previewDownloadCancellation?.Cancel();
                    ClearDeviceFileStateOnDisconnect();
                }

                RaisePropertyChanged(nameof(ShowLocalPreviewBanner));
                RaisePropertyChanged(nameof(ShowDeviceFilesPanel));
                RaisePropertyChanged(nameof(PreviewEmptyMessage));
                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
                break;
            }

            case nameof(MainWindowViewModel.CurrentFileName):
                if (string.IsNullOrWhiteSpace(MainVm?.CurrentFileName))
                    CancelLoadedJobPreviewRequest();
                else
                {
                    SelectLoadedFileIfAvailable();
                    QueueLoadedJobPreviewIfNeeded();
                }

                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
                break;

            case nameof(MainWindowViewModel.JobState):
            case nameof(MainWindowViewModel.JobStateLabel):
            case nameof(MainWindowViewModel.JobStateBrush):
                RaisePropertyChanged(nameof(IsSdJobActive));
                RaiseFileStateCardProperties();
                RaiseCanExecuteAll();
                break;

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
        if (IsSdJobActive || MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
            return;

        CancelSelectedFilePreviewRequest();
        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        var refreshSucceeded = false;
        try
        {
            SetStorageState(DesktopStorageState.Refreshing);
            _pendingFileList.Clear();
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_LIST_END",
                MainVm.Protocol.SendFileList,
                TimeSpan.FromSeconds(5),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "File list refresh failed");
            refreshSucceeded = result.Success;
            CompleteStorageState(result.Success);
        }
        finally
        {
            _storageOperationGate.Release();
        }

        if (refreshSucceeded)
            QueueSelectedFilePreviewIfNeeded();
        else
            FailPendingUploadVerification("device file list refresh failed.");
    }

    private void HandleFileListEntry(string name, long sizeBytes)
    {
        _pendingFileList.Add(new GCodeFileInfo
        {
            Name      = name,
            FullPath  = string.Empty,
            Size      = FormatSize(sizeBytes),
            Modified  = string.Empty,
            SizeBytes = sizeBytes
        });
    }

    private void HandleFileListEnd(int count, long freeBytes)
    {
        var requestedSelectionName = _selectAfterRefreshName;
        _selectAfterRefreshName = null;
        var previousSelectionName = requestedSelectionName
            ?? SelectedFile?.Name
            ?? MainVm?.CurrentFileName;
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

        // Evict cache entries for files that no longer exist or whose size changed.
        var currentFiles = Files.ToDictionary(f => f.Name, f => f.SizeBytes, StringComparer.OrdinalIgnoreCase);
        var stale = _previewCache
            .Where(kv => !currentFiles.TryGetValue(kv.Key, out var sz) || sz != kv.Value.SizeBytes)
            .Select(kv => kv.Key)
            .ToList();
        foreach (var key in stale)
            _previewCache.Remove(key);

        VerifyPendingUploadAfterRefresh();
    }

    private void VerifyPendingUploadAfterRefresh()
    {
        if (string.IsNullOrWhiteSpace(_pendingUploadVerificationName) ||
            _pendingUploadVerificationSize is not { } expectedSize)
        {
            return;
        }

        var uploaded = Files.FirstOrDefault(file =>
            string.Equals(file.Name, _pendingUploadVerificationName, StringComparison.OrdinalIgnoreCase));
        var verified = uploaded != null && uploaded.SizeBytes == expectedSize;
        var name = _pendingUploadVerificationName;
        ClearPendingUploadVerification();

        if (verified)
        {
            PromoteCurrentPreviewToUploadedFile(uploaded!);
            UploadProgress = 1.0;
            UploadStatusText = $"Uploaded: {name}";
            if (MainVm != null)
            {
                MainVm.StatusMessage = $"Uploaded: {name}";
                MainVm.IsStatusError = false;
            }
            return;
        }

        UploadProgress = 0;
        UploadStatusText = uploaded == null
            ? $"Upload verification failed: {name} is missing from the device file list."
            : $"Upload verification failed: {name} size is {FormatSize(uploaded.SizeBytes)}, expected {FormatSize(expectedSize)}.";
        if (MainVm != null)
        {
            MainVm.StatusMessage = UploadStatusText;
            MainVm.IsStatusError = true;
        }
    }

    private void HandleProtocolEvent(string name, IReadOnlyDictionary<string, string> metadata)
    {
        switch (name)
        {
            case "SD_MOUNTED":
                _ = RequestFileListAsync();
                break;

            case "SD_REMOVED":
                CancelLoadedJobPreviewRequest();
                Files.Clear();
                SelectedFile = null;
                SdFreeBytes  = -1;
                RaiseFileStateCardProperties();
                break;
        }
    }

    private void RefreshFileList()
    {
        if (!IsSdJobActive && MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
            _ = RequestFileListAsync();
    }

    private bool CanLoadSelectedFile()
        => !HasActiveStorageOperation &&
           !IsSdJobActive &&
           MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected &&
           SelectedFile != null &&
           !IsSelectedFileLoaded;

    private async void LoadSelectedFile()
    {
        if (!CanLoadSelectedFile() || SelectedFile == null || MainVm == null)
            return;

        var fileToLoad = SelectedFile.Name;

        if (!string.IsNullOrWhiteSpace(MainVm.CurrentFileName))
        {
            if (LoadReplaceRequested == null)
                return;

            _replaceLoadTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            LoadReplaceRequested?.Invoke(this, new LoadReplaceDialogRequest(MainVm.CurrentFileName!, fileToLoad));
            var shouldReplace = await _replaceLoadTcs.Task;
            _replaceLoadTcs = null;
            if (!shouldReplace)
                return;
        }

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            SetStorageState(DesktopStorageState.Loading);
            MainVm.StatusMessage = $"Loading: {fileToLoad}";
            MainVm.IsStatusError = false;
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_LOAD",
                () => MainVm.Protocol.SendFileLoad(fileToLoad),
                TimeSpan.FromSeconds(3),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "Load failed");
            if (result.Success)
                SelectFileByName(fileToLoad);
            CompleteStorageState(result.Success);
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private bool CanUnloadLoadedFile()
        => !HasActiveStorageOperation &&
           !IsSdJobActive &&
           MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected &&
           !string.IsNullOrWhiteSpace(MainVm.CurrentFileName);

    private async void UnloadLoadedFile()
    {
        if (!CanUnloadLoadedFile() || MainVm == null)
            return;

        var loadedFileName = MainVm.CurrentFileName!;
        if (UnloadLoadedJobRequested == null)
            return;

        _unloadLoadedJobTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        UnloadLoadedJobRequested?.Invoke(this, loadedFileName);
        var shouldUnload = await _unloadLoadedJobTcs.Task;
        _unloadLoadedJobTcs = null;
        if (!shouldUnload)
            return;

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            SetStorageState(DesktopStorageState.Loading);
            MainVm.StatusMessage = "Unloading active job";
            MainVm.IsStatusError = false;
            var result = await MainVm.SendCommandAndWaitAsync(
                "FILE_UNLOAD",
                MainVm.Protocol.SendFileUnload,
                TimeSpan.FromSeconds(3),
                disconnectOnTimeout: false);
            MainVm.ApplyCommandResult(result, "Unload failed");
            CompleteStorageState(result.Success);
        }
        finally
        {
            _storageOperationGate.Release();
        }
    }

    private async void DeleteFile(GCodeFileInfo? file)
    {
        if (file == null || MainVm == null || IsSdJobActive) return;
        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();
        try
        {
            SetStorageState(DesktopStorageState.Deleting);
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
            CompleteStorageState(result.Success);
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
            _previewCache.Remove(name);
        }
    }

    private async Task<byte[]?> DownloadSelectedFileAsync(string fileName, CancellationToken cancellationToken = default)
    {
        if (IsSdJobActive)
            return null;

        if (MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
            return null;

        if (cancellationToken.IsCancellationRequested)
            return null;

        CancelPendingPreviewDownload();
        await AbortRemotePreviewDownloadAsync();

        CancellationTokenSource? localCancellation = null;
        Channel<DownloadPacket>? localChannel = null;
        var gateAcquired = false;

        try
        {
            await _storageOperationGate.WaitAsync(cancellationToken);
            gateAcquired = true;

            _previewDownloadCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            var ct = _previewDownloadCancellation.Token;
            localCancellation = _previewDownloadCancellation;
            SetStorageState(DesktopStorageState.DownloadingPreview);
            // Preview downloads can legitimately produce hundreds of chunk packets for
            // mid-sized files. Dropping old packets corrupts the stream and eventually
            // turns into CRC mismatches or timeouts. Keep the full packet sequence.
            _downloadChannel = Channel.CreateUnbounded<DownloadPacket>(new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false
            });
            localChannel = _downloadChannel;

            ToolpathStatusMessage = $"Downloading preview: {fileName}";
            UpdateToolpathState(hasGeometry: false, hasError: false);
            ClearWarnings();

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

            if (totalBytes < 0 || totalBytes > int.MaxValue)
                throw new IOException($"Preview download size is not supported ({totalBytes} bytes).");

            uint downloadCrc = 0xFFFFFFFFu;

            using var stream = totalBytes > 0
                ? new MemoryStream((int)totalBytes)
                : new MemoryStream();
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

                        if (bytesReceived != totalBytes)
                            throw new IOException($"Preview download ended early ({bytesReceived}/{totalBytes} bytes).");
                        var actualCrc = FinalizeCrc32(downloadCrc).ToString("x8");
                        if (!string.Equals(actualCrc, packet.Reason, StringComparison.OrdinalIgnoreCase))
                            throw new IOException("Preview download CRC mismatch.");

                        return stream.ToArray();
                    }

                    case DownloadPacketType.Failed:
                        throw new IOException(packet.Reason);
                }
            }
        }
        catch (OperationCanceledException)
        {
            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileDownloadAbort();
            SetStorageState(DesktopStorageState.Idle);
            return null;
        }
        catch (ChannelClosedException)
        {
            SetStorageState(DesktopStorageState.Idle);
            return null;
        }
        catch (Exception ex)
        {
            SetStorageState(DesktopStorageState.Failed);
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
                ClearTransferStateToIdle();
            }
            if (gateAcquired)
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

    private async Task BeginSelectedFilePreviewAsync(GCodeFileInfo file, CancellationToken debounceCt = default, bool debounce = true)
    {
        try
        {
        if (debounce)
        {
            // Debounce: hold off while the user is rapidly switching files.
            // During the delay the previously displayed content stays visible.
            try { await Task.Delay(300, debounceCt); }
            catch (OperationCanceledException) { return; }
        }

        if (MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
            return;

        if (debounceCt.IsCancellationRequested)
            return;

        // Clear stale content now that we've committed to loading this file.
        CancelPendingParse();
        PreviewLines.Clear();
        PreviewLineCount = 0;
        RaisePropertyChanged(nameof(HasPreviewContent));
        RaisePropertyChanged(nameof(IsPreviewEmpty));
        UpdateToolpathState(hasGeometry: false, hasError: false);
        if (MainVm != null) MainVm.ActiveGCodeDocument = null;
        ClearWarnings();
        _previewDisplayName = file.Name;
        IsLocalPreviewFile  = false;
        _localPreviewPath = null;
        ToolpathStatusMessage = $"Loading {file.Name}…";
        RaiseFileStateCardProperties();

        // Cache hit: skip the download entirely.
        if (_previewCache.TryGetValue(file.Name, out var cached) &&
            cached.SizeBytes == file.SizeBytes)
        {
            if (debounceCt.IsCancellationRequested)
                return;

            ApplyPreviewResult(
                new PreviewBuildResult(cached.Document, cached.PreviewLines, cached.PreviewLineCount),
                file.Name,
                isLocalPreview: false);
            return;
        }

        var data = await DownloadSelectedFileAsync(file.Name, debounceCt);
        if (data == null)
            return;

        if (debounceCt.IsCancellationRequested)
            return;

        using var memory = new MemoryStream(data, writable: false);
        var result = await BuildPreviewFromStreamAsync(memory, file.Name, debounceCt);
        _previewCache[file.Name] = new PreviewCacheEntry(
            file.SizeBytes,
            result.Document,
            result.PreviewLines,
            result.PreviewLineCount);

        ApplyPreviewResult(result, file.Name, isLocalPreview: false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ChannelClosedException)
        {
        }
        catch (ObjectDisposedException)
        {
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
            PreviewLineCount = 0;
            UpdateToolpathState(hasGeometry: false, hasError: true);
            ToolpathStatusMessage = "Source preview could not be loaded.";
            if (MainVm != null) MainVm.ActiveGCodeDocument = null;
            RaiseParseErrorDialog("File Read Error", $"Unable to read '{file.Name}'.", ex.ToString());
            RaisePropertyChanged(nameof(HasPreviewContent));
            RaisePropertyChanged(nameof(IsPreviewEmpty));
        }
    }

    private async Task BeginPreviewFromFileAsync(string filePath, string displayName, bool isLocalPreview)
    {
        _previewDisplayName = displayName;
        _localPreviewPath   = isLocalPreview ? filePath : null;
        IsLocalPreviewFile  = isLocalPreview;
        ResetPreviewState(keepLocalPreviewFlag: true);
        _previewDisplayName = displayName;
        _localPreviewPath   = isLocalPreview ? filePath : null;
        RaiseFileStateCardProperties();

        if (!File.Exists(filePath))
        {
            ClearPreviewIdentity();
            return;
        }

        // Source preview
        try
        {
            await using var stream = new FileStream(
                filePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 81920,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            var result = await BuildPreviewFromStreamAsync(stream, displayName, CancellationToken.None);
            ApplyPreviewResult(result, displayName, isLocalPreview);
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
        }
    }

    private async Task<PreviewBuildResult> BuildPreviewFromStreamAsync(Stream stream, string sourceName, CancellationToken cancellationToken)
    {
        ValidatePreviewFileText(stream);
        stream.Position = 0;

        var previewLines = new List<GCodePreviewLine>();
        int total = 0;
        using (var reader = new StreamReader(stream, System.Text.Encoding.UTF8, detectEncodingFromByteOrderMarks: true, leaveOpen: true))
        {
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var line = await reader.ReadLineAsync();
                if (line == null)
                    break;

                total++;
                if (total <= PreviewMaxLines)
                    previewLines.Add(new GCodePreviewLine { LineNumber = total, Text = line });
            }
        }

        if (total > PreviewMaxLines)
            previewLines.Add(new GCodePreviewLine
            {
                LineNumber = 0,
                Text       = $"; ... ({total - PreviewMaxLines} more lines not shown)",
                IsMetaLine = true
            });

        stream.Position = 0;
        var document = await GCodeParser.ParseAsync(stream, sourceName, cancellationToken);
        return new PreviewBuildResult(document, previewLines, total);
    }

    private void ApplyPreviewResult(PreviewBuildResult result, string displayName, bool isLocalPreview)
    {
        CancelPendingParse();
        PreviewLines.Clear();
        foreach (var line in result.PreviewLines)
        {
            PreviewLines.Add(new GCodePreviewLine
            {
                LineNumber = line.LineNumber,
                Text = line.Text,
                IsMetaLine = line.IsMetaLine
            });
        }

        PreviewLineCount = result.PreviewLineCount;
        _previewDisplayName = displayName;
        IsLocalPreviewFile = isLocalPreview;

        if (MainVm != null)
        {
            MainVm.ActiveGCodeDocument = result.Document;
            MainVm.TotalLines = result.Document.TotalLines;
        }

        UpdateToolpathState(hasGeometry: result.Document.HasGeometry, hasError: false);
        UpdateWarnings(result.Document.Warnings);
        ToolpathStatusMessage = result.Document.HasGeometry
            ? $"Toolpath ready: {result.Document.Segments.Length} segments"
            : "No motion geometry found.";
        ToolpathWarningSummary = result.Document.WarningCount == 0
            ? "No parse warnings."
            : $"{result.Document.WarningCount} parse warning{(result.Document.WarningCount == 1 ? "" : "s")}.";

        RaisePropertyChanged(nameof(HasPreviewContent));
        RaisePropertyChanged(nameof(IsPreviewEmpty));
        RaisePropertyChanged(nameof(FilePreviewTitle));
        RaiseFileStateCardProperties();
    }

    private static void ValidatePreviewFileText(Stream stream)
    {
        var buffer = new byte[8192];
        long offset = 0;
        while (true)
        {
            var read = stream.Read(buffer, 0, buffer.Length);
            if (read <= 0)
                return;

            for (var index = 0; index < read; index++)
            {
                var value = buffer[index];
                if (value == 0 ||
                    (value < 0x20 && value != (byte)'\t' && value != (byte)'\r' && value != (byte)'\n'))
                {
                    throw new InvalidDataException(
                        $"Downloaded preview contains binary data at byte {offset + index}. Re-upload the source G-code file.");
                }
            }

            offset += read;
        }
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
        => !HasActiveStorageOperation && !IsSdJobActive && MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected;

    private bool CanUploadLocalPreview()
        => IsLocalPreviewFile && !HasActiveStorageOperation && !IsSdJobActive && MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected;

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
    public void ConfirmUnloadLoadedJob() => _unloadLoadedJobTcs?.TrySetResult(true);
    public void CancelUnloadLoadedJob()  => _unloadLoadedJobTcs?.TrySetResult(false);

    private void RouteUpload(UploadAck ack) => _uploadChannel?.Writer.TryWrite(ack);
    private void RouteDownload(DownloadPacket packet) => _downloadChannel?.Writer.TryWrite(packet);

    private void QueueSelectAfterRefresh(string fileName)
        => _selectAfterRefreshName = fileName;

    private void QueueUploadVerification(string fileName, long sizeBytes)
    {
        _pendingUploadVerificationName = fileName;
        _pendingUploadVerificationSize = sizeBytes;
        QueueSelectAfterRefresh(fileName);
    }

    private void ClearPendingUploadVerification()
    {
        _pendingUploadVerificationName = null;
        _pendingUploadVerificationSize = null;
    }

    private void FailPendingUploadVerification(string reason)
    {
        if (string.IsNullOrWhiteSpace(_pendingUploadVerificationName))
            return;

        var name = _pendingUploadVerificationName;
        ClearPendingUploadVerification();
        UploadProgress = 0;
        UploadStatusText = $"Upload verification failed: {name} {reason}";
        if (MainVm != null)
        {
            MainVm.StatusMessage = UploadStatusText;
            MainVm.IsStatusError = true;
        }
    }

    private string FormatUploadProgressText(string name, long committedBytes, long totalBytes)
    {
        var percent = totalBytes <= 0 ? 100 : (int)(100.0 * committedBytes / totalBytes);
        return $"Uploading {name}: {FormatSize(committedBytes)} / {FormatSize(totalBytes)} ({percent}%)";
    }

    private string FormatUploadFileExistsText(string name)
    {
        var existing = Files.FirstOrDefault(file => string.Equals(file.Name, name, StringComparison.OrdinalIgnoreCase));
        return existing == null
            ? $"File exists: {name}"
            : $"File exists: {name} ({existing.Size})";
    }

    private void PromoteCurrentPreviewToUploadedFile(GCodeFileInfo uploadedFile)
    {
        if (MainVm?.ActiveGCodeDocument == null || PreviewLines.Count == 0)
            return;

        var previewCopy = PreviewLines
            .Select(line => new GCodePreviewLine
            {
                LineNumber = line.LineNumber,
                Text = line.Text,
                IsMetaLine = line.IsMetaLine
            })
            .ToList();

        _previewCache[uploadedFile.Name] = new PreviewCacheEntry(
            uploadedFile.SizeBytes,
            MainVm.ActiveGCodeDocument,
            previewCopy,
            PreviewLineCount);

        _previewDisplayName = uploadedFile.Name;
        _localPreviewPath = null;
        IsLocalPreviewFile = false;
        RaiseFileStateCardProperties();
    }

    private async Task DoUploadAsync(string filePath, bool overwrite)
    {
        if (UploadWindowSize > 1)
        {
            // Run on a background thread so the UI thread is never blocked during the
            // chunk loop. All UI property updates inside are dispatched explicitly.
            await Task.Run(() => DoUploadWindowedAsync(filePath, overwrite));
            return;
        }

        if (HasActiveStorageOperation || IsSdJobActive || MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected) return;

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
        var uploadTouchedStorage = false;
        SetStorageState(DesktopStorageState.Uploading);
        UploadProgress   = 0;
        UploadStatusText = $"Reading {name}…";

        try
        {
            var fileInfo = new FileInfo(filePath);
            long fileSize = fileInfo.Length;
            uint uploadCrc = 0xFFFFFFFFu;

            const long MaxUploadBytes = 5L * 1024 * 1024;
            if (fileSize > MaxUploadBytes)
                throw new InvalidOperationException($"File is too large to upload ({fileSize / 1024}KB). Maximum is {MaxUploadBytes / 1024 / 1024}MB.");

            // ── Phase 1: initiate ──────────────────────────────────────────
            UploadStatusText = $"Connecting: {name}";
            var initAck = await BeginUploadSessionAsync(localUploadChannel, name, fileSize, overwrite, ct);

            if (initAck.Type == UploadAckType.FileExists)
            {
                // Surface overwrite confirm dialog to the user
                UploadFileExistsName = initAck.Name;
                UploadStatusText = FormatUploadFileExistsText(initAck.Name);
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
                throw new InvalidOperationException("Upload not accepted by controller");
            byte transferId = initAck.TransferId;
            int negotiatedChunkSize = initAck.ChunkSize > 0 ? initAck.ChunkSize : TransferRawChunkSize;
            uploadTouchedStorage = true;

            // ── Phase 2: chunks ────────────────────────────────────────────
            int totalChunks = (int)((fileSize + negotiatedChunkSize - 1) / negotiatedChunkSize);
            var buffer = new byte[negotiatedChunkSize];
            long committedBytes = 0;
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
                        chunkAck = await ReadUploadChunkAckAsync(
                            localUploadChannel,
                            TransferChunkTimeout,
                            ct,
                            transferId,
                            (uint)seq,
                            committedBytes,
                            negotiatedChunkSize,
                            fileSize);
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

                committedBytes   = Math.Max(committedBytes, chunkAck.BytesCommitted);
                UploadProgress   = fileSize == 0 ? 1.0 : (double)committedBytes / fileSize;
                UploadStatusText = FormatUploadProgressText(name, committedBytes, fileSize);
            }

            // ── Phase 3: finalise ──────────────────────────────────────────
            UploadStatusText = "Finalizing...";
            var finalCrc = $"{FinalizeCrc32(uploadCrc):x8}";
            MainVm?.DiagnosticsVm.AddLog("UPLOAD", $"finalizing name={name} size={fileSize} crc={finalCrc}");
            MainVm!.Protocol.SendFileUploadEnd(finalCrc);

            var finalAck = await ReadAckAsync(localUploadChannel, TransferFinalizeTimeout, ct);
            if (finalAck.Type != UploadAckType.Complete || finalAck.TransferId != transferId)
                throw new IOException($"Upload verification failed: {finalAck.Reason}");

            UploadStatusText = $"Verifying upload: {name}";
            MainVm!.StatusMessage = $"Verifying upload: {name}";
            MainVm.IsStatusError = false;
            uploadSucceeded = true;
            QueueUploadVerification(name, fileSize);
            _ = RequestFileListAsync();
        }
        catch (OperationCanceledException)
        {
            // User cancel or USB disconnect
            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            ClearPendingUploadVerification();
            UploadStatusText = "Upload cancelled.";
            SetStorageState(DesktopStorageState.Idle);
        }
        catch (Exception ex)
        {
            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            ClearPendingUploadVerification();
            UploadStatusText = $"Upload failed: {ex.Message}";
            SetStorageState(DesktopStorageState.Failed);
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
            if (!uploadSucceeded)
                UploadProgress = 0;
            if (uploadSucceeded)
                SetStorageState(DesktopStorageState.Idle);
            else
                ClearTransferStateToIdle();
            _storageOperationGate.Release();
            if (!uploadSucceeded && uploadTouchedStorage)
                _ = RequestFileListAsync();
        }
    }

    private async Task DoUploadWindowedAsync(string filePath, bool overwrite)
    {
        if (HasActiveStorageOperation || IsSdJobActive || MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected) return;

        await StopPreviewDownloadBeforeStorageCommandAsync();
        await _storageOperationGate.WaitAsync();

        _uploadCancellation = new CancellationTokenSource();
        var ct = _uploadCancellation.Token;

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
        var uploadTouchedStorage = false;
        var uploadTimer = Stopwatch.StartNew();
        Dispatcher.UIThread.Post(() =>
        {
            SetStorageState(DesktopStorageState.Uploading);
            UploadProgress = 0;
            UploadStatusText = $"Reading {name}...";
            MainVm?.DiagnosticsVm.AddLog("UPLOAD", $"start name={name} path={filePath}");
        });

        try
        {
            var fileInfo = new FileInfo(filePath);
            long fileSize = fileInfo.Length;
            uint uploadCrc = 0xFFFFFFFFu;

            const long MaxUploadBytes = 5L * 1024 * 1024;
            if (fileSize > MaxUploadBytes)
                throw new InvalidOperationException($"File is too large to upload ({fileSize / 1024}KB). Maximum is {MaxUploadBytes / 1024 / 1024}MB.");

            Dispatcher.UIThread.Post(() => UploadStatusText = $"Connecting: {name}");
            var initAck = await BeginUploadSessionAsync(localUploadChannel, name, fileSize, overwrite, ct);

            if (initAck.Type == UploadAckType.FileExists)
            {
                var tcs = new TaskCompletionSource<bool>();
                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    UploadFileExistsName = initAck.Name;
                    UploadStatusText = FormatUploadFileExistsText(initAck.Name);
                    _overwriteTcs = tcs;
                    UploadFileExistsRequested?.Invoke(this, initAck.Name);
                });

                bool confirmed = await tcs.Task.WaitAsync(TimeSpan.FromSeconds(60), ct);
                await Dispatcher.UIThread.InvokeAsync(() => { UploadFileExistsName = null; _overwriteTcs = null; });

                if (!confirmed)
                {
                    Dispatcher.UIThread.Post(() => UploadStatusText = "Upload cancelled.");
                    return;
                }

                initAck = await BeginUploadSessionAsync(localUploadChannel, name, fileSize, overwrite: true, ct);
            }

            if (initAck.Type == UploadAckType.Failed)
                throw new InvalidOperationException(initAck.Reason);
            if (initAck.Type != UploadAckType.Ready)
                throw new InvalidOperationException("Upload not accepted by controller");

            byte transferId = initAck.TransferId;
            int negotiatedChunkSize = initAck.ChunkSize > 0 ? initAck.ChunkSize : TransferRawChunkSize;
            int totalChunks = (int)((fileSize + negotiatedChunkSize - 1) / negotiatedChunkSize);
            uploadTouchedStorage = true;
            Dispatcher.UIThread.Post(() =>
                MainVm?.DiagnosticsVm.AddLog(
                    "UPLOAD",
                    $"ready name={name} size={fileSize} chunks={totalChunks} chunk_size={negotiatedChunkSize} window={UploadWindowSize} transfer_id={transferId}"));
            var buffer = new byte[negotiatedChunkSize];
            var inFlight = new Queue<PendingUploadChunk>();
            uint nextSequenceToSend = 0;
            uint highestAckedSequence = uint.MaxValue;
            long highestCommittedBytes = 0;
            int retries = 0;
            uint lastLoggedAckSequence = uint.MaxValue;

            await using var fileStream = new FileStream(
                filePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 81920,
                useAsync: true);

            while ((highestAckedSequence + 1u) < (uint)totalChunks)
            {
                ct.ThrowIfCancellationRequested();

                // Pre-build all window frames into one buffer, then flush in a single Write().
                // This eliminates ~30ms Windows USB CDC per-syscall overhead per chunk.
                var windowBatch = new System.IO.MemoryStream(UploadWindowSize * 8300);
                int batchCount = 0;
                while (nextSequenceToSend < totalChunks && inFlight.Count < UploadWindowSize)
                {
                    int length = await fileStream.ReadAsync(buffer.AsMemory(0, buffer.Length), ct);
                    if (length <= 0)
                        throw new IOException($"Upload stream ended early at chunk {nextSequenceToSend}.");

                    uploadCrc = UpdateCrc32(uploadCrc, buffer, length);

                    var payload = new byte[length];
                    Buffer.BlockCopy(buffer, 0, payload, 0, length);

                    MainVm!.Protocol.BuildUploadDataFrame(transferId, nextSequenceToSend, payload, windowBatch);
                    batchCount++;
                    inFlight.Enqueue(new PendingUploadChunk(nextSequenceToSend, payload, length));
                    nextSequenceToSend++;
                }
                if (batchCount > 0)
                {
                    MainVm!.Protocol.SendPrebuiltFrames(windowBatch, batchCount);
                }

                if (inFlight.Count == 0)
                    break;

                UploadAck chunkAck;
                try
                {
                    chunkAck = await ReadUploadChunkAckAsync(
                        localUploadChannel,
                        TransferChunkTimeout,
                        ct,
                        transferId,
                        inFlight.Peek().Sequence,
                        nextSequenceToSend,
                        highestCommittedBytes,
                        negotiatedChunkSize,
                        fileSize);
                }
                catch (TimeoutException)
                {
                    if (++retries >= TransferMaxRetries)
                        throw new IOException($"Upload timed out waiting for chunk {inFlight.Peek().Sequence} after {TransferMaxRetries} retries.");

                    var retryMsg = $"Upload timeout; resending {inFlight.Count} chunk(s), retry {retries}/{TransferMaxRetries}...";
                    Dispatcher.UIThread.Post(() => UploadStatusText = retryMsg);
                    var retryBatch = new System.IO.MemoryStream(inFlight.Count * 8300);
                    foreach (var pending in inFlight)
                        MainVm!.Protocol.BuildUploadDataFrame(transferId, pending.Sequence, pending.Data, retryBatch);
                    MainVm!.Protocol.SendPrebuiltFrames(retryBatch, inFlight.Count);
                    continue;
                }

                if (chunkAck.Type == UploadAckType.Failed || chunkAck.Type == UploadAckType.Aborted)
                    throw new IOException($"Upload failed at chunk {inFlight.Peek().Sequence}: {chunkAck.Reason}");

                retries = 0;

                while (inFlight.Count > 0 && inFlight.Peek().Sequence <= chunkAck.Seq)
                    inFlight.Dequeue();

                highestAckedSequence = chunkAck.Seq;
                highestCommittedBytes = Math.Max(highestCommittedBytes, chunkAck.BytesCommitted);
                var progress = fileSize == 0 ? 1.0 : (double)highestCommittedBytes / fileSize;
                Dispatcher.UIThread.Post(() => { UploadProgress = progress; UploadStatusText = FormatUploadProgressText(name, highestCommittedBytes, fileSize); });
                if (lastLoggedAckSequence == uint.MaxValue || chunkAck.Seq - lastLoggedAckSequence >= 32 || chunkAck.Seq + 1u >= totalChunks)
                {
                    lastLoggedAckSequence = chunkAck.Seq;
                    var elapsedMs = uploadTimer.Elapsed.TotalMilliseconds;
                    var bps = elapsedMs > 0
                        ? (long)(chunkAck.BytesCommitted * 1000.0 / elapsedMs)
                        : 0L;
                    var logMessage =
                        $"progress name={name} committed={chunkAck.BytesCommitted}/{fileSize} elapsed_ms={(long)elapsedMs} bps={bps} in_flight={inFlight.Count} sent={nextSequenceToSend}/{totalChunks}";
                    Dispatcher.UIThread.Post(() => MainVm?.DiagnosticsVm.AddLog("UPLOAD", logMessage));
                }
            }

            var finalCrc = $"{FinalizeCrc32(uploadCrc):x8}";
            Dispatcher.UIThread.Post(() =>
            {
                UploadStatusText = "Finalizing...";
                MainVm?.DiagnosticsVm.AddLog("UPLOAD", $"finalizing name={name} size={fileSize} crc={finalCrc}");
            });
            MainVm!.Protocol.SendFileUploadEnd(finalCrc);

            var finalAck = await ReadAckAsync(localUploadChannel, TransferFinalizeTimeout, ct);
            if (finalAck.Type != UploadAckType.Complete || finalAck.TransferId != transferId)
                throw new IOException($"Upload verification failed: {finalAck.Reason}");

            uploadSucceeded = true;
            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                QueueUploadVerification(name, fileSize);
                UploadStatusText = $"Verifying upload: {name}";
                MainVm!.StatusMessage = $"Verifying upload: {name}";
                MainVm.IsStatusError = false;
            });
            uploadTimer.Stop();
            Dispatcher.UIThread.Post(() =>
            {
                UploadProgress = 1.0;
            });
        }
        catch (OperationCanceledException)
        {
            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            Dispatcher.UIThread.Post(() => { ClearPendingUploadVerification(); UploadStatusText = "Upload cancelled."; SetStorageState(DesktopStorageState.Idle); });
        }
        catch (Exception ex)
        {
            if (MainVm?.ControllerConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileUploadAbort();
            var errorMsg = $"Upload failed: {ex.Message}";
            Dispatcher.UIThread.Post(() =>
            {
                ClearPendingUploadVerification();
                UploadStatusText = errorMsg;
                SetStorageState(DesktopStorageState.Failed);
                if (MainVm != null) { MainVm.StatusMessage = errorMsg; MainVm.IsStatusError = true; }
            });
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
            _overwriteTcs = null;
            Dispatcher.UIThread.Post(() =>
            {
                if (!uploadSucceeded) UploadProgress = 0;
            });
            _storageOperationGate.Release();
            if (uploadSucceeded)
                Dispatcher.UIThread.Post(() => { SetStorageState(DesktopStorageState.Idle); _ = RequestFileListAsync(); });
            else
                Dispatcher.UIThread.Post(() =>
                {
                    ClearTransferStateToIdle();
                    if (uploadTouchedStorage)
                        _ = RequestFileListAsync();
                });
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

    private async Task<UploadAck> ReadUploadChunkAckAsync(
        Channel<UploadAck> channel,
        TimeSpan timeout,
        CancellationToken ct,
        byte transferId,
        uint sequence,
        long committedBytes,
        int chunkSize,
        long fileSize)
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

            if (ack.Type == UploadAckType.ChunkOk &&
                ack.TransferId == transferId &&
                ack.Seq == sequence &&
                IsCommittedUploadAck(ack, committedBytes, chunkSize, fileSize))
            {
                return ack;
            }
        }
    }

    private async Task<UploadAck> ReadUploadChunkAckAsync(
        Channel<UploadAck> channel,
        TimeSpan timeout,
        CancellationToken ct,
        byte transferId,
        uint minimumSequence,
        uint nextSequenceToSend,
        long committedBytes,
        int chunkSize,
        long fileSize)
    {
        var deadline = DateTime.UtcNow + timeout;

        while (true)
        {
            var remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
                throw new TimeoutException("Storage transfer timed out.");

            var ack = await ReadChannelMessageAsync(channel.Reader, remaining, ct, "Storage transfer timed out.");
            if (ack.Type == UploadAckType.Failed || ack.Type == UploadAckType.Aborted)
                return ack;

            if (ack.Type == UploadAckType.ChunkOk &&
                ack.TransferId == transferId &&
                ack.Seq >= minimumSequence &&
                ack.Seq < nextSequenceToSend &&
                IsCommittedUploadAck(ack, committedBytes, chunkSize, fileSize))
            {
                return ack;
            }
        }
    }

    private static bool IsCommittedUploadAck(UploadAck ack, long committedBytes, int chunkSize, long fileSize)
    {
        if (chunkSize <= 0)
            return ack.BytesCommitted > committedBytes;

        var expectedCommitted = Math.Min(fileSize, ((long)ack.Seq + 1) * chunkSize);
        return ack.BytesCommitted >= expectedCommitted &&
               ack.BytesCommitted > committedBytes;
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
        ToolpathStatusMessage  = "";
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
        bool hadActiveDownload = _previewDownloadCancellation != null;
        if (StorageState == DesktopStorageState.DownloadingPreview)
            SetStorageState(DesktopStorageState.Aborting);
        CancelPendingPreviewDownload();
        if (hadActiveDownload)
            await AbortRemotePreviewDownloadAsync();
    }

    private async Task AbortRemotePreviewDownloadAsync()
    {
        if (MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
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
        ClearTransferStateToIdle();
    }

    private void CancelSelectedFilePreviewRequest()
    {
        _selectionDebounceCts?.Cancel();
        _selectionDebounceCts = null;
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
        CancelLoadedJobPreviewRequest();
        CancelPendingPreviewDownload();
        CancelSelectedFilePreviewRequest();
        ClearPreviewCache();
        _overwriteTcs?.TrySetResult(false);
        _overwriteTcs = null;
        _replaceLoadTcs?.TrySetResult(false);
        _replaceLoadTcs = null;
        _unloadLoadedJobTcs?.TrySetResult(false);
        _unloadLoadedJobTcs = null;
        _pendingFileList.Clear();
        _pendingDeletes.Clear();

        Files.Clear();
        SelectedFile = null;
        SdFreeBytes = -1;

        if (!IsLocalPreviewFile)
        {
            ResetPreviewState();
        }

        SetStorageState(DesktopStorageState.Idle);
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

    private void ClearPreviewCache()
    {
        _previewCache.Clear();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    private void QueueLoadedJobPreviewIfNeeded()
    {
        if (MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
            return;

        if (SelectedFile != null && !IsSelectedFileLoaded && !IsLocalPreviewFile)
            return;

        var loadedName = MainVm.CurrentFileName;
        if (string.IsNullOrWhiteSpace(loadedName))
        {
            CancelLoadedJobPreviewRequest();
            return;
        }

        if (!string.IsNullOrWhiteSpace(_previewDisplayName) && !IsPreviewIdentityForFile(loadedName))
            return;

        if (IsCurrentPreviewForDeviceFile(loadedName))
            return;

        if (IsSdJobActive || (HasActiveStorageOperation && !IsPreviewDownloadActive))
            return;

        var loadedFile = Files.FirstOrDefault(file =>
            string.Equals(file.Name, loadedName, StringComparison.OrdinalIgnoreCase));
        if (loadedFile == null)
            return;

        if (string.Equals(_loadedJobPreviewInFlightName, loadedName, StringComparison.OrdinalIgnoreCase))
            return;

        CancelLoadedJobPreviewRequest();
        _loadedJobPreviewCts = new CancellationTokenSource();
        _loadedJobPreviewInFlightName = loadedName;
        _ = BeginLoadedJobPreviewAsync(loadedFile, _loadedJobPreviewCts);
    }

    private void QueueSelectedFilePreviewIfNeeded()
    {
        if (MainVm?.ControllerConnectionStatus != ConnectionStatus.Connected)
            return;

        if (IsLocalPreviewFile || SelectedFile == null)
        {
            QueueLoadedJobPreviewIfNeeded();
            return;
        }

        if (IsSdJobActive || (HasActiveStorageOperation && !IsPreviewDownloadActive))
            return;

        if (IsCurrentPreviewForDeviceFile(SelectedFile.Name))
            return;

        CancelLoadedJobPreviewRequest();
        CancelSelectedFilePreviewRequest();
        _selectionDebounceCts = new CancellationTokenSource();
        _ = BeginSelectedFilePreviewAsync(SelectedFile, _selectionDebounceCts.Token, debounce: false);
    }

    private async Task BeginLoadedJobPreviewAsync(GCodeFileInfo file, CancellationTokenSource cancellation)
    {
        try
        {
            await BeginSelectedFilePreviewAsync(file, cancellation.Token);
        }
        finally
        {
            cancellation.Dispose();
            if (ReferenceEquals(_loadedJobPreviewCts, cancellation))
            {
                _loadedJobPreviewCts = null;
                _loadedJobPreviewInFlightName = null;
            }
        }
    }

    private void CancelLoadedJobPreviewRequest()
    {
        _loadedJobPreviewCts?.Cancel();
        _loadedJobPreviewCts = null;
        _loadedJobPreviewInFlightName = null;
    }

    private bool IsCurrentPreviewForDeviceFile(string fileName)
    {
        if (IsLocalPreviewFile || !IsPreviewIdentityForFile(fileName))
            return false;

        return MainVm?.ActiveGCodeDocument != null || IsParsingToolpath || IsPreviewDownloadActive;
    }

    private bool IsPreviewIdentityForFile(string fileName)
        => string.Equals(GetPreviewBaseName(), fileName, StringComparison.OrdinalIgnoreCase);

    private void SelectLoadedFileIfAvailable()
    {
        var loadedName = MainVm?.CurrentFileName;
        if (string.IsNullOrWhiteSpace(loadedName))
            return;

        SelectFileByName(loadedName);
    }

    private void SelectFileByName(string fileName)
    {
        var loadedFile = Files.FirstOrDefault(file =>
            string.Equals(file.Name, fileName, StringComparison.OrdinalIgnoreCase));
        if (loadedFile != null && !ReferenceEquals(SelectedFile, loadedFile))
            SelectedFile = loadedFile;
    }

    private void RaiseCanExecuteAll()
    {
        ((RelayCommand)UploadCommand).RaiseCanExecuteChanged();
        ((RelayCommand)CancelUploadCommand).RaiseCanExecuteChanged();
        ((RelayCommand)PreviewLocalCommand).RaiseCanExecuteChanged();
        ((RelayCommand)UploadLocalPreviewCommand).RaiseCanExecuteChanged();
        ((RelayCommand)RefreshCommand).RaiseCanExecuteChanged();
        ((RelayCommand)LoadSelectedFileCommand).RaiseCanExecuteChanged();
        ((RelayCommand)UnloadLoadedFileCommand).RaiseCanExecuteChanged();
        ((RelayCommand<GCodeFileInfo>)DeleteCommand).RaiseCanExecuteChanged();
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
        RaisePropertyChanged(nameof(LoadedJobPrimaryActionText));
        RaisePropertyChanged(nameof(LoadedJobFileName));
        RaisePropertyChanged(nameof(LoadedJobSizeText));
        RaisePropertyChanged(nameof(LoadedJobLinesText));
        RaisePropertyChanged(nameof(LoadedJobStatusText));
        RaisePropertyChanged(nameof(LoadedJobStatusBrush));
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

    private string GetLoadedJobLineSummary()
    {
        var loadedName = MainVm?.CurrentFileName;
        if (string.IsNullOrWhiteSpace(loadedName))
            return "--";

        var activeDocumentLines = MainVm?.ActiveGCodeDocument?.TotalLines ?? 0;
        if (activeDocumentLines > 0 && IsPreviewIdentityForFile(loadedName))
            return activeDocumentLines.ToString();

        var reportedLines = MainVm?.TotalLines ?? 0;
        return reportedLines > 0 ? reportedLines.ToString() : "--";
    }

    private string? GetLocalPreviewName()
        => GetPreviewBaseName();

    private string? GetPreviewBaseName()
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
            crc = Crc32Table[(int)((crc ^ data[index]) & 0xFFu)] ^ (crc >> 8);

        return crc;
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
    public string Name      { get; set; } = "";
    public string FullPath  { get; set; } = "";
    public string Size      { get; set; } = "";
    public string Modified  { get; set; } = "";
    public long   SizeBytes { get; set; }
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
