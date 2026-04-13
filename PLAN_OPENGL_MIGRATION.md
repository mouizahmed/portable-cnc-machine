# Toolpath Renderer Migration Plan
> Replace the current Three.js/WebView toolpath viewer with a native OpenGL renderer using Avalonia `OpenGlControlBase` and `Silk.NET.OpenGL`.

## Implementation Status (2026-04-12)

**Phases 0–3 are implemented and building.** The native renderer is live in the dashboard.

- **Phase 0 ✅** — `IToolpathViewportBridge`, `ToolpathViewportState`, `RendererSelectionMode` created. `DashboardViewModel` no longer depends on `ToolpathWebViewerServer` for playback (now uses `DateTime`-based time accumulation).
- **Phase 1 ✅** — `Silk.NET.OpenGL 2.22.0` added. `NativeGlToolpathView` (OpenGlControlBase), `ToolpathGlRenderer`, `GlMath`, `GlShaderHelper`, `OrbitCamera`, `LineRenderer`, `GridRenderer`, `MarkerRenderer` implemented. Shaders embedded as resources.
- **Phase 2 ✅** — Static geometry upload from `GCodeDocument`. Grid/axes/stock box rendering. Orbit/pan/zoom camera. Completed-vs-remaining split via `gl_VertexID` uniform (no buffer rebuilds). Category visibility via uniform bitmask. Idle rendering via `RequestNextFrameRendering()`. Theme color support.
- **Phase 3 ✅** — Playback driven by C# timer (`AdvancePlayback` time-accumulation). `PreviewLine` binding drives `CurrentLine` → renderer computes completed segment count. Marker position tracks last segment end at current line.
- **Phase 4** — Performance benchmarking and validation. Not yet done.
- **Phase 5** — WebView/CEF removal. Kept for now; `WebToolpathView` and `ToolpathWebViewerServer` still present but not rendered in dashboard.

**Files added:** `desktop/Rendering/` directory with 13 C# files + 4 GLSL shaders.
**`AllowUnsafeBlocks`** enabled in csproj for GL pointer operations.

## Goal
Improve viewer performance, reduce app size and runtime overhead, and keep compatibility across weak, old, and unusual laptops.

This plan assumes work happens in an isolated environment / branch until the native renderer is ready to replace the current implementation.

## Why Migrate

### Current issues worth addressing
- The embedded WebView/CEF stack materially increases app size.
- The current viewer uses a separate browser runtime plus JS/C# bridge logic.
- Scene/state synchronization is split across C#, local HTTP, JS polling, and browser rendering.
- On lower-end laptops, browser/runtime overhead may matter as much as raw drawing cost.

### Current performance issues that must be addressed
- The current viewer rebuilds toolpath geometry when playback progress changes.
- Scene data is serialized to JSON in C#, transported over loopback HTTP, and parsed again in JS.
- JS polls viewer state continuously, and the Avalonia control also pushes state on a timer.
- Rendering runs continuously with `requestAnimationFrame`, even while idle.
- Internal render resolution follows full device pixel ratio with no cap.
- Arc tessellation can create a very large segment count for preview purposes.
- Theme changes rebuild the scene instead of updating only the affected visual state.
- Current segment coloring creates avoidable short-lived JS allocations during rebuilds.
- Large files are fully loaded into memory before parsing.

### What migration is and is not solving
- It **can** reduce memory, startup cost, and packaging size.
- It **can** improve frame pacing and large-scene handling if the native renderer is designed around mostly static GPU buffers.
- It **does not automatically** make the viewer faster on every laptop.
- It still requires parity for the dashboard workflow before merge.

## Non-Goals
- Do not change the G-code parser or document model.
- Do not redesign dashboard UX during renderer migration.
- Do not assume a native GL path is universally more compatible than WebGL.

---

## Performance Targets

The migration is only successful if it meets measurable goals.

### Acceptance criteria
- Viewer startup should be no worse than the current implementation on a typical laptop.
- Memory use should be materially lower than the WebView path.
- Packaged app size should shrink materially after WebView/CEF is removed from production builds.
- Orbit, pan, zoom, and scrubbing should remain responsive on large files.
- Playback should avoid visible hitching on lower-end laptops.
- Large toolpaths should not require full geometry rebuilds when only playback position changes.
- Idle viewer behavior should avoid unnecessary full-speed rendering work.
- High-DPI displays should not force uncapped internal render cost on weak machines.
- Preview fidelity should remain acceptable while arc-heavy files avoid runaway segment counts.

### Required benchmarking
Measure the current implementation and the native implementation on at least:
- one modern laptop
- one older laptop with integrated graphics
- one low-power / weaker CPU machine if available

Capture:
- app startup time to viewer ready
- memory usage after loading a representative file
- FPS while orbiting a large toolpath
- scrubbing responsiveness
- playback smoothness
- behavior on high-DPI displays
- idle CPU/GPU usage while the viewer is visible but not interacting
- load time and responsiveness for an arc-heavy file

