using System;
using System.Windows.Input;
using Avalonia.Media;

namespace PortableCncApp.ViewModels;

public sealed class ManualControlViewModel : PageViewModelBase
{
    private double _jogStep = 1.0;
    public double JogStep
    {
        get => _jogStep;
        set
        {
            if (SetProperty(ref _jogStep, value))
                UpdateSelectionPreview();
        }
    }

    private double _jogFeedRate = 500;
    public double JogFeedRate
    {
        get => _jogFeedRate;
        set
        {
            if (SetProperty(ref _jogFeedRate, value))
                UpdateSelectionPreview();
        }
    }

    private bool _continuousJog;
    public bool ContinuousJog
    {
        get => _continuousJog;
        set
        {
            if (SetProperty(ref _continuousJog, value))
            {
                RaisePropertyChanged(nameof(JogModeText));
                UpdateSelectionPreview();
            }
        }
    }

    private double _spindleTargetRpm = 12000;
    public double SpindleTargetRpm
    {
        get => _spindleTargetRpm;
        set => SetProperty(ref _spindleTargetRpm, ClampSpindleRpm(value));
    }

    private string _customCommand = string.Empty;
    public string CustomCommand
    {
        get => _customCommand;
        set
        {
            if (SetProperty(ref _customCommand, value))
                RaisePropertyChanged(nameof(CanSendCustomCommand));
        }
    }

    public bool CanSendCustomCommand => IsMachineConnected && CustomCommand.Length > 0;

    private int _feedOverride = 100;
    public int FeedOverride
    {
        get => _feedOverride;
        set => SetProperty(ref _feedOverride, Math.Clamp(value, 10, 200));
    }

    private int _spindleOverride = 100;
    public int SpindleOverride
    {
        get => _spindleOverride;
        set => SetProperty(ref _spindleOverride, Math.Clamp(value, 10, 200));
    }

    private int _rapidOverride = 100;
    public int RapidOverride
    {
        get => _rapidOverride;
        set => SetProperty(ref _rapidOverride, value);
    }

    public bool CanUseOverrides => IsMachineConnected;

    private string _commandPreview = string.Empty;
    public string CommandPreview
    {
        get => _commandPreview;
        private set => SetProperty(ref _commandPreview, value);
    }

    public ICommand JogXPlusCommand { get; }
    public ICommand JogXMinusCommand { get; }
    public ICommand JogYPlusCommand { get; }
    public ICommand JogYMinusCommand { get; }
    public ICommand JogZPlusCommand { get; }
    public ICommand JogZMinusCommand { get; }
    public ICommand JogXMinusYPlusCommand { get; }
    public ICommand JogXPlusYPlusCommand { get; }
    public ICommand JogXMinusYMinusCommand { get; }
    public ICommand JogXPlusYMinusCommand { get; }
    public ICommand HomeAllCommand { get; }
    public ICommand HomeXCommand { get; }
    public ICommand HomeYCommand { get; }
    public ICommand HomeZCommand { get; }
    public ICommand ZeroAllCommand { get; }
    public ICommand ZeroXCommand { get; }
    public ICommand ZeroYCommand { get; }
    public ICommand ZeroZCommand { get; }
    public ICommand RaiseZSafeCommand { get; }
    public ICommand GoToXYZeroCommand { get; }
    public ICommand GoToWorkZeroCommand { get; }
    public ICommand ParkMachineCommand { get; }
    public ICommand SetStepCommand { get; }
    public ICommand StartSpindleCommand { get; }
    public ICommand StopSpindleCommand { get; }
    public ICommand AlarmUnlockCommand { get; }
    public ICommand SoftResetCommand { get; }
    public ICommand ProbeZCommand { get; }
    public ICommand JogCancelCommand { get; }
    public ICommand SendCustomCommandCommand { get; }
    public ICommand FeedOverridePlusCommand { get; }
    public ICommand FeedOverrideMinusCommand { get; }
    public ICommand FeedOverrideResetCommand { get; }
    public ICommand SpindleOverridePlusCommand { get; }
    public ICommand SpindleOverrideMinusCommand { get; }
    public ICommand SpindleOverrideResetCommand { get; }
    public ICommand RapidOverride25Command { get; }
    public ICommand RapidOverride50Command { get; }
    public ICommand RapidOverride100Command { get; }

