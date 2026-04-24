using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia.Media;

namespace PortableCncApp.ViewModels;

public sealed class ManualControlViewModel : PageViewModelBase
{
    // ════════════════════════════════════════════════════════════════
    // JOG SETTINGS
    // ════════════════════════════════════════════════════════════════

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

    private double _zCycleDistance = 1.0;
    public double ZCycleDistance
    {
        get => _zCycleDistance;
        set => SetProperty(ref _zCycleDistance, Math.Clamp(value, 0.01, 50.0));
    }

    private int _zCycleIntervalMs = 1000;
    public int ZCycleIntervalMs
    {
        get => _zCycleIntervalMs;
        set => SetProperty(ref _zCycleIntervalMs, Math.Clamp(value, 100, 10000));
    }

    private bool _isZCycleRunning;
    public bool IsZCycleRunning
    {
        get => _isZCycleRunning;
        private set
        {
            if (SetProperty(ref _isZCycleRunning, value))
            {
                RaisePropertyChanged(nameof(CanStartZCycle));
                RaisePropertyChanged(nameof(CanStopZCycle));
                RaiseZCycleCanExecuteChanged();
            }
        }
    }

    public bool CanStartZCycle => MainVm?.IsConnected == true && CanJogControls && !IsZCycleRunning;
    public bool CanStopZCycle => MainVm?.IsConnected == true && IsZCycleRunning;

    private CancellationTokenSource? _zCycleCancellation;

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

    // ════════════════════════════════════════════════════════════════
    // SPINDLE & OVERRIDES
    // ════════════════════════════════════════════════════════════════

    private double _spindleTargetRpm = 12000;
    public double SpindleTargetRpm
    {
        get => _spindleTargetRpm;
        set => SetProperty(ref _spindleTargetRpm, ClampSpindleRpm(value));
    }

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

    // ════════════════════════════════════════════════════════════════
    // PROBE SETTINGS
    // ════════════════════════════════════════════════════════════════

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

    // ════════════════════════════════════════════════════════════════
    // CUSTOM COMMAND
    // ════════════════════════════════════════════════════════════════

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

    private string _commandPreview = string.Empty;
    public string CommandPreview
    {
        get => _commandPreview;
        private set => SetProperty(ref _commandPreview, value);
    }

    // ════════════════════════════════════════════════════════════════
    // STATE PASS-THROUGHS (delegated from MainVm)
    // ════════════════════════════════════════════════════════════════

    public string MotionStateLabel => MainVm?.MotionStateLabel ?? "OFFLINE";
    public IBrush MotionStateBrush => MainVm?.MotionStateBrush ?? ThemeResources.Brush("NeutralStateBrush", "#808080");
    public string SafetyStateLabel => MainVm?.SafetyStateLabel ?? "OFFLINE";
    public IBrush SafetyStateBrush => MainVm?.SafetyStateBrush ?? ThemeResources.Brush("NeutralStateBrush", "#808080");

    public bool IsMachineConnected => MainVm?.IsConnected == true;

    // Homed / limit display — driven by @EVENT LIMIT; individual homed bits not yet in protocol.
    public bool XHomed          => MainVm?.XHomed          == true;
    public bool YHomed          => MainVm?.YHomed          == true;
    public bool ZHomed          => MainVm?.ZHomed          == true;
    public bool AllAxesHomed    => MainVm?.AllAxesHomed    == true;
    public string HomingStatusText => MainVm?.HomingStatusText ?? "HOME X/Y/Z";

    public bool XLimitTriggered  => MainVm?.XLimitTriggered  == true;
    public bool YLimitTriggered  => MainVm?.YLimitTriggered  == true;
    public bool ZLimitTriggered  => MainVm?.ZLimitTriggered  == true;
    public bool LimitsTriggered  => MainVm?.LimitsTriggered  == true;
    public string LimitSummaryText => MainVm?.LimitSummaryText ?? "XYZ CLEAR";

    public bool SpindleOn          => MainVm?.SpindleOn      == true;
    public string SpindleStatusText => MainVm?.SpindleStatusText ?? "OFF";
    public double SpindleMinRpm    => MainVm?.SettingsVm.SpindleMinRpm ?? 1000;
    public double SpindleMaxRpm    => MainVm?.SettingsVm.SpindleMaxRpm ?? 24000;

