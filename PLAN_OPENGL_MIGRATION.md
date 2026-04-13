# Toolpath Renderer Migration Plan
> Add a native OpenGL renderer using Avalonia `OpenGlControlBase` with `Silk.NET.OpenGL` as the primary path, while keeping the current Three.js/WebView viewer as a compatibility fallback.

## Goal
Improve viewer performance, reduce app size and runtime overhead, and keep compatibility across weak, old, and unusual laptops.

This is **not** a hard cutover plan.

This plan keeps the current WebView renderer until the native renderer proves it is:

- faster on target machines
- stable on Windows, Linux, and macOS
- functionally equivalent for the dashboard workflow

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
- It **does not justify** deleting the current WebView viewer before parity is proven.

## Non-Goals
- Do not remove the WebView renderer in Phase 1.
- Do not change the G-code parser or document model.
- Do not redesign dashboard UX during renderer migration.
- Do not assume a native GL path is universally more compatible than WebGL.

---

## Renderer Strategy

We will support two renderer paths:

1. `Native GL` preferred path
   - Avalonia `OpenGlControlBase`
   - `Silk.NET.OpenGL` for GL API access

2. `WebView` fallback path
   - existing Three.js/WebView implementation
   - used when native GL initialization fails, performs poorly, or is disabled by config

### Why this architecture
- Reduces migration risk
- Preserves compatibility for odd hardware/driver combinations
- Lets us compare native vs current renderer on the same app build
- Avoids blocking the app on renderer parity work

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
Measure both renderer paths on at least:
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

### Existing pieces that will remain during migration
- `desktop/Controls/WebToolpathView.cs`
- `desktop/Services/Web/ToolpathWebViewerServer.cs`
- `desktop/Assets/WebView/Toolpath3D/`

These stay until native parity is proven.

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

Renderer Host
  -> NativeGlToolpathView        (preferred)
  -> WebToolpathView             (fallback)

NativeGlToolpathView
  -> OpenGlControlBase
  -> ToolpathGlRenderer
       -> LineRenderer
       -> GridRenderer
       -> MarkerRenderer
       -> OrbitCamera

WebToolpathView
  -> existing Three.js viewer
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

### 5. Fallback is a product feature, not temporary scaffolding
The fallback renderer should remain available until the native path has enough field confidence.

---

## New Dependencies

Add to `desktop.csproj` when native work begins:

```xml
<PackageReference Include="Silk.NET.OpenGL" Version="2.x" />
<PackageReference Include="Silk.NET.Maths" Version="2.x" />
```

Do **not** remove `WebViewControl-Avalonia` during initial phases.

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
4. Add renderer selection setting:
   - `Auto`
   - `Native`
   - `WebView`
5. On failure to initialize native GL, automatically fall back to WebView.
6. Add adaptive settings hooks for render scale, optional FPS cap, and simplified effects.

**Result:** app can launch with either renderer.

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
1. Benchmark native vs WebView on representative machines.
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

### Phase 5 - Default switch
1. Make native GL the default in `Auto` mode if benchmarks and stability are acceptable.
2. Keep WebView fallback available.
3. Add logging/diagnostics to record which renderer was selected and whether fallback occurred.

**Result:** native becomes the primary production path without removing compatibility.

### Phase 6 - Optional WebView retirement
Only after enough confidence:
1. decide whether fallback is still needed
2. remove WebView/CEF dependencies only if native path is proven acceptable across target machines

This phase is optional.

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
Only after Phase 6:
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
- removing fallback too early would reduce compatibility
- shipping without playback parity would regress existing dashboard behavior

---

## Decision Gate

Proceed with the native renderer only if all of the following remain true after Phase 2 or 3:

- native path is measurably lighter or faster on target laptops
- dashboard behavior is at or near current parity
- fallback works when native initialization fails
- maintenance complexity stays acceptable

If those are not true, keep the current WebView path and treat the native renderer as an optional experiment, not the default.
