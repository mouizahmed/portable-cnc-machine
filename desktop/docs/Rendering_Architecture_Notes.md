# Rendering Architecture Notes

## Current Viewer Choice

For the MVP, the desktop app uses an embedded Three.js WebGL viewer inside the app's WebView.

This is the current recommendation because it gives us:

- a working GPU-accelerated 3D toolpath viewer quickly
- shared camera, interaction, and rendering behavior without building a full native 3D engine
- lower implementation and maintenance cost for standard CNC preview needs

## What We Had Before

The previous built-in renderer was a custom native C# Avalonia-drawn control in `desktop/Controls/ToolpathView.cs`.

That old renderer was:

- native C#
- custom-drawn inside Avalonia
- not a true native GPU 3D engine like Vulkan, DirectX, Ab4d, or Silk.NET

So while it was "native," it was not the same as adopting a dedicated native 3D graphics engine.

## Native GPU Engine Option

Using a native GPU engine such as Ab4d.SharpEngine or Silk.NET would be a moderate-to-high effort change.

It would likely be better only if the toolpath viewer becomes a major product feature and we need:

- much higher performance on very large toolpaths
- tighter integration with the Avalonia visual tree
- richer CNC-specific visualization such as stock removal, mesh rendering, picking, simulation, or custom GPU effects
- long-term ownership of the rendering stack without an embedded browser surface

## Recommendation

For the MVP:

- keep the embedded Three.js WebGL viewer

For a future advanced visualization phase:

- consider replacing it with a true native GPU rendering stack