    public string MotionStateLabel => MainVm?.MotionStateLabel ?? "OFFLINE";
    public IBrush MotionStateBrush => MainVm?.MotionStateBrush ?? ThemeResources.Brush("NeutralStateBrush", "#808080");
    public string SafetyStateLabel => MainVm?.SafetyStateLabel ?? "OFFLINE";
    public IBrush SafetyStateBrush => MainVm?.SafetyStateBrush ?? ThemeResources.Brush("NeutralStateBrush", "#808080");

    public bool IsMachineConnected => MainVm?.IsConnected == true;
    public bool CanJogControls => MainVm?.CanJog == true;
    public bool CanHomeControls => MainVm?.CanHome == true;
    public bool CanUseAuxControls => MainVm is { IsConnected: true } &&
                                     MainVm.MotionState != MotionState.EStopLatched &&
                                     MainVm.MotionState != MotionState.Fault;

    public bool ShowJogLockout => !CanJogControls;
    public bool ShowHomeLockout => !CanHomeControls;
    public bool ShowAuxLockout => !CanUseAuxControls;

    public string JogLockoutReason => GetJogLockoutReason();
    public string HomeLockoutReason => GetHomeLockoutReason();
    public string AuxiliaryLockoutReason => GetAuxiliaryLockoutReason();
    public string JogModeText => ContinuousJog ? "CONTINUOUS" : "STEP";

    public bool XHomed => MainVm?.XHomed == true;
    public bool YHomed => MainVm?.YHomed == true;
    public bool ZHomed => MainVm?.ZHomed == true;
    public bool AllAxesHomed => MainVm?.AllAxesHomed == true;
    public string HomingStatusText => MainVm?.HomingStatusText ?? "HOME X/Y/Z";

    public bool XLimitTriggered => MainVm?.XLimitTriggered == true;
    public bool YLimitTriggered => MainVm?.YLimitTriggered == true;
    public bool ZLimitTriggered => MainVm?.ZLimitTriggered == true;
    public bool LimitsTriggered => MainVm?.LimitsTriggered == true;
    public string LimitSummaryText => MainVm?.LimitSummaryText ?? "XYZ CLEAR";

    private double _probeDepth = 10.0;
    public double ProbeDepth
    {
        get => _probeDepth;
        set => SetProperty(ref _probeDepth, Math.Max(0.1, value));
    }

    private double _probeFeed = 50.0;
    public double ProbeFeed
    {
        get => _probeFeed;
        set => SetProperty(ref _probeFeed, Math.Clamp(value, 1, 500));
    }

    public bool CanCancelJog => MainVm?.IsConnected == true;

    public bool CanAlarmUnlock => MainVm?.IsConnected == true &&
                                  (MainVm.MotionState == MotionState.Fault ||
                                   MainVm.MotionState == MotionState.EStopLatched);
    public bool CanSoftReset => MainVm?.IsConnected == true;
    public bool CanProbe => MainVm?.IsConnected == true && MainVm.MotionState == MotionState.Idle;

    public bool SpindleOn => MainVm?.SpindleOn == true;
    public string SpindleStatusText => MainVm?.SpindleStatusText ?? "OFF";
    public double SpindleMinRpm => MainVm?.Settings.Current.SpindleMinRpm ?? 1000;
    public double SpindleMaxRpm => MainVm?.Settings.Current.SpindleMaxRpm ?? 24000;

    public string ReadinessSummary
    {
        get
        {
            if (MainVm == null || !MainVm.IsConnected) return "Connect to the controller to enable manual actions.";
            if (MainVm.MotionState == MotionState.EStopLatched) return "E-stop is latched. Clear it before using manual controls.";
            if (MainVm.MotionState == MotionState.Fault) return "Controller alarm is active. Clear the fault before jogging.";
            if (!AllAxesHomed) return "Home the machine before relying on work zero or park moves.";
            if (LimitsTriggered) return "One or more limit inputs are active. Clear them before moving.";
            return "Manual motion and auxiliaries are ready.";
        }
    }

    public ManualControlViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;

        JogXPlusCommand = new RelayCommand(() => Jog("X", JogStep));
        JogXMinusCommand = new RelayCommand(() => Jog("X", -JogStep));
        JogYPlusCommand = new RelayCommand(() => Jog("Y", JogStep));
        JogYMinusCommand = new RelayCommand(() => Jog("Y", -JogStep));
        JogZPlusCommand = new RelayCommand(() => Jog("Z", JogStep));
        JogZMinusCommand = new RelayCommand(() => Jog("Z", -JogStep));
        JogXMinusYPlusCommand = new RelayCommand(() => JogXY(-JogStep, JogStep));
        JogXPlusYPlusCommand = new RelayCommand(() => JogXY(JogStep, JogStep));
        JogXMinusYMinusCommand = new RelayCommand(() => JogXY(-JogStep, -JogStep));
        JogXPlusYMinusCommand = new RelayCommand(() => JogXY(JogStep, -JogStep));

