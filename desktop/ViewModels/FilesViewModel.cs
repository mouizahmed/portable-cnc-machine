using System.Collections.ObjectModel;
using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class FilesViewModel : PageViewModelBase
{
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

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand BrowseCommand { get; }
    public ICommand LoadSelectedCommand { get; }
    public ICommand RefreshCommand { get; }
    public ICommand DeleteCommand { get; }

    public FilesViewModel()
    {
        BrowseCommand = new RelayCommand(BrowseForFile);
        LoadSelectedCommand = new RelayCommand(LoadSelectedFile, () => HasSelectedFile);
        RefreshCommand = new RelayCommand(RefreshFileList);
        DeleteCommand = new RelayCommand(DeleteSelectedFile, () => HasSelectedFile);

        // Add some demo files
        LoadDemoFiles();
    }

    private void BrowseForFile()
    {
        // TODO: Open file dialog to select .gcode or .nc file
        MainVm?.StatusMessage.ToString();
    }

    private void LoadSelectedFile()
    {
        if (SelectedFile == null || MainVm == null) return;

        MainVm.CurrentFileName = SelectedFile.Name;
        MainVm.TotalLines = PreviewLineCount;
        MainVm.CurrentLine = 0;
        MainVm.Progress = 0;
        MainVm.StatusMessage = $"Loaded: {SelectedFile.Name}";
    }

    private void RefreshFileList()
    {
        // TODO: Scan directory for G-code files
        LoadDemoFiles();
    }

    private void DeleteSelectedFile()
    {
        if (SelectedFile == null) return;

        Files.Remove(SelectedFile);
        SelectedFile = null;
        FilePreview = "";
    }

    private void LoadFilePreview()
    {
        if (SelectedFile == null)
        {
            FilePreview = "";
            PreviewLineCount = 0;
            return;
        }

        // TODO: Actually load and preview the file
        FilePreview = $"; Demo G-code preview for {SelectedFile.Name}\n" +
                      "G21 ; Metric units\n" +
                      "G90 ; Absolute positioning\n" +
                      "G0 Z5.0\n" +
                      "G0 X0 Y0\n" +
                      "M3 S12000 ; Spindle on\n" +
                      "G1 Z-1.0 F100\n" +
                      "G1 X50 F500\n" +
                      "G1 Y50\n" +
                      "G1 X0\n" +
                      "G1 Y0\n" +
                      "G0 Z5.0\n" +
                      "M5 ; Spindle off\n" +
                      "M30 ; Program end";
        PreviewLineCount = 14;
    }

    private void LoadDemoFiles()
    {
        Files.Clear();
        Files.Add(new GCodeFileInfo { Name = "square_pocket.gcode", Size = "2.4 KB", Modified = "2026-01-28" });
        Files.Add(new GCodeFileInfo { Name = "circle_engrave.nc", Size = "1.1 KB", Modified = "2026-01-27" });
        Files.Add(new GCodeFileInfo { Name = "bracket_v2.gcode", Size = "15.6 KB", Modified = "2026-01-25" });
        Files.Add(new GCodeFileInfo { Name = "test_pattern.nc", Size = "0.8 KB", Modified = "2026-01-20" });
    }
}

public class GCodeFileInfo
{
    public string Name { get; set; } = "";
    public string Size { get; set; } = "";
    public string Modified { get; set; } = "";
}
