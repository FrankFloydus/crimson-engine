# Quader Agent Guide

This repository is the bootstrap for a professional 3D modeling app. Keep early code boring,
testable, and future-friendly.

## Project Identity And Source Of Truth

- This repository is the native standalone Windows Quader app. It is not the Unity plugin, and it
  should not keep the old plugin/DLL protection layer behavior unless a native-app requirement
  explicitly asks for it.
- The Unity plugin checkout at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` is the active
  source of truth for modeling viewport parity: viewport camera behavior, grid visuals, gizmo
  visuals and placement, keybinds, box creation, selection modes, overlay colors, transparency,
  hover/selected states, multiselection, and depth behavior.
- The original Unity package lives at
  `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\package\com.qvertexworks.quader`.
- The original SDK lives at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\sdk`. This app keeps a
  copied SDK under `modeling/sdk`; preserve the SDK boundary and adapt native-app behavior around
  it instead of rewriting SDK semantics in UI or renderer code.
- For portable modeling, selection, picking, overlay payloads, box creation, navigation, and gizmo
  semantics, check SDK/editor-core first. Important files include:
  `sdk/packages/editor-core/src/session_selection_overlay.cpp`,
  `sdk/packages/editor-core/src/selection_policy.cpp`,
  `sdk/packages/editor-core/src/session_picking.cpp`,
  `sdk/packages/editor-core/src/overlay_visibility.cpp`, and
  `sdk/packages/editor-core/include/quader_editor_core/session.h`.
- For presentation parity, check the Unity viewport renderer and shaders first:
  `Editor/Viewport/QuaderViewportRenderer.cs`,
  `Editor/Viewport/QuaderOverlayMeshCache.cs`,
  `Editor/Viewport/QuaderViewportSettingsState.cs`,
  `Runtime/Shaders/QuaderOverlayLine.shader`,
  `Runtime/Shaders/QuaderOverlayPoint.shader`, and
  `Runtime/Shaders/QuaderOverlayFlat.shader`.
- Do not invent overlay, selection, gizmo, grid, or navigation behavior from screenshots or local
  guesses. Read the Unity plugin and SDK source, mirror it, then document any intentional
  standalone-native divergence.

## Architecture Rules

- Treat the Unity plugin at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` as the source of
  truth for Quader modeling behavior, viewport visuals, grid behavior, gizmo visuals, keybinds,
  overlays, selection, and tool interaction. Do not invent native-app behavior when parity is
  unclear; inspect the Unity plugin and copied SDK first, then implement the matching behavior.
- Vulkan is the first runtime backend. Do not introduce OpenGL-only assumptions into shared code.
- Keep backend-specific behavior behind `RenderSystem`, `PlatformWindow`, or explicit backend
  adapters so OpenGL and Metal can be added later.
- `SceneDocument` is the editor source of truth. Filament entities, buffers, materials, and views
  are render proxies derived from document state.
- UI panels do not own Filament resources directly. Route rendering and texture requests through
  renderer-facing services.
- The in-app console is an ImGui dockable panel. Do not spawn or rely on a separate console window
  for app logs.
- Use the `Logger` macros (`LOG_INFO`, `LOG_ERROR`, etc.) instead of direct `spdlog` calls outside
  the logging layer.

## UI Architecture Rules

- Use `UI` casing for UI infrastructure types (`UISystem`, `UIPanel`, `UIContext`,
  `UIPanelManager`, `UISettings`, `UIStyleTokens`), not `Ui`.
- Keep `UISystem` as the composition root for ImGui initialization, menus, dockspace, layout
  persistence, and panel registration. Put panel contents in concrete `UIPanel` classes such as
  `ViewportPanel`, `OutlinerPanel`, `InspectorPanel`, and `ConsolePanel`.
- Route frame dependencies through `UIContext`. Do not make panels reach sideways into app globals.
- Put shared UI colors, font size, paddings, and other visual constants in `UIStyleTokens` instead
  of scattering hardcoded values across panels.
- Persist dock layout through ImGui's ini file and panel visibility through `UISettings`; keep these
  under the user profile rather than repo-local files.
- For scene object names, use `SceneEntityNaming`. Mesh names start as `Mesh`, then `Mesh_1`,
  `Mesh_2`, etc.; do not show or create user-facing `SDK Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text type suffixes such as `[Mesh]` or `[Light]` in the outliner. Use plain entity
  names until icon support is added.
- Do not create placeholder startup scene content such as default meshes or fake lights unless a
  product requirement explicitly asks for it.

## Transform Preview And Scene Sync Rules

- Unity Shift+gizmo behavior is the reference: Shift duplicates only in object selection mode for
  Move/Rotate/Scale; in component modes Shift maps to topology behavior (`Move -> Extrude`,
  `Scale -> Inset`) and must not duplicate objects.