        HomeAllCommand = new RelayCommand(() => Home("ALL"));
        HomeXCommand = new RelayCommand(() => Home("X"));
        HomeYCommand = new RelayCommand(() => Home("Y"));
        HomeZCommand = new RelayCommand(() => Home("Z"));

        ZeroAllCommand = new RelayCommand(ZeroAll);
        ZeroXCommand = new RelayCommand(() => Zero("X"));
        ZeroYCommand = new RelayCommand(() => Zero("Y"));
        ZeroZCommand = new RelayCommand(() => Zero("Z"));

        RaiseZSafeCommand = new RelayCommand(RaiseZSafe);
        GoToXYZeroCommand = new RelayCommand(GoToXYZero);
        GoToWorkZeroCommand = new RelayCommand(GoToWorkZero);
        ParkMachineCommand = new RelayCommand(ParkMachine);

        SetStepCommand = new RelayCommand<object>(param =>
        {
            if (param is double d)
                JogStep = d;
            else if (param is string s && double.TryParse(s, out var parsed))
                JogStep = parsed;
        });

        StartSpindleCommand = new RelayCommand(StartSpindle);
        StopSpindleCommand = new RelayCommand(StopSpindle);
        AlarmUnlockCommand = new RelayCommand(AlarmUnlock);
        SoftResetCommand = new RelayCommand(SoftReset);
        ProbeZCommand = new RelayCommand(ProbeZ);
        JogCancelCommand = new RelayCommand(JogCancel);
        SendCustomCommandCommand = new RelayCommand(SendCustom);

        FeedOverridePlusCommand  = new RelayCommand(() => { FeedOverride += 10; SetCommandPreview($"Feed override: {FeedOverride}%"); });
        FeedOverrideMinusCommand = new RelayCommand(() => { FeedOverride -= 10; SetCommandPreview($"Feed override: {FeedOverride}%"); });
        FeedOverrideResetCommand = new RelayCommand(() => { FeedOverride  = 100; SetCommandPreview("Feed override reset: 100%"); });
        SpindleOverridePlusCommand  = new RelayCommand(() => { SpindleOverride += 10; SetCommandPreview($"Spindle override: {SpindleOverride}%"); });
        SpindleOverrideMinusCommand = new RelayCommand(() => { SpindleOverride -= 10; SetCommandPreview($"Spindle override: {SpindleOverride}%"); });
        SpindleOverrideResetCommand = new RelayCommand(() => { SpindleOverride  = 100; SetCommandPreview("Spindle override reset: 100%"); });
        RapidOverride25Command  = new RelayCommand(() => { RapidOverride = 25;  SetCommandPreview("Rapid override: 25%"); });
        RapidOverride50Command  = new RelayCommand(() => { RapidOverride = 50;  SetCommandPreview("Rapid override: 50%"); });
        RapidOverride100Command = new RelayCommand(() => { RapidOverride = 100; SetCommandPreview("Rapid override: 100%"); });