    // ════════════════════════════════════════════════════════════════
    // CAPABILITY FLAGS  (bound directly to Caps — no local re-derivation)
    // ════════════════════════════════════════════════════════════════

    /// <summary>Jog and motion controls: Caps.Motion.</summary>
    public bool CanJogControls  => MainVm?.Caps.Motion  == true;
    public bool CanHomeControls => MainVm?.Caps.Motion  == true;

    /// <summary>Spindle controls: Caps.Spindle.</summary>
    public bool CanUseAuxControls => MainVm?.Caps.Spindle == true;

    /// <summary>Override sliders: Caps.Overrides.</summary>
    public bool CanUseOverrides => MainVm?.Caps.Overrides == true;

    /// <summary>Probe button: Caps.Probe.</summary>
    public bool CanProbe => MainVm?.Caps.Probe == true;

    /// <summary>Reset/alarm-unlock: Caps.Reset.</summary>
    public bool CanAlarmUnlock => MainVm?.Caps.Reset == true;
    public bool CanSoftReset   => MainVm?.Caps.Reset == true;

    /// <summary>Jog cancel is always available when connected (realtime command).</summary>
    public bool CanCancelJog => MainVm?.IsConnected == true;

    public bool CanSendCustomCommand => IsMachineConnected && CustomCommand.Length > 0;

    public bool ShowJogLockout  => !CanJogControls;
    public bool ShowHomeLockout => !CanHomeControls;
    public bool ShowAuxLockout  => !CanUseAuxControls;

    public string JogLockoutReason      => GetJogLockoutReason();
    public string HomeLockoutReason     => GetHomeLockoutReason();
    public string AuxiliaryLockoutReason => GetAuxiliaryLockoutReason();
    public string JogModeText           => ContinuousJog ? "CONTINUOUS" : "STEP";

    public string ReadinessSummary
    {
        get
        {
            if (MainVm == null || !MainVm.IsConnected) return "Connect to the controller to enable manual actions.";
            if (MainVm.MachineState == MachineOperationState.Estop)      return "E-stop is latched. Clear it before using manual controls.";
            if (MainVm.MachineState == MachineOperationState.Fault)      return "Controller alarm is active. Clear the fault before jogging.";
            if (MainVm.MachineState == MachineOperationState.CommsFault) return "Communications fault. Reconnect the controller.";
            if (!AllAxesHomed) return "Home the machine before relying on work zero or park moves.";
            if (LimitsTriggered) return "One or more limit inputs are active. Clear them before moving.";
            return "Manual motion and auxiliaries are ready.";
        }
    }

    // ════════════════════════════════════════════════════════════════
    // COMMANDS
    // ════════════════════════════════════════════════════════════════

    public ICommand JogXPlusCommand        { get; }
    public ICommand JogXMinusCommand       { get; }
    public ICommand JogYPlusCommand        { get; }
    public ICommand JogYMinusCommand       { get; }
    public ICommand JogZPlusCommand        { get; }
    public ICommand JogZMinusCommand       { get; }
    public ICommand JogXMinusYPlusCommand  { get; }
    public ICommand JogXPlusYPlusCommand   { get; }
    public ICommand JogXMinusYMinusCommand { get; }
    public ICommand JogXPlusYMinusCommand  { get; }
    public ICommand HomeAllCommand         { get; }
    public ICommand HomeXCommand           { get; }
    public ICommand HomeYCommand           { get; }
    public ICommand HomeZCommand           { get; }
    public ICommand ZeroAllCommand         { get; }
    public ICommand ZeroXCommand           { get; }
    public ICommand ZeroYCommand           { get; }
    public ICommand ZeroZCommand           { get; }
    public ICommand RaiseZSafeCommand      { get; }
    public ICommand GoToXYZeroCommand      { get; }
    public ICommand GoToWorkZeroCommand    { get; }
    public ICommand ParkMachineCommand     { get; }
    public ICommand SetStepCommand         { get; }
    public ICommand StartSpindleCommand    { get; }
    public ICommand StopSpindleCommand     { get; }
    public ICommand AlarmUnlockCommand     { get; }
    public ICommand SoftResetCommand       { get; }
    public ICommand ProbeZCommand          { get; }
    public ICommand JogCancelCommand       { get; }
    public ICommand StartZCycleCommand     { get; }
    public ICommand StopZCycleCommand      { get; }
    public ICommand SendCustomCommandCommand { get; }
    public ICommand FeedOverridePlusCommand   { get; }
    public ICommand FeedOverrideMinusCommand  { get; }
    public ICommand FeedOverrideResetCommand  { get; }
    public ICommand SpindleOverridePlusCommand  { get; }
    public ICommand SpindleOverrideMinusCommand { get; }
    public ICommand SpindleOverrideResetCommand { get; }
    public ICommand RapidOverride25Command  { get; }
    public ICommand RapidOverride50Command  { get; }
    public ICommand RapidOverride100Command { get; }

