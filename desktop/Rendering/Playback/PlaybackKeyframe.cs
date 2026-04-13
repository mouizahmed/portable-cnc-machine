namespace PortableCncApp.Rendering.Playback;

/// <summary>
/// Represents a playback position marker tied to a source G-code line and segment index.
/// </summary>
public readonly record struct PlaybackKeyframe(int SourceLine, int SegmentIndex);