---

## Current State

### Existing pieces to keep
- `desktop/Services/GCode/GCodeDocument.cs`
- `desktop/Services/GCode/GCodeParser.cs`
- Dashboard ViewModels and UI bindings
- `ThemeResources`

### Existing pieces that should be abstracted
The current WebView stack is doing more than hosting HTML:
- scene creation from `GCodeDocument`
- viewer state transport
- playback progress reporting back to C#
- camera reset / current-line synchronization

Those responsibilities need a renderer-neutral contract before the native path is introduced.

---

## New Architecture

```text
Dashboard / ViewModels
  -> IToolpathViewportBridge
       -> scene input
       -> state input
       -> playback feedback
       -> camera commands
       -> capability / health reporting

NativeGlToolpathView
  -> OpenGlControlBase
  -> ToolpathGlRenderer
       -> LineRenderer
       -> GridRenderer
       -> MarkerRenderer
       -> OrbitCamera
```

### Core principle
The app should talk to a renderer-neutral bridge, not directly to the current web server implementation.

---

## Key Design Decisions

### 1. Use `OpenGlControlBase` and `Silk.NET` together
- `OpenGlControlBase` owns the Avalonia GL surface and lifecycle.
- `Silk.NET.OpenGL` provides the GL API for buffers, shaders, and draw calls.

These are complementary, not competing choices.

### 2. Keep geometry mostly static
Do **not** rebuild all toolpath buffers when `CurrentLine` changes.

Instead:
- upload toolpath geometry once when the document changes
- keep segment buckets static by category where possible
- update completed/remaining rendering using draw ranges, index ranges, uniforms, or other cheap GPU-side state
- rebuild only when the document or visibility categories materially change

This is the main performance requirement for weak laptops.

### 3. Avoid continuous expensive work when idle
The renderer should not consume maximum frame budget when nothing is changing.

Preferred behavior:
- render continuously only while interacting, animating playback, or animating the marker
- otherwise render on demand or at a reduced cadence

### 4. Cap quality adaptively
The renderer should support lower-cost modes for weak systems:
- capped internal render scale
- optional FPS cap
- simplified marker effects
- optional disabling of point cloud / gizmo / nonessential polish

## New Dependencies

Add to `desktop.csproj` when native work begins:

```xml
<PackageReference Include="Silk.NET.OpenGL" Version="2.x" />
<PackageReference Include="Silk.NET.Maths" Version="2.x" />
```

Keep `WebViewControl-Avalonia` only until the native renderer is ready for full replacement in the isolated branch.

---

## Functional Scope for Native Renderer

### Must-have for parity
- toolpath line rendering
- completed / remaining path split
- rapid / cut / arc / plunge visibility toggles
- grid
- axes
- stock box
- camera presets
- orbit / pan / zoom
- theme-aware colors
- current tool position marker
- scrubber / playback synchronization with C#

### Nice-to-have after parity
- orientation gizmo
- extra marker polish
- advanced line thickness work
- GPU picking
- future simulation features

---

## Native Rendering Plan

### Scene model
Build a native scene representation from `GCodeDocument` once per loaded file:
- bounds
- segment metadata
- per-category geometry buffers
- playback keyframes

### Line rendering
Support the same logical categories as the current viewer:
- rapid
- cut
- arc
- plunge

Track completed vs remaining without full buffer rebuilds on every progress update.

Additional requirements:
- precompute or cache per-segment colors instead of recreating color objects during every update
- keep GPU uploads tied to document changes, not playback position changes
- make large segment counts observable in diagnostics so arc-heavy files can be profiled

### Marker rendering
Port the current tool marker behavior:
- marker mesh
- current position
- playback interpolation
- optional pulse/glow effects

The marker can ship with simpler visuals first, but it must preserve position behavior.

### Camera and input
Implement:
- fit-to-bounds
- preset views
- orbit
- pan
- zoom
- damping if it does not hurt responsiveness on weaker hardware

### Theme handling
Listen to the same app theme changes and apply equivalent palette values in native rendering.

Theme switches should update only the visual resources that changed unless a full rebuild is genuinely required.

### Parsing / preview geometry policy
The preview pipeline should separate controller fidelity from preview cost.

Required follow-up:
- review arc tessellation density for preview rendering
- allow a coarser preview representation when segment count becomes excessive
- keep preview responsive even if exact preview geometry is slightly simplified

---

## Implementation Phases

### Phase 0 - Shared contracts
1. Introduce a renderer-neutral bridge/service for:
   - scene updates
   - state updates
   - playback position reporting
   - camera reset / preset commands
2. Refactor current WebView path to use the same bridge.
3. Do not change behavior yet.
4. Add baseline instrumentation for:
   - scene build time
   - JSON payload size
   - playback update cost
   - renderer selection
   - idle usage measurements

**Result:** app no longer depends directly on WebView-specific plumbing.

