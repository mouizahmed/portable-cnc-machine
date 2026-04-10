using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.ViewModels;

public sealed class FilesViewModel : PageViewModelBase
{
    private const int PreviewMaxLines = 200;

    private CancellationTokenSource? _parseCancellation;
    private bool _toolpathHasGeometry;
    private bool _toolpathHasError;
    private GCodeFileInfo? _selectedFile;
    private int _previewLineCount;
    private bool _isParsingToolpath;
    private string _toolpathStatusMessage = "Select a file to build the toolpath preview.";
    private string _toolpathWarningSummary = "";
    private string _globalWarningSummary = "";

    public FilesViewModel()
    {
        BrowseCommand = new RelayCommand(BrowseForFile);
        LoadSelectedCommand = new RelayCommand(LoadSelectedFile, () => HasSelectedFile);
        RefreshCommand = new RelayCommand(RefreshFileList);
        RemoveCommand = new RelayCommand(RemoveSelectedFile, () => HasSelectedFile);
    }

    public event EventHandler<ParseErrorDialogRequest>? ParseErrorDialogRequested;

    public ObservableCollection<GCodeFileInfo> Files { get; } = new();
    public ObservableCollection<GCodeWarningInfo> ToolpathWarnings { get; } = new();
    public ObservableCollection<GCodePreviewLine> PreviewLines { get; } = new();

    public GCodeFileInfo? SelectedFile
    {
        get => _selectedFile;
        set
        {
            if (SetProperty(ref _selectedFile, value))
            {
                RaisePropertyChanged(nameof(HasSelectedFile));
                RaisePropertyChanged(nameof(FilePreviewTitle));
                RaisePropertyChanged(nameof(SelectedFilePathSummary));
                RaisePropertyChanged(nameof(SelectedFileModifiedSummary));
                RaisePropertyChanged(nameof(SelectedFileLineSummary));
                RaisePropertyChanged(nameof(PreviewViewportSummary));
                RaisePropertyChanged(nameof(ToolpathStateLabel));
                RaisePropertyChanged(nameof(ToolpathStateBrush));
                ((RelayCommand)LoadSelectedCommand).RaiseCanExecuteChanged();
                ((RelayCommand)RemoveCommand).RaiseCanExecuteChanged();
                LoadFilePreview();
            }
        }
    }

