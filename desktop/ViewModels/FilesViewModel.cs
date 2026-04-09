using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform.Storage;
using PortableCncApp.Services.GCode;

namespace PortableCncApp.ViewModels;

public sealed class FilesViewModel : PageViewModelBase
{
    private const int PreviewMaxLines = 200;
    private CancellationTokenSource? _parseCancellation;

    // ════════════════════════════════════════════════════════════════
    // FILE LIST
    // ════════════════════════════════════════════════════════════════

    public ObservableCollection<GCodeFileInfo> Files { get; } = new();

    private GCodeFileInfo? _selectedFile;
    public GCodeFileInfo? SelectedFile
    {
        get => _selectedFile;
        set
        {
            if (SetProperty(ref _selectedFile, value))
            {
                RaisePropertyChanged(nameof(HasSelectedFile));
                ((RelayCommand)LoadSelectedCommand).RaiseCanExecuteChanged();
                ((RelayCommand)RemoveCommand).RaiseCanExecuteChanged();
                LoadFilePreview();
            }
        }
    }

    public bool HasSelectedFile => SelectedFile != null;

    // ════════════════════════════════════════════════════════════════
    // FILE PREVIEW
    // ════════════════════════════════════════════════════════════════

    private string _filePreview = "";
    public string FilePreview
    {
        get => _filePreview;
        set => SetProperty(ref _filePreview, value);
    }

    private int _previewLineCount;
    public int PreviewLineCount
    {
        get => _previewLineCount;
        set => SetProperty(ref _previewLineCount, value);
    }

    private bool _isParsingToolpath;
    public bool IsParsingToolpath
    {
        get => _isParsingToolpath;
        set => SetProperty(ref _isParsingToolpath, value);
    }

    private string _toolpathStatusMessage = "Select a file to build the toolpath preview.";
    public string ToolpathStatusMessage
    {
        get => _toolpathStatusMessage;
        set => SetProperty(ref _toolpathStatusMessage, value);
    }

    private string _toolpathWarningSummary = "";
    public string ToolpathWarningSummary
    {
        get => _toolpathWarningSummary;
        set => SetProperty(ref _toolpathWarningSummary, value);
    }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand BrowseCommand { get; }
    public ICommand LoadSelectedCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand RemoveCommand { get; }

    public FilesViewModel()
    {
        BrowseCommand = new RelayCommand(BrowseForFile);
        LoadSelectedCommand = new RelayCommand(LoadSelectedFile, () => HasSelectedFile);
        RefreshCommand = new RelayCommand(RefreshFileList);
        RemoveCommand = new RelayCommand(RemoveSelectedFile, () => HasSelectedFile);
    }

    private async void BrowseForFile()
    {
        var lifetime = Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime;
        var window = lifetime?.MainWindow;
        if (window == null) return;

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
            if (Files.Any(f => f.FullPath == path)) continue;

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
            SelectedFile = lastAdded;
    }

    private void LoadSelectedFile()
    {
        if (SelectedFile == null || MainVm == null) return;

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

        foreach (var f in existing)
        {
            if (!File.Exists(f.FullPath)) continue;

            var info = new FileInfo(f.FullPath);
            Files.Add(new GCodeFileInfo
            {
                Name = info.Name,
                FullPath = f.FullPath,
                Size = FormatSize(info.Length),
                Modified = info.LastWriteTime.ToString("yyyy-MM-dd")
            });
        }
    }

    private void RemoveSelectedFile()
    {
        if (SelectedFile == null) return;

        Files.Remove(SelectedFile);
        SelectedFile = null;
        FilePreview = "";
        PreviewLineCount = 0;
        ToolpathStatusMessage = "Select a file to build the toolpath preview.";
        ToolpathWarningSummary = "";
        MainVm?.ActiveGCodeDocument = null;
        if (MainVm != null)
        {
            MainVm.DashboardVm.PreviewLine = 0;
            MainVm.DashboardVm.ScrubberValue = 0;
        }
    }

    private void LoadFilePreview()
    {
        if (SelectedFile == null || !File.Exists(SelectedFile.FullPath))
        {
            FilePreview = "";
            PreviewLineCount = 0;
            CancelPendingParse();
            ToolpathStatusMessage = "Select a file to build the toolpath preview.";
            ToolpathWarningSummary = "";
            MainVm?.ActiveGCodeDocument = null;
            return;
        }

        try
        {
            var previewLines = new List<string>(PreviewMaxLines);
            int total = 0;

            foreach (var line in File.ReadLines(SelectedFile.FullPath))
            {
                total++;
                if (total <= PreviewMaxLines)
                    previewLines.Add(line);
            }

            PreviewLineCount = total;
            FilePreview = string.Join("\n", previewLines);

            if (total > PreviewMaxLines)
                FilePreview += $"\n\n; ... ({total - PreviewMaxLines} more lines not shown)";
        }
        catch (Exception ex)
        {
            FilePreview = $"; Error reading file: {ex.Message}";
            PreviewLineCount = 0;
        }

        BeginToolpathParse();
    }

    private static string FormatSize(long bytes)
    {
        if (bytes < 1024) return $"{bytes} B";
        if (bytes < 1024 * 1024) return $"{bytes / 1024.0:F1} KB";
        return $"{bytes / (1024.0 * 1024):F1} MB";
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
            ToolpathStatusMessage = document.HasGeometry
                ? $"Toolpath ready: {document.Segments.Length} segments"
                : "No motion geometry was found in the selected file.";
            ToolpathWarningSummary = document.WarningCount == 0
                ? "No parse warnings."
                : $"{document.WarningCount} parse warnings.";
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            if (SelectedFile?.FullPath == selectedPath)
            {
                ToolpathStatusMessage = $"Toolpath parse failed: {ex.Message}";
                ToolpathWarningSummary = "";
                if (MainVm != null)
                {
                    MainVm.ActiveGCodeDocument = null;
                }
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
}

public class GCodeFileInfo
{
    public string Name { get; set; } = "";
    public string FullPath { get; set; } = "";
    public string Size { get; set; } = "";
    public string Modified { get; set; } = "";
}