- SDK transform previews are transactions. `begin_transform_preview(true)` creates a zero-offset
  duplicate, selects the duplicate, and uses that post-duplicate state as the preview base. Preview
  update deltas are absolute from drag start, not accumulated frame deltas.
- Do not treat SDK `content_revision` alone as a unique identity for every live preview mesh state.
  Preview code can restore a base state and reapply a new delta, so bridge code that renders or
  caches scene objects must force-refresh during transform preview begin/update/active commit/cancel.
- If `AssetRegistry::replaceMesh` mutates an existing mesh under the same `AssetId`, make sure the
  `SceneDocument` revision changes before relying on `SceneRenderBridge::sync`; otherwise rendering
  can show stale geometry while SDK picking/selection sees the updated mesh.
- For Shift duplicate regressions, keep app-level coverage that verifies duplicate preview updates
  move beyond the first offset, commit at the final position, and remain pick-selectable.

## Overlay And Selection Rules

- `src/quader/render/ViewportOverlayPolicy.h` is this app's local mirror of Unity overlay
  presentation policy. Keep layer draw order, colors, alpha, widths, point sizes, and
  depth/draw-on-top decisions centralized there.
- `ModelingSessionService` should transport SDK overlay payloads and use the overlay policy for
  presentation color mapping. Do not scatter Unity color constants through unrelated files.
- `SceneRenderBridge` renders face/fill triangle overlays only. Do not reintroduce raw Filament
  `LINES`/`POINTS` or unfeathered triangle-strip fallbacks for selection overlays; that regressed
  line quality and removed the Unity-like cyan source wire.
- Wire and vertex overlays are currently drawn in `ViewportPanel` as policy-driven ImGui
  screen-space overlays to preserve crisp line quality. In element modes, only policy-depth-tested
  layers (`SourceWire`, `OpenEdge`, `HoverFaceEdge`, `HoverEdge`, `UnselectedVertex`) are clipped
  against scene geometry; selected handles stay on top.
- The current CPU clipping is a bridge until a proper Filament overlay shader matches Unity's
  `QuaderOverlayLine.shader` and `QuaderOverlayPoint.shader`. Any replacement must preserve
  Unity-style screen-space feathering, widths, point visuals, and view-space depth bias.
- Unity overlay contract: `SourceWire`, `OpenEdge`, `HoverFaceEdge`, and `HoverEdge` are
  depth-tested; `SelectedFaceEdge`, `SelectedEdge`, and `DiagnosticEdge` draw on top; selected,
  hovered, and snap vertices draw on top; face fills use Unity's depth-stamped two-pass behavior.
- The Unity layer order is the reference order: `SelectedVisibleFace`, `SelectedFaceMask`,
  `SourceWire`, `SelectedXrayFace`, `HoverFace`, `OpenEdge`, `DiagnosticEdge`,
  `SelectedFaceEdge`, `SelectedEdge`, `HoverFaceEdge`, `HoverEdge`, `UnselectedVertex`,
  `SelectedVertex`, `HoverVertex`, `SnapTargetVertex`.

## TDD Workflow

- Add or update GoogleTest coverage before implementing non-trivial core behavior.
- Keep tests fast and headless. Scene data, assets, baking descriptors, logger sinks, and viewport
  navigation must be testable without opening a window.
- GPU/window behavior is validated with manual smoke tests until a render-test harness exists.

## Material And Baking Direction

- Treat materials as renderer-independent documents first, Filament packages second.
- Preserve CPU-side mesh data in `MeshAsset`; do not throw topology/UV data away after uploading.
- Texture assets must carry usage metadata such as base color, normal, roughness, metallic,
  curvature, thickness, AO, position, ID, and masks.
- Baking is job-oriented. Add future map generation through `BakeService` and `BakeBackend`, not by
  burying it in the viewport renderer.

## Dependency Rules

- Use CMake and vcpkg manifest mode for dependencies.
- Do not vendor SDL, ImGui, spdlog, GoogleTest, or a new Filament SDK into this repo.
- Use the Filament SDK path provided by `FILAMENT_SDK_ROOT`; default is:
  `C:\Users\Drako\Desktop\quader-windows\_SOURCES\filament-v1.71.6-windows`.

## Expected Commands

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug
```

If PowerShell inherits conflicting `PATH`/`Path` entries and CMake cannot find tools, use the
local workaround:

```powershell
$p = $env:Path
cmd /c "set PATH=& set Path=$p& cmake --build --preset vs2026-debug --target quader"
```

Manual smoke checklist:

- Vulkan backend initializes.
- Main SDL window opens without a separate console window.
- Dockspace appears.
- Console panel is docked in-app and receives log lines.
- Viewport panel docks/moves and renders Quader SDK-created meshes with the default material.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.