    public ManualControlViewModel()
    {
        ThemeResources.ThemeChanged += HandleThemeChanged;

        JogXPlusCommand        = new RelayCommand(() => Jog('X',  JogStep));
        JogXMinusCommand       = new RelayCommand(() => Jog('X', -JogStep));
        JogYPlusCommand        = new RelayCommand(() => Jog('Y',  JogStep));
        JogYMinusCommand       = new RelayCommand(() => Jog('Y', -JogStep));
        JogZPlusCommand        = new RelayCommand(() => Jog('Z',  JogStep));
        JogZMinusCommand       = new RelayCommand(() => Jog('Z', -JogStep));
        JogXMinusYPlusCommand  = new RelayCommand(() => JogXY(-JogStep,  JogStep));
        JogXPlusYPlusCommand   = new RelayCommand(() => JogXY( JogStep,  JogStep));
        JogXMinusYMinusCommand = new RelayCommand(() => JogXY(-JogStep, -JogStep));
        JogXPlusYMinusCommand  = new RelayCommand(() => JogXY( JogStep, -JogStep));

        HomeAllCommand = new RelayCommand(() => Home("ALL"));
        HomeXCommand   = new RelayCommand(() => Home("X"));
        HomeYCommand   = new RelayCommand(() => Home("Y"));
        HomeZCommand   = new RelayCommand(() => Home("Z"));

        ZeroAllCommand = new RelayCommand(ZeroAll);
        ZeroXCommand   = new RelayCommand(() => Zero("X"));
        ZeroYCommand   = new RelayCommand(() => Zero("Y"));
        ZeroZCommand   = new RelayCommand(() => Zero("Z"));

        RaiseZSafeCommand   = new RelayCommand(RaiseZSafe);
        GoToXYZeroCommand   = new RelayCommand(GoToXYZero);
        GoToWorkZeroCommand = new RelayCommand(GoToWorkZero);
        ParkMachineCommand  = new RelayCommand(ParkMachine);

        SetStepCommand = new RelayCommand<object>(param =>
        {
            if (param is double d)
                JogStep = d;
            else if (param is string s && double.TryParse(s, out var parsed))
                JogStep = parsed;
        });

        StartSpindleCommand = new RelayCommand(StartSpindle);
        StopSpindleCommand  = new RelayCommand(StopSpindle);
        AlarmUnlockCommand  = new RelayCommand(AlarmUnlockTracked);
        SoftResetCommand    = new RelayCommand(SoftResetTracked);
        ProbeZCommand       = new RelayCommand(ProbeZ);
        JogCancelCommand    = new RelayCommand(JogCancel);
        StartZCycleCommand  = new RelayCommand(StartZCycle, () => CanStartZCycle);
        StopZCycleCommand   = new RelayCommand(StopZCycle, () => CanStopZCycle);
        SendCustomCommandCommand = new RelayCommand(SendCustom);

        FeedOverridePlusCommand    = new RelayCommand(() => SendFeedOverride(FeedOverride + 10));
        FeedOverrideMinusCommand   = new RelayCommand(() => SendFeedOverride(FeedOverride - 10));
        FeedOverrideResetCommand   = new RelayCommand(() => SendFeedOverride(100));
        SpindleOverridePlusCommand  = new RelayCommand(() => SendSpindleOverride(SpindleOverride + 10));
        SpindleOverrideMinusCommand = new RelayCommand(() => SendSpindleOverride(SpindleOverride - 10));
        SpindleOverrideResetCommand = new RelayCommand(() => SendSpindleOverride(100));
        RapidOverride25Command  = new RelayCommand(() => SendRapidOverride(25));
        RapidOverride50Command  = new RelayCommand(() => SendRapidOverride(50));
        RapidOverride100Command = new RelayCommand(() => SendRapidOverride(100));

        UpdateSelectionPreview();
    }

