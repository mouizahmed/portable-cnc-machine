using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace PortableCncApp.ViewModels;

public abstract class ViewModelBase : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void RaisePropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        RaisePropertyChanged(name);
        return true;
    }
}

/// <summary>
/// Base class for page ViewModels that need access to shared machine state
/// </summary>
public abstract class PageViewModelBase : ViewModelBase
{
    public MainWindowViewModel? MainVm { get; private set; }

    public void SetMainViewModel(MainWindowViewModel mainVm)
    {
        MainVm = mainVm;
    }
}