        UpdateSelectionPreview();
    }

    protected override void OnMainViewModelSet()
    {
        if (MainVm == null) return;

        SpindleTargetRpm = MainVm.SpindleSpeed > 0 ? MainVm.SpindleSpeed : Math.Max(12000, SpindleMinRpm);
        RaiseMainStateProperties();
        UpdateSelectionPreview();
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        switch (propertyName)
        {
            case nameof(MainWindowViewModel.IsConnected):
            case nameof(MainWindowViewModel.MotionState):
            case nameof(MainWindowViewModel.MotionStateLabel):
            case nameof(MainWindowViewModel.MotionStateBrush):
            case nameof(MainWindowViewModel.SafetyState):
            case nameof(MainWindowViewModel.SafetyStateLabel):
            case nameof(MainWindowViewModel.SafetyStateBrush):
            case nameof(MainWindowViewModel.CanJog):
            case nameof(MainWindowViewModel.CanHome):
            case nameof(MainWindowViewModel.XHomed):
            case nameof(MainWindowViewModel.YHomed):
            case nameof(MainWindowViewModel.ZHomed):
            case nameof(MainWindowViewModel.AllAxesHomed):
            case nameof(MainWindowViewModel.HomingStatusText):
            case nameof(MainWindowViewModel.XLimitTriggered):
            case nameof(MainWindowViewModel.YLimitTriggered):
            case nameof(MainWindowViewModel.ZLimitTriggered):
            case nameof(MainWindowViewModel.LimitsTriggered):
            case nameof(MainWindowViewModel.LimitSummaryText):
            case nameof(MainWindowViewModel.SpindleOn):
            case nameof(MainWindowViewModel.SpindleSpeed):
            case nameof(MainWindowViewModel.Settings):
                SpindleTargetRpm = ClampSpindleRpm(SpindleTargetRpm);
                RaiseMainStateProperties();
                break;
        }
    }

    private void RaiseMainStateProperties()
    {
        RaisePropertyChanged(nameof(MotionStateLabel));
        RaisePropertyChanged(nameof(MotionStateBrush));
        RaisePropertyChanged(nameof(SafetyStateLabel));
        RaisePropertyChanged(nameof(SafetyStateBrush));
        RaisePropertyChanged(nameof(IsMachineConnected));
        RaisePropertyChanged(nameof(CanJogControls));
        RaisePropertyChanged(nameof(CanHomeControls));
        RaisePropertyChanged(nameof(CanUseAuxControls));
        RaisePropertyChanged(nameof(ShowJogLockout));
        RaisePropertyChanged(nameof(ShowHomeLockout));
        RaisePropertyChanged(nameof(ShowAuxLockout));
        RaisePropertyChanged(nameof(JogLockoutReason));
        RaisePropertyChanged(nameof(HomeLockoutReason));
        RaisePropertyChanged(nameof(AuxiliaryLockoutReason));
        RaisePropertyChanged(nameof(XHomed));
        RaisePropertyChanged(nameof(YHomed));
        RaisePropertyChanged(nameof(ZHomed));
        RaisePropertyChanged(nameof(AllAxesHomed));
        RaisePropertyChanged(nameof(HomingStatusText));
        RaisePropertyChanged(nameof(XLimitTriggered));
        RaisePropertyChanged(nameof(YLimitTriggered));
        RaisePropertyChanged(nameof(ZLimitTriggered));
        RaisePropertyChanged(nameof(LimitsTriggered));
        RaisePropertyChanged(nameof(LimitSummaryText));
        RaisePropertyChanged(nameof(SpindleOn));
        RaisePropertyChanged(nameof(SpindleStatusText));
        RaisePropertyChanged(nameof(ReadinessSummary));
        RaisePropertyChanged(nameof(SpindleMinRpm));
        RaisePropertyChanged(nameof(SpindleMaxRpm));
        RaisePropertyChanged(nameof(CanCancelJog));
        RaisePropertyChanged(nameof(CanAlarmUnlock));
        RaisePropertyChanged(nameof(CanSoftReset));
        RaisePropertyChanged(nameof(CanProbe));
        RaisePropertyChanged(nameof(CanSendCustomCommand));
        RaisePropertyChanged(nameof(CanUseOverrides));
    }

    private void Jog(string axis, double distance)
    {
        if (MainVm == null || !MainVm.CanJog) return;

        var command = $"$J=G91 G21 {axis}{distance:+0.000;-0.000} F{JogFeedRate:F0}";
        SetCommandPreview(command);
        MainVm.StatusMessage = $"Jog {axis} {distance:+0.000;-0.000} mm";
    }

    private void JogXY(double xDistance, double yDistance)
    {
        if (MainVm == null || !MainVm.CanJog) return;

        var command = $"$J=G91 G21 X{xDistance:+0.000;-0.000} Y{yDistance:+0.000;-0.000} F{JogFeedRate:F0}";
        SetCommandPreview(command);
        MainVm.StatusMessage = $"Jog X{xDistance:+0.000;-0.000} Y{yDistance:+0.000;-0.000} mm";
    }

    private void Home(string axis)
    {
        if (MainVm == null || !MainVm.CanHome) return;

        if (axis == "ALL")
        {
            MainVm.SetAllAxesHomed(true);
            SetCommandPreview("$H");
            MainVm.StatusMessage = "Homing all axes...";
            return;
        }

        MainVm.SetAxisHomed(axis, true);
        SetCommandPreview($"Home {axis} axis");
        MainVm.StatusMessage = $"Homing {axis} axis...";
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

        SetCommandPreview($"G10 L20 P1 {axis}0");
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
        SetCommandPreview("G10 L20 P1 X0 Y0 Z0");
        MainVm.StatusMessage = "Work offset XYZ = 0";
    }

    private void RaiseZSafe()
    {
        if (MainVm == null || !MainVm.CanJog) return;

        SetCommandPreview($"Raise Z +{JogStep:0.000} mm at {JogFeedRate:F0} mm/min");
        MainVm.StatusMessage = $"Raise Z by {JogStep:+0.000;-0.000} mm";
    }

    private void GoToXYZero()
    {
        if (MainVm == null || !MainVm.CanJog) return;

        SetCommandPreview("Move to work XY zero, keep current Z");
        MainVm.StatusMessage = "Move to work XY zero";
    }

    private void GoToWorkZero()
    {
        if (MainVm == null || !MainVm.CanJog) return;

        SetCommandPreview("Move to work XYZ zero");
        MainVm.StatusMessage = "Move to work XYZ zero";
    }

    private void ParkMachine()
    {
        if (MainVm == null || !MainVm.CanJog) return;

        SetCommandPreview("Retract to safe Z, then move to park position");
        MainVm.StatusMessage = "Move to park position";
    }

    private void StartSpindle()
    {
        if (MainVm == null || !CanUseAuxControls) return;

        var rpm = ClampSpindleRpm(SpindleTargetRpm);
        SpindleTargetRpm = rpm;
        MainVm.SpindleSpeed = rpm;
        MainVm.SpindleOn = true;
        SetCommandPreview($"M3 S{rpm:F0}");
        MainVm.StatusMessage = $"Spindle start {rpm:F0} RPM";
    }

    private void StopSpindle()
    {
        if (MainVm == null || !CanUseAuxControls) return;

        MainVm.SpindleOn = false;
        MainVm.SpindleSpeed = 0;
        SetCommandPreview("M5");
        MainVm.StatusMessage = "Spindle stopped";
    }

    private void JogCancel()
    {
        if (MainVm == null || !CanCancelJog) return;

        SetCommandPreview("\\x85 (jog cancel)");
        MainVm.StatusMessage = "Jog cancelled.";
    }

    private void AlarmUnlock()
    {
        if (MainVm == null || !CanAlarmUnlock) return;

        SetCommandPreview("$X");
        MainVm.StatusMessage = "Alarm cleared — check machine position before moving.";
    }

    private void SoftReset()
    {
        if (MainVm == null || !CanSoftReset) return;

        SetCommandPreview("\\x18 (Ctrl+X soft reset)");
        MainVm.StatusMessage = "Soft reset sent.";
    }

    private void ProbeZ()
    {
        if (MainVm == null || !CanProbe) return;

        var command = $"G38.2 Z-{ProbeDepth:0.###} F{ProbeFeed:F0}";
        SetCommandPreview(command);
        MainVm.StatusMessage = $"Probing Z down {ProbeDepth:0.###} mm at {ProbeFeed:F0} mm/min";
    }

    private void SendCustom()
    {
        if (!CanSendCustomCommand) return;

        SetCommandPreview(CustomCommand);
        if (MainVm != null)
            MainVm.StatusMessage = $"Sent: {CustomCommand}";
        CustomCommand = string.Empty;
    }

    private double ClampSpindleRpm(double value)
        => Math.Clamp(value, SpindleMinRpm, SpindleMaxRpm);

    private void SetCommandPreview(string preview)
        => CommandPreview = preview;

    private void UpdateSelectionPreview()
    {
        var mode = ContinuousJog ? "continuous" : "step";
        CommandPreview = $"Jog selection: {JogStep:0.###} mm at {JogFeedRate:F0} mm/min ({mode})";
    }

    private string GetJogLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MotionState == MotionState.EStopLatched) return "E-stop is latched.";
        if (MainVm.MotionState == MotionState.Fault) return "Controller fault is active.";
        if (MainVm.MotionState != MotionState.Idle) return $"Controller is busy: {MainVm.MotionStateLabel}.";
        return "Jogging is ready.";
    }

    private string GetHomeLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MotionState == MotionState.EStopLatched) return "E-stop is latched.";
        if (MainVm.MotionState == MotionState.Fault) return "Controller fault is active.";
        if (MainVm.MotionState != MotionState.Idle) return $"Controller is busy: {MainVm.MotionStateLabel}.";
        return AllAxesHomed ? "Axes are already homed." : "Homing is available.";
    }

    private string GetAuxiliaryLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MotionState == MotionState.EStopLatched) return "E-stop is latched.";
        if (MainVm.MotionState == MotionState.Fault) return "Controller fault is active.";
        return "Spindle controls are available.";
    }

    private void HandleThemeChanged(object? sender, EventArgs e) => RaiseMainStateProperties();
}
