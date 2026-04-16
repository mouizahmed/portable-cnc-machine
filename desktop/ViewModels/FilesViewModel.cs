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
    private const int PreviewMaxLines  = 200;
    private const int UploadChunkSize  = 192;   // raw bytes per chunk → 256 base64 chars

    // ── Preview fields ────────────────────────────────────────────────────────
    private CancellationTokenSource? _parseCancellation;
    private bool   _toolpathHasGeometry;
    private bool   _toolpathHasError;
    private int    _previewLineCount;
    private bool   _isParsingToolpath;
    private string _toolpathStatusMessage = "Use 'Preview local file' to inspect a file.";
    private string _toolpathWarningSummary = "";
    private string _globalWarningSummary   = "";
    private bool   _isLocalPreviewFile;
    private string? _localPreviewPath;

    // ── SD-list fields ────────────────────────────────────────────────────────
    private readonly List<GCodeFileInfo> _pendingFileList = new();
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

    // Upload ACK discriminated union
    private enum UploadAckType { Ready, ChunkOk, Complete, Aborted, Failed, FileExists }
    private record UploadAck(UploadAckType Type, int Seq = 0, string Name = "",
                              long Size = 0, string Reason = "");

    // ─────────────────────────────────────────────────────────────────────────
    // Constructor
    // ─────────────────────────────────────────────────────────────────────────

    public FilesViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;
        Files.CollectionChanged += (_, _) => RaisePropertyChanged(nameof(HasNoFiles));

        UploadCommand             = new RelayCommand(StartUpload,            CanUpload);
        CancelUploadCommand       = new RelayCommand(CancelUpload,           () => IsUploading);
        PreviewLocalCommand       = new RelayCommand(PreviewLocalFile,       () => !IsUploading);
        UploadLocalPreviewCommand = new RelayCommand(StartUploadLocalPreview, CanUploadLocalPreview);
        RefreshCommand            = new RelayCommand(RefreshFileList,        () => !IsUploading);
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
            RaiseCanExecuteAll();

            if (value == null) return;

            // Auto-send FileSelect to Pico when an SD file is clicked
            if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
                MainVm.Protocol.SendFileSelect(value.Name);

            if (MainVm != null)
            {
                MainVm.CurrentFileName = value.Name;
                MainVm.StatusMessage   = $"Selected: {value.Name}";
            }
        }
    }

    public bool HasSelectedFile => SelectedFile != null;
    public bool HasNoFiles      => Files.Count == 0;

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

    public string FilePreviewTitle
        => IsLocalPreviewFile && _localPreviewPath != null
            ? $"{Path.GetFileName(_localPreviewPath)} (local)"
            : "No local preview loaded";

    public bool HasPreviewContent => PreviewLines.Count > 0;
    public bool IsPreviewEmpty    => !HasPreviewContent;

    public string PreviewEmptyMessage
        => IsLocalPreviewFile ? "No source lines to display." : "Use 'Preview local file' to inspect a file.";

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
        MainVm.Protocol.UploadReadyReceived      += name         => RouteUpload(new UploadAck(UploadAckType.Ready,      Name: name));
        MainVm.Protocol.ChunkAckReceived         += seq          => RouteUpload(new UploadAck(UploadAckType.ChunkOk,    Seq: seq));
        MainVm.Protocol.UploadCompleteReceived   += (name, size) => RouteUpload(new UploadAck(UploadAckType.Complete,  Name: name, Size: size));
        MainVm.Protocol.UploadAbortedReceived    += ()           => RouteUpload(new UploadAck(UploadAckType.Aborted));
        MainVm.Protocol.UploadFailedReceived     += reason       => RouteUpload(new UploadAck(UploadAckType.Failed,    Reason: reason));
        MainVm.Protocol.UploadFileExistsReceived += name         => RouteUpload(new UploadAck(UploadAckType.FileExists, Name: name));

        // File operation confirmations
        MainVm.Protocol.FileDeleteConfirmed += HandleFileDeleteConfirmed;
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        if (propertyName != nameof(MainWindowViewModel.PiConnectionStatus)) return;

        var status = MainVm?.PiConnectionStatus;

        if (status == ConnectionStatus.Connected)
            RequestFileList();
        else if (IsUploading)
            _uploadCancellation?.Cancel(); // USB disconnect mid-upload — no abort command, just cancel

        RaisePropertyChanged(nameof(ShowLocalPreviewBanner));
        RaiseCanExecuteAll();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // File list (SD card)
    // ─────────────────────────────────────────────────────────────────────────

    private void RequestFileList()
    {
        _pendingFileList.Clear();
        MainVm?.Protocol.SendFileList();
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
        Files.Clear();
        SelectedFile = null;
        foreach (var entry in _pendingFileList)
            Files.Add(entry);
        _pendingFileList.Clear();
        SdFreeBytes = freeBytes;
        // Toolpath preview is independent — do not reset it here.
    }

    private void HandleProtocolEvent(string name, IReadOnlyDictionary<string, string> _)
    {
        switch (name)
        {
            case "SD_MOUNTED":
                RequestFileList();
                break;

            case "SD_REMOVED":
                Files.Clear();
                SelectedFile = null;
                SdFreeBytes  = -1;
                break;
        }
    }

    private void RefreshFileList()
    {
        if (MainVm?.PiConnectionStatus == ConnectionStatus.Connected)
            RequestFileList();
    }

    private void DeleteFile(GCodeFileInfo? file)
    {
        if (file == null || MainVm == null) return;
        _pendingDeletes[file.Name] = file;
        if (SelectedFile == file)
            SelectedFile = null;
        MainVm.Protocol.SendFileDelete(file.Name);
    }

    private void HandleFileDeleteConfirmed(string name)
    {
        if (_pendingDeletes.TryGetValue(name, out var file))
        {
            _pendingDeletes.Remove(name);
            Files.Remove(file);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Local preview
    // ─────────────────────────────────────────────────────────────────────────

    private async void PreviewLocalFile()
    {
        var path = await PickGCodeFileAsync();
        if (path == null) return;
        await BeginLocalPreviewAsync(path);
    }

    private async Task BeginLocalPreviewAsync(string filePath)
    {
        _localPreviewPath  = filePath;
        IsLocalPreviewFile = true;
        ResetPreviewState(keepLocalPreviewFlag: true);

        if (!File.Exists(filePath))
        {
            IsLocalPreviewFile = false;
            _localPreviewPath  = null;
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
            IsLocalPreviewFile = false;
            _localPreviewPath  = null;
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
        => !IsUploading && MainVm?.PiConnectionStatus == ConnectionStatus.Connected;

    private bool CanUploadLocalPreview()
        => IsLocalPreviewFile && !IsUploading && MainVm?.PiConnectionStatus == ConnectionStatus.Connected;

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

    private void RouteUpload(UploadAck ack) => _uploadChannel?.Writer.TryWrite(ack);

    private async Task DoUploadAsync(string filePath, bool overwrite)
    {
        if (IsUploading || MainVm?.PiConnectionStatus != ConnectionStatus.Connected) return;

        _uploadCancellation = new CancellationTokenSource();
        var ct = _uploadCancellation.Token;

        _uploadChannel = Channel.CreateBounded<UploadAck>(new BoundedChannelOptions(16)
        {
            SingleReader = true,
            FullMode     = BoundedChannelFullMode.DropOldest
        });

        var name = Path.GetFileName(filePath);
        IsUploading      = true;
        UploadProgress   = 0;
        UploadStatusText = $"Reading {name}…";

        try
        {
            var bytes = await File.ReadAllBytesAsync(filePath, ct);
            var crc   = ComputeCrc32(bytes);

            // ── Phase 1: initiate ──────────────────────────────────────────
            UploadStatusText = $"Connecting: {name}";
            MainVm!.Protocol.SendFileUpload(name, bytes.Length, overwrite);

            var initAck = await ReadAckAsync(TimeSpan.FromSeconds(3), ct);

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
                MainVm!.Protocol.SendFileUpload(name, bytes.Length, overwrite: true);
                initAck = await ReadAckAsync(TimeSpan.FromSeconds(3), ct);
            }

            if (initAck.Type == UploadAckType.Failed)
                throw new InvalidOperationException(initAck.Reason);
            if (initAck.Type != UploadAckType.Ready)
                throw new InvalidOperationException("Upload not accepted by Pico");

            // ── Phase 2: chunks ────────────────────────────────────────────
            int totalChunks = (bytes.Length + UploadChunkSize - 1) / UploadChunkSize;

            for (int seq = 0; seq < totalChunks; seq++)
            {
                ct.ThrowIfCancellationRequested();

                int offset = seq * UploadChunkSize;
                int length = Math.Min(UploadChunkSize, bytes.Length - offset);
                var base64 = Convert.ToBase64String(bytes, offset, length);

                int retries = 0;
                while (true)
                {
                    MainVm!.Protocol.SendChunk(seq, base64);

                    UploadAck chunkAck;
                    try
                    {
                        chunkAck = await ReadAckAsync(TimeSpan.FromSeconds(5), ct);
                    }
                    catch (TimeoutException)
                    {
                        if (++retries >= 3)
                            throw new IOException($"Chunk {seq} timed out after 3 retries.");
                        UploadStatusText = $"Chunk {seq} timeout — retry {retries}/3…";
                        continue;
                    }

                    if (chunkAck.Type == UploadAckType.ChunkOk && chunkAck.Seq == seq)
                        break;

                    // Unrecoverable SD errors — abort immediately
                    if (chunkAck.Type == UploadAckType.Failed &&
                        (chunkAck.Reason.Contains("SD_WRITE_FAIL") ||
                         chunkAck.Reason.Contains("UPLOAD_SD_REMOVED") ||
                         chunkAck.Reason.Contains("UNKNOWN")))
                        throw new IOException($"SD write failed at chunk {seq}: {chunkAck.Reason}");

                    if (++retries >= 3)
                        throw new IOException($"Chunk {seq} failed after 3 retries.");
                }

                UploadProgress   = (double)(seq + 1) / totalChunks;
                UploadStatusText = $"Uploading {name}: {(int)(UploadProgress * 100)}%";
            }

            // ── Phase 3: finalise ──────────────────────────────────────────
            UploadStatusText = "Verifying…";
            MainVm!.Protocol.SendFileUploadEnd($"{crc:x8}");

            var finalAck = await ReadAckAsync(TimeSpan.FromSeconds(5), ct);
            if (finalAck.Type != UploadAckType.Complete)
                throw new IOException($"Upload verification failed: {finalAck.Reason}");

            // Success
            UploadProgress        = 1.0;
            UploadStatusText      = $"Uploaded: {name}";
            IsLocalPreviewFile    = false;           // file is now on SD
            MainVm!.StatusMessage = $"Uploaded: {name}";
            MainVm.IsStatusError  = false;
            RequestFileList();
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
            _uploadChannel?.Writer.TryComplete();
            _uploadChannel = null;
            _uploadCancellation?.Dispose();
            _uploadCancellation = null;
            _overwriteTcs?.TrySetResult(false);
            _overwriteTcs  = null;
            IsUploading    = false;
            UploadProgress = 0;
        }
    }

    private async Task<UploadAck> ReadAckAsync(TimeSpan timeout, CancellationToken ct)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(timeout);
        try
        {
            return await _uploadChannel!.Reader.ReadAsync(linked.Token);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new TimeoutException("Pico did not respond within the timeout period.");
        }
    }

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
            IsLocalPreviewFile = false;
            _localPreviewPath  = null;
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

    private void UpdateToolpathState(bool hasGeometry, bool hasError)
    {
        _toolpathHasGeometry = hasGeometry;
        _toolpathHasError    = hasError;
        RaisePropertyChanged(nameof(ToolpathStateLabel));
        RaisePropertyChanged(nameof(ToolpathStateBrush));
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
    {
        uint crc = 0xFFFFFFFF;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int i = 0; i < 8; i++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return ~crc;
    }

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