    // ════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ════════════════════════════════════════════════════════════════

    protected override void OnMainViewModelSet()
    {
        if (MainVm == null) return;
        SpindleTargetRpm = MainVm.SpindleSpeed > 0
            ? MainVm.SpindleSpeed
            : Math.Max(12000, SpindleMinRpm);
        RaiseMainStateProperties();
        UpdateSelectionPreview();
    }

    protected override void OnMainViewModelPropertyChanged(string? propertyName)
    {
        switch (propertyName)
        {
            case nameof(MainWindowViewModel.IsConnected):
            case nameof(MainWindowViewModel.MachineState):
            case nameof(MainWindowViewModel.MotionStateLabel):
            case nameof(MainWindowViewModel.MotionStateBrush):
            case nameof(MainWindowViewModel.SafetyLevel):
            case nameof(MainWindowViewModel.SafetyStateLabel):
            case nameof(MainWindowViewModel.SafetyStateBrush):
            case nameof(MainWindowViewModel.Caps):
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
        RaisePropertyChanged(nameof(CanUseOverrides));
        RaisePropertyChanged(nameof(CanProbe));
        RaisePropertyChanged(nameof(CanAlarmUnlock));
        RaisePropertyChanged(nameof(CanSoftReset));
        RaisePropertyChanged(nameof(CanCancelJog));
        RaisePropertyChanged(nameof(CanStartZCycle));
        RaisePropertyChanged(nameof(CanStopZCycle));
        RaisePropertyChanged(nameof(CanSendCustomCommand));
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
    }

    // ════════════════════════════════════════════════════════════════
    // JOG COMMANDS
    // ════════════════════════════════════════════════════════════════

    private async void Jog(char axis, double distance)
    {
        if (MainVm == null || !CanJogControls) return;

        await ExecuteProtocolCommandAsync(
            "JOG",
            () => MainVm.Protocol.SendJog(axis, (float)distance, (float)JogFeedRate),
            $"@JOG AXIS={axis} DIST={distance:+0.000;-0.000} FEED={JogFeedRate:F0}",
            $"Jog {axis} {distance:+0.000;-0.000} mm",
            "Jog failed");
    }

    private async void JogXY(double xDistance, double yDistance)
    {
        if (MainVm == null || !CanJogControls) return;

        // Jog X then Y as two commands; diagonal jog requires firmware support.
        // Send the dominant axis if they differ, or X first.
        var xOk = await ExecuteProtocolCommandAsync(
            "JOG",
            () => MainVm.Protocol.SendJog('X', (float)xDistance, (float)JogFeedRate),
            $"@JOG AXIS=X DIST={xDistance:+0.000;-0.000} FEED={JogFeedRate:F0}",
            $"Jog X{xDistance:+0.000;-0.000} Y{yDistance:+0.000;-0.000} mm",
            "Jog failed");
        if (!xOk)
            return;

        await ExecuteProtocolCommandAsync(
            "JOG",
            () => MainVm.Protocol.SendJog('Y', (float)yDistance, (float)JogFeedRate),
            $"@JOG AXIS=Y DIST={yDistance:+0.000;-0.000} FEED={JogFeedRate:F0}",
            $"Jog X{xDistance:+0.000;-0.000} Y{yDistance:+0.000;-0.000} mm",
            "Jog failed");
    }

    private async void JogCancel()
    {
        if (MainVm == null || !CanCancelJog) return;
        await ExecuteProtocolCommandAsync("JOG_CANCEL", MainVm.Protocol.SendJogCancel, "@JOG_CANCEL", "Jog cancelled.", "Jog cancel failed");
    }

    private void StartZCycle()
    {
        if (MainVm == null || !CanStartZCycle)
            return;

        _zCycleCancellation?.Cancel();
        _zCycleCancellation?.Dispose();
        _zCycleCancellation = new CancellationTokenSource();
        IsZCycleRunning = true;
        _ = RunZCycleAsync(_zCycleCancellation.Token);
    }

    private void StopZCycle()
    {
        _zCycleCancellation?.Cancel();
        if (MainVm != null && MainVm.IsConnected)
            MainVm.Protocol.SendJogCancel();

        IsZCycleRunning = false;
        SetCommandPreview("Z cycle stopped");
        if (MainVm != null)
        {
            MainVm.StatusMessage = "Z cycle stopped.";
            MainVm.IsStatusError = false;
        }
    }

    private async Task RunZCycleAsync(CancellationToken cancellationToken)
    {
        if (MainVm == null)
            return;

        var physicalUp = true;
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var distance = Math.Abs(ZCycleDistance);
                var commandDistance = physicalUp ? -distance : distance;
                var directionText = physicalUp ? "up" : "down";
                var preview = $"@JOG AXIS=Z DIST={commandDistance:+0.000;-0.000} FEED={JogFeedRate:F0}";

                SetCommandPreview(preview);
                MainVm.StatusMessage = $"Z cycle {directionText} {distance:0.###} mm";
                MainVm.IsStatusError = false;

                var result = await MainVm.SendCommandAndWaitAsync(
                    "JOG",
                    () => MainVm.Protocol.SendJog('Z', (float)commandDistance, (float)JogFeedRate),
                    TimeSpan.FromSeconds(3),
                    cancellationToken,
                    disconnectOnTimeout: false);

                if (result.Kind == MainWindowViewModel.ControllerCommandResultKind.Busy)
                {
                    await Task.Delay(Math.Min(250, ZCycleIntervalMs), cancellationToken);
                    continue;
                }

                MainVm.ApplyCommandResult(result, "Z cycle jog failed");
                if (!result.Success)
                    break;

                physicalUp = !physicalUp;
                var idle = await WaitForMachineIdleAsync(TimeSpan.FromSeconds(10), cancellationToken);
                if (!idle)
                    break;

                await Task.Delay(ZCycleIntervalMs, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            if (_zCycleCancellation?.Token == cancellationToken)
            {
                _zCycleCancellation.Dispose();
                _zCycleCancellation = null;
                IsZCycleRunning = false;
            }
        }
    }

    private async Task<bool> WaitForMachineIdleAsync(TimeSpan timeout, CancellationToken cancellationToken)
    {
        if (MainVm == null)
            return false;

        var start = DateTime.UtcNow;
        while (!cancellationToken.IsCancellationRequested)
        {
            if (MainVm.MachineState == MachineOperationState.Idle)
                return true;

            if (DateTime.UtcNow - start >= timeout)
            {
                MainVm.StatusMessage = "Z cycle stopped: machine did not return to idle.";
                MainVm.IsStatusError = true;
                return false;
            }

            await Task.Delay(50, cancellationToken);
        }

        return false;
    }

    // ════════════════════════════════════════════════════════════════
    // HOME COMMANDS
    // ════════════════════════════════════════════════════════════════

    private async void Home(string axes)
    {
        if (MainVm == null || !CanHomeControls) return;

        await ExecuteProtocolCommandAsync(
            "HOME",
            MainVm.Protocol.SendHome,
            "@HOME",
            axes == "ALL" ? "Homing all axes..." : $"Homing {axes} axis...",
            "Home failed");
    }

    // ════════════════════════════════════════════════════════════════
    // ZERO COMMANDS
    // ════════════════════════════════════════════════════════════════

    private async void Zero(string axis)
    {
        if (MainVm == null || !CanJogControls) return;

        await ExecuteProtocolCommandAsync(
            "ZERO",
            () => MainVm.Protocol.SendZero(axis),
            $"@ZERO AXIS={axis}",
            $"Work offset {axis} = 0",
            "Zero failed");
    }

    private async void ZeroAll()
    {
        if (MainVm == null || !CanJogControls) return;

        await ExecuteProtocolCommandAsync(
            "ZERO",
            () => MainVm.Protocol.SendZero("ALL"),
            "@ZERO AXIS=ALL",
            "Work offset XYZ = 0",
            "Zero failed");
    }

    // ════════════════════════════════════════════════════════════════
    // MOVE-TO COMMANDS  (require @JOG with absolute target — not yet in protocol)
    // ════════════════════════════════════════════════════════════════

    private async void RaiseZSafe()
    {
        if (MainVm == null || !CanJogControls) return;
        await ExecuteProtocolCommandAsync(
            "JOG",
            () => MainVm.Protocol.SendJog('Z', (float)JogStep, (float)JogFeedRate),
            $"@JOG AXIS=Z DIST=+{JogStep:0.000} FEED={JogFeedRate:F0}",
            $"Raise Z by +{JogStep:0.000} mm",
            "Raise Z failed");
    }

    private void GoToXYZero()
    {
        if (MainVm == null || !CanJogControls) return;
        SetCommandPreview("Move to work XY zero (not yet implemented)");
        MainVm.StatusMessage = "Move to work XY zero";
    }

    private void GoToWorkZero()
    {
        if (MainVm == null || !CanJogControls) return;
        SetCommandPreview("Move to work XYZ zero (not yet implemented)");
        MainVm.StatusMessage = "Move to work XYZ zero";
    }

    private void ParkMachine()
    {
        if (MainVm == null || !CanJogControls) return;
        SetCommandPreview("Park (not yet implemented)");
        MainVm.StatusMessage = "Move to park position";
    }

    // ════════════════════════════════════════════════════════════════
    // SPINDLE COMMANDS
    // ════════════════════════════════════════════════════════════════

    private async void StartSpindle()
    {
        if (MainVm == null || !CanUseAuxControls) return;

        var rpm = (int)ClampSpindleRpm(SpindleTargetRpm);
        SpindleTargetRpm = rpm;
        await ExecuteProtocolCommandAsync(
            "SPINDLE_ON",
            () => MainVm.Protocol.SendSpindleOn(rpm),
            $"@SPINDLE_ON RPM={rpm}",
            $"Spindle start {rpm} RPM",
            "Spindle start failed");
    }

    private async void StopSpindle()
    {
        if (MainVm == null || !CanUseAuxControls) return;

        await ExecuteProtocolCommandAsync(
            "SPINDLE_OFF",
            MainVm.Protocol.SendSpindleOff,
            "@SPINDLE_OFF",
            "Spindle stopped",
            "Spindle stop failed");
    }

    // ════════════════════════════════════════════════════════════════
    // OVERRIDE COMMANDS
    // ════════════════════════════════════════════════════════════════

    private async void SendFeedOverride(int percent)
    {
        FeedOverride = percent;
        if (MainVm == null)
            return;

        await ExecuteProtocolCommandAsync(
            "OVERRIDE",
            () => MainVm.Protocol.SendOverrideFeed(FeedOverride),
            $"@OVERRIDE FEED={FeedOverride}",
            $"Feed override {FeedOverride}%",
            "Feed override failed");
    }

    private async void SendSpindleOverride(int percent)
    {
        SpindleOverride = percent;
        if (MainVm == null)
            return;

        await ExecuteProtocolCommandAsync(
            "OVERRIDE",
            () => MainVm.Protocol.SendOverrideSpindle(SpindleOverride),
            $"@OVERRIDE SPINDLE={SpindleOverride}",
            $"Spindle override {SpindleOverride}%",
            "Spindle override failed");
    }

    private async void SendRapidOverride(int percent)
    {
        RapidOverride = percent;
        if (MainVm == null)
            return;

        await ExecuteProtocolCommandAsync(
            "OVERRIDE",
            () => MainVm.Protocol.SendOverrideRapid(RapidOverride),
            $"@OVERRIDE RAPID={RapidOverride}",
            $"Rapid override {RapidOverride}%",
            "Rapid override failed");
    }

    // ════════════════════════════════════════════════════════════════
    // RESET / ALARM COMMANDS
    // ════════════════════════════════════════════════════════════════

    private void AlarmUnlock()
    {
        if (MainVm == null || !CanAlarmUnlock) return;

        MainVm.Protocol.SendReset();
        SetCommandPreview("@RESET");
        MainVm.StatusMessage = "Reset sent — check machine position before moving.";
    }

    private void SoftReset()
    {
        if (MainVm == null || !CanSoftReset) return;

        MainVm.Protocol.SendReset();
        SetCommandPreview("@RESET");
        MainVm.StatusMessage = "Soft reset sent.";
    }

    private async void AlarmUnlockTracked()
    {
        if (MainVm == null || !CanAlarmUnlock) return;

        await ExecuteProtocolCommandAsync(
            "RESET",
            MainVm.Protocol.SendReset,
            "@RESET",
            "Reset sent — check machine position before moving.",
            "Reset failed");
    }

    private async void SoftResetTracked()
    {
        if (MainVm == null || !CanSoftReset) return;

        await ExecuteProtocolCommandAsync(
            "RESET",
            MainVm.Protocol.SendReset,
            "@RESET",
            "Soft reset sent.",
            "Soft reset failed");
    }

    // ════════════════════════════════════════════════════════════════
    // PROBE COMMAND
    // ════════════════════════════════════════════════════════════════

    private void ProbeZ()
    {
        if (MainVm == null || !CanProbe) return;

        // Probe is not yet a first-class @-protocol command; preview only for now.
        SetCommandPreview($"Probe Z -{ProbeDepth:0.###} mm @ {ProbeFeed:F0} mm/min (not yet implemented)");
        MainVm.StatusMessage = $"Probing Z down {ProbeDepth:0.###} mm at {ProbeFeed:F0} mm/min";
    }

    // ════════════════════════════════════════════════════════════════
    // CUSTOM COMMAND
    // ════════════════════════════════════════════════════════════════

    private void SendCustom()
    {
        if (!CanSendCustomCommand || MainVm == null) return;

        SetCommandPreview(CustomCommand);
        MainVm.StatusMessage = "Raw text commands are no longer supported by the binary protocol.";
        MainVm.IsStatusError = true;
        CustomCommand = string.Empty;
    }

    // ════════════════════════════════════════════════════════════════
    // LOCKOUT REASONS
    // ════════════════════════════════════════════════════════════════

    private string GetJogLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MachineState == MachineOperationState.Estop)      return "E-stop is latched.";
        if (MainVm.MachineState == MachineOperationState.Fault)      return "Controller fault is active.";
        if (MainVm.MachineState == MachineOperationState.CommsFault) return "Communications fault.";
        if (!MainVm.Caps.Motion) return $"Motion not available: {MainVm.MotionStateLabel}.";
        return "Jogging is ready.";
    }

    private string GetHomeLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MachineState == MachineOperationState.Estop)      return "E-stop is latched.";
        if (MainVm.MachineState == MachineOperationState.Fault)      return "Controller fault is active.";
        if (MainVm.MachineState == MachineOperationState.CommsFault) return "Communications fault.";
        if (!MainVm.Caps.Motion) return $"Motion not available: {MainVm.MotionStateLabel}.";
        return AllAxesHomed ? "Axes are already homed." : "Homing is available.";
    }

    private string GetAuxiliaryLockoutReason()
    {
        if (MainVm == null || !MainVm.IsConnected) return "Machine is disconnected.";
        if (MainVm.MachineState == MachineOperationState.Estop)      return "E-stop is latched.";
        if (MainVm.MachineState == MachineOperationState.Fault)      return "Controller fault is active.";
        if (!MainVm.Caps.Spindle) return $"Spindle not available: {MainVm.MotionStateLabel}.";
        return "Spindle controls are available.";
    }

    // ════════════════════════════════════════════════════════════════
    // HELPERS
    // ════════════════════════════════════════════════════════════════

    private double ClampSpindleRpm(double value)
        => Math.Clamp(value, SpindleMinRpm, SpindleMaxRpm);

    private async Task<bool> ExecuteProtocolCommandAsync(
        string okToken,
        Action send,
        string preview,
        string statusText,
        string failurePrefix,
        TimeSpan? timeout = null)
    {
        if (MainVm == null)
            return false;

        SetCommandPreview(preview);
        MainVm.StatusMessage = statusText;
        MainVm.IsStatusError = false;

        var result = await MainVm.SendCommandAndWaitAsync(okToken, send, timeout ?? TimeSpan.FromSeconds(3));
        MainVm.ApplyCommandResult(result, failurePrefix);
        return result.Success;
    }

    private void SetCommandPreview(string preview) => CommandPreview = preview;

    private void UpdateSelectionPreview()
    {
        var mode = ContinuousJog ? "continuous" : "step";
        CommandPreview = $"Jog selection: {JogStep:0.###} mm at {JogFeedRate:F0} mm/min ({mode})";
    }

    private void RaiseZCycleCanExecuteChanged()
    {
        ((RelayCommand)StartZCycleCommand).RaiseCanExecuteChanged();
        ((RelayCommand)StopZCycleCommand).RaiseCanExecuteChanged();
    }

    private void HandleThemeChanged(object? sender, EventArgs e) => RaiseMainStateProperties();
}
