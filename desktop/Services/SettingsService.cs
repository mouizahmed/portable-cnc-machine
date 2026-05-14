using System;
using System.IO;
using System.Text.Json;

namespace PortableCncApp.Services;

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions _json = new() { WriteIndented = true };

    private static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PortableCNC",
        "settings.json");

    // Persists desktop-local app preferences only.
    public AppSettings Current { get; private set; } = new();

    public void Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            var text = File.ReadAllText(FilePath);
            Current = JsonSerializer.Deserialize<AppSettings>(text) ?? new();
        }
        catch
        {
            Current = new();
        }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(Current, _json));
        }
        catch { }
    }
}