    public bool HasSelectedFile => SelectedFile != null;

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
            {
                RaisePropertyChanged(nameof(WarningDetail));
            }
        }
    }

    public string FilePreviewTitle => SelectedFile?.Name ?? "No file selected";

    public bool HasPreviewContent => PreviewLines.Count > 0;

    public bool IsPreviewEmpty => !HasPreviewContent;

    public string PreviewEmptyMessage => SelectedFile == null
        ? "Select a file to inspect."
        : "No source lines to display.";

    public string SelectedFilePathSummary
        => SelectedFile?.FullPath ?? string.Empty;

    public string SelectedFileModifiedSummary
        => SelectedFile == null ? "Nothing selected" : $"Updated {SelectedFile.Modified}";

    public string SelectedFileLineSummary
        => PreviewLineCount > 0 ? $"{PreviewLineCount} total lines" : "--";

    public string PreviewViewportSummary
    {
        get
        {
            if (SelectedFile == null)
            {
                return "Choose a file from the library to inspect its source.";
            }

            if (PreviewLineCount <= 0)
            {
                return "Source preview unavailable.";
            }

            return PreviewLineCount > PreviewMaxLines
                ? $"Showing first {PreviewMaxLines} of {PreviewLineCount} lines"
                : $"{PreviewLineCount} line{(PreviewLineCount == 1 ? string.Empty : "s")} shown";
        }
    }

    public string ToolpathStateLabel
    {
        get
        {
            if (SelectedFile == null)
            {
                return "NO FILE";
            }

            if (IsParsingToolpath)
            {
                return "PARSING";
            }

            if (_toolpathHasError)
            {
                return "ERROR";
            }

            return _toolpathHasGeometry ? "READY" : "NO PATH";
        }
    }

    public IBrush ToolpathStateBrush => ToolpathStateLabel switch
    {
        "READY" => new SolidColorBrush(Color.Parse("#3BB273")),
        "PARSING" => new SolidColorBrush(Color.Parse("#5B9BD5")),
        "ERROR" => new SolidColorBrush(Color.Parse("#D83B3B")),
        "NO PATH" => new SolidColorBrush(Color.Parse("#E0A100")),
        _ => new SolidColorBrush(Color.Parse("#3A3A3A"))
    };

    public string WarningDetail
    {
        get
        {
            if (ToolpathWarnings.Count == 0)
            {
                return "No parse warnings.";
            }

            var highlightedCount = ToolpathWarnings.Count(warning => warning.LineNumber > 0 && warning.IsVisibleInPreview);
            var hiddenCount = ToolpathWarnings.Count(warning => warning.LineNumber > 0 && !warning.IsVisibleInPreview);
            var globalCount = ToolpathWarnings.Count(warning => warning.LineNumber <= 0);

            var parts = new List<string> { ToolpathWarningSummary };
            if (highlightedCount > 0)
            {
                parts.Add("Click warning icons in the source preview to inspect them.");
            }

            if (hiddenCount > 0)
            {
                parts.Add($"{hiddenCount} warning{(hiddenCount == 1 ? string.Empty : "s")} fall outside the visible preview.");
            }

            if (globalCount > 0)
            {
                parts.Add($"{globalCount} warning{(globalCount == 1 ? string.Empty : "s")} are file-wide.");
            }

            return string.Join(" ", parts);
        }
    }

    public bool HasGlobalWarnings => !string.IsNullOrWhiteSpace(GlobalWarningSummary);

    public string GlobalWarningSummary
    {
        get => _globalWarningSummary;
        private set => SetProperty(ref _globalWarningSummary, value);
    }

    public ICommand BrowseCommand { get; }
    public ICommand LoadSelectedCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand RemoveCommand { get; }

    private async void BrowseForFile()
    {
        var lifetime = Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime;
        var window = lifetime?.MainWindow;
        if (window == null)
        {
            return;
        }

        var picked = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select G-code file",
            AllowMultiple = true,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("G-code")
                {
                    Patterns = new[] { "*.gcode", "*.nc", "*.ngc", "*.cnc", "*.tap" }
                },
                new FilePickerFileType("All files") { Patterns = new[] { "*.*" } }
            }
        });

        GCodeFileInfo? lastAdded = null;
        foreach (var file in picked)
        {
            var path = file.Path.LocalPath;
            if (Files.Any(f => f.FullPath == path))
            {
                continue;
            }

            var info = new FileInfo(path);
            lastAdded = new GCodeFileInfo
            {
                Name = info.Name,
                FullPath = path,
                Size = FormatSize(info.Length),
                Modified = info.LastWriteTime.ToString("yyyy-MM-dd")
            };
            Files.Add(lastAdded);
        }

        if (lastAdded != null)
        {
            SelectedFile = lastAdded;
        }
    }

    private void LoadSelectedFile()
    {
        if (SelectedFile == null || MainVm == null)
        {
            return;
        }

        MainVm.CurrentFileName = SelectedFile.Name;
        MainVm.TotalLines = MainVm.ActiveGCodeDocument?.TotalLines ?? PreviewLineCount;
        MainVm.CurrentLine = 0;
        MainVm.Progress = 0;
        MainVm.StatusMessage = $"Loaded: {SelectedFile.Name} ({PreviewLineCount} lines)";
        MainVm.DashboardVm.PreviewLine = 0;
        MainVm.DashboardVm.ScrubberValue = 0;
    }

    private void RefreshFileList()
    {
        var existing = Files.ToList();
        Files.Clear();
        SelectedFile = null;

        foreach (var file in existing)
        {
            if (!File.Exists(file.FullPath))
            {
                continue;
            }

            var info = new FileInfo(file.FullPath);
            Files.Add(new GCodeFileInfo
            {
                Name = info.Name,
                FullPath = file.FullPath,
                Size = FormatSize(info.Length),
                Modified = info.LastWriteTime.ToString("yyyy-MM-dd")
            });
        }

        ResetPreviewState();
    }

    private void RemoveSelectedFile()
    {
        if (SelectedFile == null)
        {
            return;
        }

        Files.Remove(SelectedFile);
        SelectedFile = null;
        ResetPreviewState();
    }

    private void LoadFilePreview()
    {
        ResetPreviewState();

        if (SelectedFile == null || !File.Exists(SelectedFile.FullPath))
        {
            return;
        }

        try
        {
            int total = 0;

            foreach (var line in File.ReadLines(SelectedFile.FullPath))
            {
                total++;
                if (total <= PreviewMaxLines)
                {
                    PreviewLines.Add(new GCodePreviewLine
                    {
                        LineNumber = total,
                        Text = line
                    });
                }
            }

            PreviewLineCount = total;

            if (total > PreviewMaxLines)
            {
                PreviewLines.Add(new GCodePreviewLine
                {
                    LineNumber = 0,
                    Text = $"; ... ({total - PreviewMaxLines} more lines not shown)",
                    IsMetaLine = true
                });
            }

            RaisePropertyChanged(nameof(HasPreviewContent));
            RaisePropertyChanged(nameof(IsPreviewEmpty));
        }
        catch (Exception ex)
        {
            PreviewLines.Clear();
            PreviewLines.Add(new GCodePreviewLine
            {
                LineNumber = 0,
                Text = $"; Error reading file: {ex.Message}",
                IsMetaLine = true
            });
            PreviewLineCount = 0;
            RaisePropertyChanged(nameof(HasPreviewContent));
            RaisePropertyChanged(nameof(IsPreviewEmpty));
            UpdateToolpathState(hasGeometry: false, hasError: true);
            ToolpathStatusMessage = "Source preview could not be loaded.";
            ToolpathWarningSummary = "Preview failed before toolpath parsing could start.";
            if (MainVm != null)
            {
                MainVm.ActiveGCodeDocument = null;
            }

            RaiseParseErrorDialog(
                "File Read Error",
                $"Unable to read '{SelectedFile.Name}'.",
                $"{SelectedFile.FullPath}\n\n{ex}");
            return;
        }

        BeginToolpathParse();
    }

    private async void BeginToolpathParse()
    {
        if (SelectedFile == null || !File.Exists(SelectedFile.FullPath))
        {
            return;
        }

        CancelPendingParse();

        var selectedPath = SelectedFile.FullPath;
        var cancellation = new CancellationTokenSource();
        _parseCancellation = cancellation;

        IsParsingToolpath = true;
        UpdateToolpathState(hasGeometry: false, hasError: false);
        ClearWarnings();
        ToolpathStatusMessage = "Parsing toolpath geometry...";
        ToolpathWarningSummary = "";

        try
        {
            var document = await GCodeParser.ParseFileAsync(selectedPath, cancellation.Token);
            if (cancellation.IsCancellationRequested || SelectedFile?.FullPath != selectedPath)
            {
                return;
            }

            MainVm?.ActiveGCodeDocument = document;
            if (MainVm != null)
            {
                MainVm.TotalLines = document.TotalLines;
            }

            UpdateToolpathState(hasGeometry: document.HasGeometry, hasError: false);
            UpdateWarnings(document.Warnings);
            ToolpathStatusMessage = document.HasGeometry
                ? $"Toolpath ready: {document.Segments.Length} segments"
                : "No motion geometry was found in the selected file.";
            ToolpathWarningSummary = document.WarningCount == 0
                ? "No parse warnings."
                : $"{document.WarningCount} parse warning{(document.WarningCount == 1 ? string.Empty : "s")}.";
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            if (SelectedFile?.FullPath == selectedPath)
            {
                UpdateToolpathState(hasGeometry: false, hasError: true);
                ClearWarnings();
                ToolpathStatusMessage = "Toolpath parse failed.";
                ToolpathWarningSummary = "The parser could not build a preview for this file.";
                if (MainVm != null)
                {
                    MainVm.ActiveGCodeDocument = null;
                }

                RaiseParseErrorDialog(
                    "Toolpath Parse Error",
                    $"Unable to parse '{SelectedFile.Name}'.",
                    $"{SelectedFile.FullPath}\n\n{ex}");
            }
        }
        finally
        {
            if (_parseCancellation == cancellation)
            {
                _parseCancellation = null;
                IsParsingToolpath = false;
            }

            cancellation.Dispose();
        }
    }

    private void UpdateWarnings(IEnumerable<GCodeParseWarning> warnings)
    {
        ToolpathWarnings.Clear();
        foreach (var warning in warnings)
        {
            ToolpathWarnings.Add(new GCodeWarningInfo
            {
                LineNumber = warning.LineNumber,
                ScopeLabel = warning.LineNumber > 0 ? $"Line {warning.LineNumber}" : "Global",
                Message = warning.Message,
                IsVisibleInPreview = warning.LineNumber > 0 && warning.LineNumber <= PreviewMaxLines
            });
        }

        var warningLookup = ToolpathWarnings
            .Where(warning => warning.LineNumber > 0)
            .GroupBy(warning => warning.LineNumber)
            .ToDictionary(
                group => group.Key,
                group => string.Join("\n", group.Select(warning => $"- {warning.Message}")));

        foreach (var previewLine in PreviewLines.Where(line => line.LineNumber > 0))
        {
            if (warningLookup.TryGetValue(previewLine.LineNumber, out var tooltip))
            {
                previewLine.HasWarning = true;
                previewLine.WarningTooltip = tooltip;
            }
            else
            {
                previewLine.HasWarning = false;
                previewLine.WarningTooltip = string.Empty;
            }
        }

        GlobalWarningSummary = string.Join(
            "\n",
            ToolpathWarnings
                .Where(warning => warning.LineNumber <= 0)
                .Select(warning => warning.Message));

        RaisePropertyChanged(nameof(HasGlobalWarnings));
        RaisePropertyChanged(nameof(WarningDetail));
    }

    private void ClearWarnings()
    {
        ToolpathWarnings.Clear();
        foreach (var previewLine in PreviewLines)
        {
            previewLine.HasWarning = false;
            previewLine.WarningTooltip = string.Empty;
        }

        GlobalWarningSummary = "";
        RaisePropertyChanged(nameof(HasGlobalWarnings));
        RaisePropertyChanged(nameof(WarningDetail));
    }

    private void ResetPreviewState()
    {
        PreviewLines.Clear();
        PreviewLineCount = 0;
        RaisePropertyChanged(nameof(HasPreviewContent));
        RaisePropertyChanged(nameof(IsPreviewEmpty));
        RaisePropertyChanged(nameof(PreviewEmptyMessage));
        UpdateToolpathState(hasGeometry: false, hasError: false);
        ClearWarnings();
        ToolpathStatusMessage = "Select a file to build the toolpath preview.";
        ToolpathWarningSummary = "";
        CancelPendingParse();
        if (MainVm != null)
        {
            MainVm.ActiveGCodeDocument = null;
            MainVm.DashboardVm.PreviewLine = 0;
            MainVm.DashboardVm.ScrubberValue = 0;
        }
    }

    private void CancelPendingParse()
    {
        if (_parseCancellation == null)
        {
            return;
        }

        _parseCancellation.Cancel();
        _parseCancellation.Dispose();
        _parseCancellation = null;
        IsParsingToolpath = false;
    }

    private void UpdateToolpathState(bool hasGeometry, bool hasError)
    {
        _toolpathHasGeometry = hasGeometry;
        _toolpathHasError = hasError;
        RaisePropertyChanged(nameof(ToolpathStateLabel));
        RaisePropertyChanged(nameof(ToolpathStateBrush));
    }

    private void RaiseParseErrorDialog(string title, string summary, string details)
    {
        ParseErrorDialogRequested?.Invoke(this, new ParseErrorDialogRequest(title, summary, details));
    }

    private static string FormatSize(long bytes)
    {
        if (bytes < 1024)
        {
            return $"{bytes} B";
        }

        if (bytes < 1024 * 1024)
        {
            return $"{bytes / 1024.0:F1} KB";
        }

        return $"{bytes / (1024.0 * 1024):F1} MB";
    }
}

public sealed class GCodeFileInfo
{
    public string Name { get; set; } = "";
    public string FullPath { get; set; } = "";
    public string Size { get; set; } = "";
    public string Modified { get; set; } = "";
}

public sealed class GCodeWarningInfo
{
    public int LineNumber { get; set; }
    public string ScopeLabel { get; set; } = "";
    public string Message { get; set; } = "";
    public bool IsVisibleInPreview { get; set; }
}

public sealed class GCodePreviewLine : ViewModelBase
{
    private bool _hasWarning;
    private string _warningTooltip = "";

    public int LineNumber { get; set; }
    public string Text { get; set; } = "";
    public bool IsMetaLine { get; set; }

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
}

public sealed class ParseErrorDialogRequest : EventArgs
{
    public ParseErrorDialogRequest(string title, string summary, string details)
    {
        Title = title;
        Summary = summary;
        Details = details;
    }

    public string Title { get; }
    public string Summary { get; }
    public string Details { get; }
}