### Phase 1 - Native GL bootstrap
1. Add `Silk.NET.OpenGL` and `Silk.NET.Maths`.
2. Create `NativeGlToolpathView` based on `OpenGlControlBase`.
3. Build GL context lifecycle, resize handling, invalidation, and disposal.
4. Add adaptive settings hooks for render scale, optional FPS cap, and simplified effects.

**Result:** native renderer is available for development and parity work.

### Phase 2 - Core native parity
1. Implement static toolpath geometry upload from `GCodeDocument`.
2. Implement grid, axes, stock box.
3. Implement camera presets and mouse controls.
4. Implement current-line progress visualization without full geometry rebuilds on every update.
5. Implement theme colors.
6. Compare visuals and interactions against the current viewer.
7. Ensure idle rendering can sleep or reduce cadence when there is no active interaction or animation.
8. Cap internal render scale on high-DPI displays, with room for adaptive tuning later.

**Result:** native renderer can replace the current viewer for basic previewing.

### Phase 3 - Playback and marker parity
1. Port playback keyframe generation.
2. Port marker position logic.
3. Port playback reporting back to C# so dashboard scrubber stays synchronized.
4. Implement smooth playback animation.
5. Match current dashboard behavior closely enough that users do not lose functionality.
6. Keep playback updates GPU-cheap: no full geometry rebuilds per playback step.

**Result:** native renderer reaches dashboard-level parity.

### Phase 4 - Performance validation
1. Benchmark native implementation against the current implementation on representative machines.
2. Profile hot paths:
   - buffer uploads
   - draw counts
   - per-frame allocations
   - input response
3. Add quality degradations if needed:
   - lower internal render scale
   - cap FPS
   - disable optional marker effects
   - disable points/gizmo by default on weak systems
4. Validate that idle usage is materially lower than the current always-rendering path.
5. Validate that arc-heavy files do not create unacceptable load times or interaction stalls.
6. Review whether preview tessellation needs a configurable quality tier.

**Result:** native path is validated for the target hardware mix.

### Phase 5 - Replacement cutover
1. Replace `WebToolpathView` with the native view in the dashboard.
2. Remove WebView-specific infrastructure once native parity and performance targets are met.
3. Re-run packaging and hardware validation after dependency removal.

**Result:** native renderer fully replaces the current implementation.

---

## Files to Add
- `desktop/Rendering/IToolpathViewportBridge.cs`
- `desktop/Rendering/ToolpathViewportState.cs`
- `desktop/Rendering/RendererSelectionMode.cs`
- `desktop/Rendering/NativeGlToolpathView.cs`
- `desktop/Rendering/ToolpathGlRenderer.cs`
- `desktop/Rendering/LineRenderer.cs`
- `desktop/Rendering/GridRenderer.cs`
- `desktop/Rendering/MarkerRenderer.cs`
- `desktop/Rendering/OrbitCamera.cs`
- `desktop/Rendering/Playback/PlaybackKeyframe.cs`
- `desktop/Rendering/Shaders/line.vert`
- `desktop/Rendering/Shaders/line.frag`
- `desktop/Rendering/Shaders/mesh.vert`
- `desktop/Rendering/Shaders/mesh.frag`

## Files to Modify
- `desktop/desktop.csproj`
- `desktop/Views/Pages/DashboardView.axaml`
- `desktop/ViewModels/MainWindowViewModel.cs`
- `desktop/ViewModels/DashboardViewModel.cs`
- `desktop/Controls/WebToolpathView.cs`
- `desktop/Services/Web/ToolpathWebViewerServer.cs`

These modifications are expected because the current ViewModels and WebView path depend on Web-specific scene/playback transport.

## Files to Delete Later
After replacement cutover:
- `desktop/Controls/WebToolpathView.cs`
- `desktop/Services/Web/ToolpathWebViewerServer.cs`
- `desktop/Assets/WebView/Toolpath3D/`
- `WebViewControl-Avalonia` package reference

---

## Risks

### Native GL risks
- older/quirky drivers may fail or behave inconsistently
- macOS OpenGL is deprecated
- GL lifecycle and thread-safety inside Avalonia must be handled carefully

### Performance risks
- full buffer rebuilds during scrubbing would kill the main benefit
- excessive per-frame allocations would negate gains
- fancy marker/gizmo effects may hurt weak machines if always enabled
- uncapped render scale on high-DPI screens can erase gains on weak laptops
- always-on rendering can waste CPU/GPU even when the viewer is idle
- arc-heavy files can still overload the preview path if tessellation remains too dense

### Product risks
- shipping without playback parity would regress existing dashboard behavior

---

## Decision Gate

Proceed with the native renderer only if all of the following remain true after Phase 2 or 3:

- native path is measurably lighter or faster on target laptops
- dashboard behavior is at or near current parity
- maintenance complexity stays acceptable

If those are not true, do not merge the native replacement yet.
