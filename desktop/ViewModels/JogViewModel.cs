using System.Windows.Input;

namespace PortableCncApp.ViewModels;

public sealed class JogViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // JOG SETTINGS
    // ════════════════════════════════════════════════════════════════

    private double _jogStep = 1.0; // mm
    public double JogStep
    {
        get => _jogStep;
        set => SetProperty(ref _jogStep, value);
    }

    private double _jogFeedRate = 500; // mm/min
    public double JogFeedRate
    {
        get => _jogFeedRate;
        set => SetProperty(ref _jogFeedRate, value);
    }

    private bool _continuousJog;
    public bool ContinuousJog
    {
        get => _continuousJog;
        set => SetProperty(ref _continuousJog, value);
    }

    // Preset step sizes
    public double[] StepPresets { get; } = { 0.01, 0.1, 1.0, 10.0, 50.0 };

    // ════════════════════════════════════════════════════════════════
    // JOG COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand JogXPlusCommand { get; }
    public ICommand JogXMinusCommand { get; }
    public ICommand JogYPlusCommand { get; }
    public ICommand JogYMinusCommand { get; }
    public ICommand JogZPlusCommand { get; }
    public ICommand JogZMinusCommand { get; }

    // ════════════════════════════════════════════════════════════════
    // HOME COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand HomeAllCommand { get; }
    public ICommand HomeXCommand { get; }
    public ICommand HomeYCommand { get; }
    public ICommand HomeZCommand { get; }

    // ════════════════════════════════════════════════════════════════
    // ZERO COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand ZeroAllCommand { get; }
    public ICommand ZeroXCommand { get; }
    public ICommand ZeroYCommand { get; }
    public ICommand ZeroZCommand { get; }

    // ════════════════════════════════════════════════════════════════
    // STEP PRESET COMMAND
    // ════════════════════════════════════════════════════════════════

    public ICommand SetStepCommand { get; }

    public void SetStep(double step) => JogStep = step;

    public JogViewModel()
    {
        // Jog axis commands
        JogXPlusCommand = new RelayCommand(() => Jog("X", JogStep));
        JogXMinusCommand = new RelayCommand(() => Jog("X", -JogStep));
        JogYPlusCommand = new RelayCommand(() => Jog("Y", JogStep));
        JogYMinusCommand = new RelayCommand(() => Jog("Y", -JogStep));
        JogZPlusCommand = new RelayCommand(() => Jog("Z", JogStep));
        JogZMinusCommand = new RelayCommand(() => Jog("Z", -JogStep));

        // Home commands
        HomeAllCommand = new RelayCommand(() => Home("ALL"));
        HomeXCommand = new RelayCommand(() => Home("X"));
        HomeYCommand = new RelayCommand(() => Home("Y"));
        HomeZCommand = new RelayCommand(() => Home("Z"));

        // Zero commands (set work coordinate origin)
        ZeroAllCommand = new RelayCommand(ZeroAll);
        ZeroXCommand = new RelayCommand(() => Zero("X"));
        ZeroYCommand = new RelayCommand(() => Zero("Y"));
        ZeroZCommand = new RelayCommand(() => Zero("Z"));

        // Step preset - uses object parameter from CommandParameter
        SetStepCommand = new RelayCommand<object>(param => 
        {
            if (param is double d)
                JogStep = d;
            else if (param is string s && double.TryParse(s, out var parsed))
                JogStep = parsed;
        });
    }

    private void Jog(string axis, double distance)
    {
        if (MainVm == null || !MainVm.CanJog) return;

        // TODO: Send jog command via PiApiClient
        // $J=G91 G21 X{distance} F{JogFeedRate}
        MainVm.StatusMessage = $"Jog {axis} {distance:+0.000;-0.000} mm";
    }

    private void Home(string axis)
    {
        if (MainVm == null || !MainVm.CanHome) return;

        // TODO: Send home command via PiApiClient
        MainVm.StatusMessage = axis == "ALL" ? "Homing all axes..." : $"Homing {axis} axis...";
    }

    private void Zero(string axis)
    {
        if (MainVm == null) return;

        switch (axis)
        {
            case "X":
                MainVm.WorkX = 0;
                MainVm.WorkOffsetX = MainVm.MachineX;
                break;
            case "Y":
                MainVm.WorkY = 0;
                MainVm.WorkOffsetY = MainVm.MachineY;
                break;
            case "Z":
                MainVm.WorkZ = 0;
                MainVm.WorkOffsetZ = MainVm.MachineZ;
                break;
        }

        MainVm.StatusMessage = $"Work offset {axis} = 0";
    }

    private void ZeroAll()
    {
        if (MainVm == null) return;

        MainVm.WorkX = 0;
        MainVm.WorkY = 0;
        MainVm.WorkZ = 0;
        MainVm.WorkOffsetX = MainVm.MachineX;
        MainVm.WorkOffsetY = MainVm.MachineY;
        MainVm.WorkOffsetZ = MainVm.MachineZ;
        MainVm.StatusMessage = "Work offset XYZ = 0";
    }
}
