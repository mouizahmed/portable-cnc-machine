using System.Linq;
using Avalonia.Controls;

namespace PortableCncApp.Controls;

public class TwoColumnInfoGrid : Grid
{
    public TwoColumnInfoGrid()
    {
        ColumnDefinitions = new ColumnDefinitions("*,Auto");
        RowSpacing = 8;
        Children.CollectionChanged += (_, _) => EnsureRows();
    }

    protected override void OnInitialized()
    {
        base.OnInitialized();
        EnsureRows();
    }

    private void EnsureRows()
    {
        var rowCount = Children.Count == 0
            ? 0
            : Children.Max(child => GetRow(child)) + 1;

        while (RowDefinitions.Count < rowCount)
        {
            RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        }

        while (RowDefinitions.Count > rowCount)
        {
            RowDefinitions.RemoveAt(RowDefinitions.Count - 1);
        }
    }
}
