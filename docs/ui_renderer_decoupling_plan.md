<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# UI And Renderer Decoupling Plan

Internal architecture plan generated from a focused 10-agent audit on 2026-07-02.

This plan refines `docs/architecture_migration_plan.md` for one goal: make the
current ImGui UI and Filament renderer replaceable by a future Qt UI and custom
renderer without rewriting editor, scene, modeling, selection, command, viewport,
or asset ownership.

## Summary Verdict

Quader is not yet cleanly decoupled from the current UI and renderer. The codebase
has good domain owners for many editor behaviors, but the app/runtime layer still
binds SDL, ImGui, Filament, viewport rendering, overlays, render extraction, and
UI presentation into one runtime shape.

The replacement goal should not be "abstract everything." It should be a set of
small, testable ports:

- UI sends editor intent through `QuaderEditorApi`, typed commands, and focused
  editor services.
- UI presents panel/menu/viewport state through backend-specific presenters.
- Viewport state stays in `ViewportWorkspaceService`, `ViewportInputRouter`, and
  `ViewportInteractionController`.
- Render consumes immutable scene, material, overlay, viewport, and tool-preview
  snapshots.
- Filament, ImGui, SDL, Win32, Qt, and the future renderer stay behind adapter
  targets.

The safest migration path is adapter-first. Keep the current ImGui + Filament app
working while adding neutral contracts, then move the existing implementation
behind those contracts, then add Qt and the custom renderer side by side.

## Current Coupling Map

### App And Build Graph

- `App` owns `PlatformWindow`, `RenderSystem`, `SceneRenderBridge`, `UISystem`,
  `ProjectSession`, and `QuaderEditorApi` directly in `src/app/app.hpp`.
- `App::initialize` creates the SDL window, initializes Filament from the native
  window handle, initializes `SceneRenderBridge` from raw Filament objects, and
  initializes `UISystem` from `SDL_Window*` and `RenderSystem`.
- `App::render_frame` passes live `SceneDocument`, `AssetRegistry`,
  `ModelingSessionService`, and `EditorSelectionService` into UI drawing and
  viewport preparation.
- `quader_runtime` combines app, platform, render, UI, editor viewport, and API
  runtime sources in one target, then links `filament_sdk`, `SDL3::SDL3`,
  `imgui::imgui`, `lodepng`, and Win32 libraries publicly.
- Filament SDK validation and material compilation are unconditional at configure
  time, so a headless/API-only or custom-renderer build cannot yet exist.

### UI Shell

- `UISystem` is the current ImGui shell and also owns SDL backend setup, ImGui
  frame lifecycle, panel composition, chrome actions, relative mouse capture,
  cursor warping, icon texture allocation, ImGui draw-data rendering through
  Filament, and runtime hook installation.
- `UISystem::draw` installs `QuaderUIRuntimeHooks` and
  `QuaderPreferencesRuntimeHooks` every frame. Those hooks should be app/editor
  composition state, not per-frame UI drawing work.
- `UIContext` exposes `QuaderEditorApi`, `SceneRenderBridge`, `SceneDocument`,
  `AssetRegistry`, `ModelingSessionService`, `EditorSelectionService`, settings,
  localization, style, icons, focus pointers, and cursor-capture pointers as one
  broad mutable bag.
- `UIPanel::draw(UIContext&)` and `UIPanelManager::draw` are ImGui presenter
  contracts, not backend-neutral panel contracts.
- `LocalizationService::imgui_label(...)`, `UIIconSet` with `ImTextureID`, and
  `UISettingsPaths::imgui_ini_file` leak ImGui conventions into otherwise
  reusable UI state.

### Viewport Panel

- `ViewportPanel` is the main UI/render coupling point.
- It owns one `ViewportRenderer` per pane, initializes those renderers from raw
  `RenderSystem` Filament accessors, resizes render targets, then presents scene
  and overlay textures via `ImGui::Image`.
- It writes pane render size and visibility into `ViewportWorkspaceService`, reads
  ImGui hover/focus state, builds `ViewportInputFrame`, routes shortcuts/tools,
  handles context menus, asks for cursor capture, and draws HUD/decorations.
- It calls `SceneRenderBridge::sync_selected_object_overlay`,
  `SceneRenderBridge::sync_scene_wireframe_overlay`, and
  `SceneRenderBridge::set_overlay_frame` with live scene/asset/render data.
- It builds or transforms overlay frames using render overlay builders and live
  `SceneDocument`, `AssetRegistry`, and `ModelingSessionService` data.

Good retained boundaries:

- `ViewportWorkspaceService` already owns durable viewport layout, focused pane,
  pane view, shading, grid preset, render size, visibility, and cameras.
- `ViewportInputFrame` and `ViewportInputRouter` are close to backend-neutral.
- `ViewportInteractionController` and `TransformGizmoInteraction` are the right
  owners for transform gizmo lifecycle and should be preserved.

### Render Backend

- `RenderSystem` exposes raw `filament::Engine`, `filament::Renderer`,
  `filament::Scene`, `filament::SwapChain`, `filament::Material`, and
  `TextureRegistry` accessors to app/UI callers.
- `SceneRenderBridge` initializes from raw Filament engine/scene/materials and
  syncs from live `SceneDocument` / `AssetRegistry`.
- `ViewportRenderer`, `ViewportRenderTarget`, `TextureRegistry`,
  `ImGuiFilamentRenderer`, render caches, and overlay caches expose Filament
  types in headers.
- `SceneRenderExtractionPlan` stores raw pointers to `MeshAsset`,
  `MeshComponent`, `MaterialComponent`, and `LightComponent`.
- `SceneRenderSyncService`, mesh caches, material binding, texture binding,
  light caches, selected-object overlays, and scene wireframe overlays all consume
  live scene/assets directly.

### Platform And Input

- `PlatformWindow` owns useful behavior, but its public seam is SDL-shaped:
  `SDL_Window*`, `SDL_Event`, `SDL_Cursor`, `SDL_HitTestResult`, and SDL system
  cursor types appear in the public header.
- `App` directly pumps SDL events and wires them into both `PlatformWindow` and
  `UISystem`.
- `ViewportInputFrameBuilder` reads ImGui state and types directly. Qt will need
  a separate physical input source that maps into the same canonical Quader
  viewport input frame.

### Resources

- Runtime packaging is CMake-driven rather than manifest-driven.
- Filament `.mat` sources compile to `.filamat` through `matc` and are copied as
  app runtime assets.
- UI icons are loaded by `UISystem` into Filament textures and represented as
  `ImTextureID`.
- Fonts and icon glyph ranges are ImGui-bound.
- IBL runtime loading assumes Filament/KTX environment layouts.
- Locales, raw PNG icons, raw TTF fonts, default material PNGs, and semantic
  catalogs are reusable if they gain backend-neutral roles.

## Target Architecture

### Ownership Rules

- `src/api` remains the intent and automation front door.
- `src/ui` presents state and collects intent. It must not allocate renderer
  resources or derive renderer truth.
- `src/viewport` owns viewport workspace, camera, layout, and backend-neutral
  input routing state.
- `src/editor/viewport` owns transform gizmo and viewport interaction state
  machines.
- `src/editor/projection` and `src/editor/modeling/**` own picking, selection
  projection, source-wire policy, tool preview projection, and modeling overlay
  semantics.
- `src/render/contracts` or equivalent owns renderer-neutral snapshot, texture,
  surface, viewport render, and UI image handle contracts.
- `src/render/filament` owns current Filament device, material, buffer, texture,
  render target, cache, and draw submission mechanics.
- Future custom renderer code implements the same render contracts without
  entering UI, editor, scene, or modeling owners.
- Future Qt code implements UI/platform/viewport presentation contracts without
  entering render backend internals.

### Required Ports And DTOs

- `PlatformWindowHost`: native handle, drawable size, DPI, focus/minimized/
  occluded state, can-render state, placement, chrome actions, cursor shape,
  relative mouse capture, cursor warp, and live-resize callbacks.
- `PlatformEvent` / `HostInputSnapshot`: key, mouse, wheel, text, focus, resize,
  expose, DPI, and close events expressed in Quader types.
- `UIHost`: frame lifecycle, shell composition, panel/menu presentation, viewport
  preparation, UI render-data preparation, UI render submission, and window action
  consumption.
- `UIRuntimeService`: panel visibility/focus, notifications, locale, preferences,
  navigation-gizmo preference, and preference saving.
- `UIPanelDescriptor` / `PanelModel`: stable panel id, title key, default
  visibility, docking role, and backend-specific presenter factory.
- `MenuModel`: stable menu/action descriptors backed by public commands and
  public editor APIs; ImGui/Qt draw those models differently.
- `ViewportPaneViewModel`: pane id, rect, hover/focus/chrome state, shading/view
  labels, presented texture/surface handles, UV/capacity metadata.
- `ViewportInputSource`: maps ImGui, Qt, or test input into `ViewportInputFrame`.
- `ViewportSurfaceService`: owns per-pane render surfaces, resize/defer policy,
  texture handles, overlay handles, and presentation metadata.
- `RenderDevice` / `RenderFrame`: backend-neutral render lifecycle, frame stats,
  scene submission, viewport surface rendering, UI image upload, and presentation.
- `RenderSceneSnapshot`: immutable scene/material/light/mesh records with
  revisions and no pointers to live document or asset components.
- `RenderOverlaySnapshot`: selected-object wires, scene wireframe, hierarchy
  wires, group bounds, selected light markers, source-wire data, gizmos, marquee,
  tool fills, and modal preview overlays.
- `RenderToolPreviewSnapshot`: source entity, material fallback, transform, and
  immutable mesh payload or payload key.
- `RuntimeResourceManifest`: logical resource ids, roles, source paths, runtime
  paths, licenses, owners, and backend-specific variants.

## Render Snapshot Contract

Only a snapshot builder should read `SceneDocument` and `AssetRegistry` for render
submission. Renderer services and caches should consume immutable records.

Required records:

- `RenderSceneSnapshot`
  - scene revision;
  - asset revision;
  - selection/visibility/transform/mesh-binding revisions where needed;
  - scene settings;
  - mesh records;
  - light records;
  - material table;
  - texture table;
  - overlay snapshot;
  - tool preview snapshot.
- `RenderMeshRecord`
  - entity id;
  - mesh id and mesh revision;
  - world transform;
  - visibility/layer flags;
  - cast-shadows flag;
  - double-sided flag;
  - material assignment signature;
  - resolved material slots;
  - payload key.
- `RenderMeshPayload`
  - immutable vertex/index buffers or spans;
  - primitive spans;
  - bounds;
  - render-specific authored wire/topology payloads.
- `RenderMaterialRecord`
  - material id and revision;
  - base color, metallic, roughness, normal/emissive/other fields;
  - sorted texture slots;
  - sorted per-slot overrides.
- `RenderTextureRecord`
  - texture id and revision;
  - color space;
  - resolved source path or immutable pixel source descriptor.
- `RenderLightRecord`
  - entity id;
  - light type;
  - color, intensity, range, cone values, shadow flag;
  - world transform.
- `RenderSceneSettingsRecord`
  - environment id/settings;
  - exposure;
  - tone mapping;
  - clear color;
  - render-mode defaults.

Do not copy large mesh buffers every frame. Use payload tables keyed by asset id
and revision, immutable shared payloads, or spans with explicit lifetime tied to
the snapshot.

Do not put Filament-specific compensation, material package names, KTX
environment assumptions, or backend texture pointers in the neutral records.

## Migration Phases

### Phase 0 - Guardrails And Baseline

- [ ] Add a dedicated UI/render architecture guardrail suite or extend
  `tests/oop_architecture_guardrail_tests.cpp`.
- [ ] Add a target/include matrix with a temporary allowlist for current
  violations. Track each allowlist entry to a phase in this plan.
- [ ] Guard that backend-neutral headers do not include `<imgui.h>`,
  `<SDL3/...>`, `<filament/...>`, `utils::Entity`, or backend-specific render
  headers.
- [ ] Guard that `src/ui/**` does not allocate/destroy Filament resources after
  the texture service lands.
- [ ] Guard that render snapshot records contain no raw pointers to
  `SceneDocument`, `AssetRegistry`, `MeshAsset`, `MeshComponent`,
  `MaterialComponent`, or `LightComponent`.
- [ ] Add an app frame-order test seam or source guardrail documenting the
  required order: process host events, run UI/update intent, build snapshots,
  prepare viewport surfaces, begin render frame, render viewports, render UI,
  present.
- [ ] Fix or invert existing guardrails that currently assert the old ImGui /
  Filament / `ViewportPanel` path once the new contracts land.

### Phase 1 - Split Build Targets

- [ ] Split current `quader_runtime` into adapter-visible targets before broad
  code moves:
  - `quader_platform_contracts`;
  - `quader_platform_sdl_win32`;
  - `quader_ui_contracts`;
  - `quader_ui_models`;
  - `quader_ui_imgui`;
  - `quader_render_contracts`;
  - `quader_render_snapshot`;
  - `quader_render_filament`;
  - `quader_app`.
- [ ] Move `filament_sdk`, `SDL3::SDL3`, `imgui::imgui`, `lodepng`, Win32 UI
  libraries, and `matc` material targets behind adapter targets.
- [ ] Make public examples link the smallest API/editor target possible instead
  of the full UI/render runtime.
- [ ] Stop unconditional Filament SDK validation for builds that do not enable
  the Filament adapter.
- [ ] Reduce broad `PUBLIC src` exposure after each split so include direction is
  enforceable.

### Phase 2 - Platform And UI Host Contracts

- [ ] Add backend-neutral platform/window/input structs beside the existing SDL
  path.
- [ ] Replace public `PlatformWindow` SDL types with Quader platform types,
  leaving SDL inside `quader_platform_sdl_win32`.
- [ ] Introduce `PlatformWindowHost` and make the SDL implementation satisfy it.
- [ ] Add `PlatformEvent` / `HostInputSnapshot` and map SDL events into canonical
  Quader key, mouse, wheel, text, modifier, focus, resize, and close data.
- [ ] Split `UISystem` into a backend-neutral shell/composition service and
  `ImGuiUIHost`.
- [ ] Move `QuaderUIRuntimeHooks` and `QuaderPreferencesRuntimeHooks`
  installation out of `UISystem::draw` into app/editor startup composition.
- [ ] Keep existing native frame behavior, live resize, cursor capture, and
  custom chrome behavior covered before adding Qt.

### Phase 3 - UI Context, Panels, Menus, And Intent

- [ ] Replace broad `UIContext` with narrower contexts:
  - intent context with `QuaderEditorApi` and command execution;
  - presentation context with localization, style, semantic icons, and frame
    timing;
  - panel state context with visibility, focus, and settings;
  - viewport context with viewport workspace/input/surface data.
- [ ] Create backend-neutral panel descriptors for stable ids, title keys,
  default visibility, docking role, and presentation state.
- [ ] Convert `MainMenuBar` into a command/menu model builder. Keep ImGui and Qt
  drawing separate from action model construction.
- [ ] Move ImGui-only label logic out of `LocalizationService` into an ImGui label
  adapter.
- [ ] Split portable UI settings from ImGui dock `.ini` state and future Qt dock
  state.
- [ ] Route `ToolSettingsPanel` writes through `context.editor.tools()` or a typed
  public tool-settings batch API.
- [ ] Expose inspector selected-transform edits as public editor intent instead
  of constructing `SceneTransformDragService` from UI with live document/assets.
- [ ] Keep outliner and inspector scene edits on public APIs and add guardrails
  preventing new UI owner-internal mutation paths.

### Phase 4 - Viewport Surface And Input Decoupling

- [ ] Add `ViewportPaneViewModel` and have ImGui viewport drawing consume it.
- [ ] Move per-pane render target ownership out of `ViewportPanel` and into
  `ViewportSurfaceService`.
- [ ] Keep `ViewportWorkspaceService` as the owner of pane layout, view, shading,
  grid, focused pane, render size, visibility, and cameras.
- [ ] Convert `ViewportInputFrameBuilder` away from ImGui types. It should consume
  `HostInputSnapshot`, pane geometry, and focus/capture state.
- [ ] Keep the current ImGui input source as an adapter; add test fakes and a
  Qt-ready input source later.
- [ ] Split viewport context-menu action discovery/modeling from ImGui popup
  drawing.
- [ ] Convert viewport HUD/info/fly overlays into either render overlay records or
  toolkit-specific HUD models.
- [ ] Preserve `ViewportInputRouter`, `ViewportInteractionController`, and
  `TransformGizmoInteraction` as editor/viewport owners.

### Phase 5 - Render Scene Snapshots

- [ ] Add `render_scene_snapshot.hpp` with neutral value records and no Filament,
  scene document, or asset registry includes.
- [ ] Add `render_scene_snapshot_builder.*` as the single allowed render read of
  `SceneDocument` and `AssetRegistry`.
- [ ] Build snapshots after UI/editor intent has run and before render sync.
- [ ] Replace `SceneRenderExtractionPlan` pointer records or retire it behind
  `RenderSceneSnapshot`.
- [ ] Change `SceneRenderBridge::sync` and `SceneRenderSyncService::sync` to
  consume `const RenderSceneSnapshot&`.
- [ ] Change mesh caches, mesh factories, material binding, texture binding,
  render signatures, and light caches to consume snapshot records.
- [ ] Apply scene settings from the snapshot instead of reading
  `project_session_.scene()` during render frame submission.
- [ ] Add snapshot equivalence tests before removing the old live extraction path.

### Phase 6 - Overlay And Tool Preview Decoupling

- [ ] Add `RenderOverlaySnapshot` records for selected-object wires, scene
  wireframe, hierarchy wires, group bounds, selected light markers, selected face
  fills, source-wire metadata, gizmos, marquee, and modal/tool overlays.
- [ ] Move `ViewportSceneOverlayBuilder` scene/asset-derived work out of
  UI/render backend code and into editor/projection or snapshot building.
- [ ] Convert `SelectedObjectOverlayCache` and `SceneWireframeOverlayCache` from
  `sync(document, assets, ...)` to `sync(snapshot_records, ...)`.
- [ ] Keep render backend ownership of overlay batch building, proxy/cache reuse,
  draw order, depth/blend/cull policy, and backend buffers.
- [ ] Remove tool-preview conversion from `UISystem`; centralize preview snapshot
  generation in modeling/projection/snapshot code while preserving default
  material fallback behavior.
- [ ] Add parity tests for object mode, component modes, wireframe mode, dense
  selected objects, source-wire depth stamps, gizmos, marquee, box/cut/mirror
  previews, selected lights, and group overlays.

### Phase 7 - Renderer Facade And Filament Adapter

- [ ] Narrow `RenderSystem` to backend-neutral lifecycle, frame stats, viewport
  surface, scene snapshot submission, UI image upload, and frame submission APIs.
- [ ] Move raw `filament::Engine`, `filament::Renderer`, `filament::Scene`,
  `filament::Material`, `filament::SwapChain`, `filament::Texture`, and
  `utils::Entity` access into `quader_render_filament` internals.
- [ ] Introduce a Filament context/resource bundle used only inside the adapter.
- [ ] Move `ViewportRenderer`, `ViewportRenderTarget`, material libraries,
  environment libraries, render caches, overlay renderable builders, and
  `ImGuiFilamentRenderer` behind adapter boundaries.
- [ ] Keep UI image rendering adapter-specific. Qt should not inherit ImGui draw
  data or `ImGuiFilamentRenderer` unless explicitly running the ImGui adapter.
- [ ] Add a minimal fake/null render backend for headless contract tests.

### Phase 8 - Resource Manifest And Backend Variants

- [ ] Add one runtime resource source of truth with logical ids, roles, source
  paths, runtime paths, owners, license evidence, and backend variants.
- [ ] Add `UiFontCatalog` for default UI font, icon font, glyph ranges, fallback,
  and license.
- [ ] Add `UiIconCatalog` for semantic ids such as `app.logo`, `entity.mesh`,
  `state.locked`, and `state.hidden`; do not expose `ImTextureID`.
- [ ] Add `MaterialRoleCatalog` for roles such as `mesh.lit`, `overlay.line`,
  `overlay.point`, `grid.ground`, and `ui.imgui_composite`, with Filament and
  custom-renderer variants.
- [ ] Extend environment/default-material catalogs with backend-specific variants
  while keeping semantic scene/environment ids stable.
- [ ] Generate or validate CMake copy/compile rules from the manifest so
  resources cannot drift from packaging.
- [ ] Move icon/font upload into UI/render resource services. ImGui can translate
  semantic icons to `ImTextureID`; Qt can translate them to `QIcon` / `QPixmap`.

### Phase 9 - Adapter Parity And Replacement Readiness

- [ ] Keep the current ImGui + Filament app running behind the new contracts.
- [ ] Add a Qt UI host prototype only after platform/input/UI contracts have
  parity tests.
- [ ] Add a custom renderer prototype only after render snapshots and viewport
  surface contracts have parity tests.
- [ ] Keep manual smoke checks for OS window behavior, GPU/swapchain behavior,
  material rendering, and pixel output until a render readback harness exists.
- [ ] Do not delete the ImGui or Filament adapters until the Qt/custom paths pass
  the same editor behavior, viewport, overlay, resource, and smoke gates.

## Verification Map

- Source-policy and architecture guardrails:
  `quader_guardrail_tests`.
- Public API, UI intent, viewport workspace, commands, history, selection, and
  render behavior:
  `quader_tests`.
- Runtime modeling overlay/picking/tool behavior:
  `quader_modeling_domain_tests`.
- Recommended new behavior suites:
  - `RenderSceneSnapshotBuilderTests`;
  - `RenderSnapshotEquivalenceTests`;
  - `ViewportSurfaceServiceTests`;
  - `ViewportInputSourceTests`;
  - `UIRuntimeServiceTests`;
  - `PanelModelTests`;
  - `MenuModelTests`;
  - `RuntimeResourceManifestTests`.
- Existing suites to preserve or expand:
  - `ViewportWorkspaceServiceTests`;
  - `ViewportContextMenuPolicyTests`;
  - `ViewportInteractionControllerTests`;
  - `ViewportToolGuardrailTests`;
  - `SceneRenderBridgeTests`;
  - `SceneRenderExtractionTests`;
  - `OverlayRenderableCacheTests`;
  - `ViewportSceneOverlayBuilderTests`;
  - modeling-domain overlay and picking tests.
- Manual smoke remains required for:
  - native window chrome and live resize;
  - relative mouse capture and cursor warp;
  - Vulkan/Filament swapchain creation;
  - viewport texture presentation;
  - material and overlay pixel output.

## Readiness Gates

The codebase is ready for a Qt UI spike when:

- backend-neutral UI contracts compile without ImGui, SDL, or Filament includes;
- panel/menu models exist for viewport, outliner, inspector, console, settings,
  and tool settings;
- `UIContext` no longer exposes render bridge, full document/assets, or broad
  modeling service access to every panel;
- viewport input can be built from a fake or non-ImGui input source;
- portable UI settings are separated from ImGui dock state.

The codebase is ready for a custom renderer spike when:

- render snapshots exist and renderer sync does not consume live
  `SceneDocument` / `AssetRegistry`;
- app/UI-facing render headers expose no raw Filament types;
- viewport surfaces are represented by backend-neutral handles;
- overlays and tool previews are submitted as semantic records or snapshot
  payloads;
- resources have logical roles and backend variants;
- Filament SDK and `matc` are private to the Filament adapter target.

## High-Risk Areas

- Overlay drift: source-wire depth stamps, x-ray selected topology, selected face
  fills, dense-selection batching, reversed-depth behavior, layer masks, and
  camera-only cache invalidation have many hard-won invariants.
- Snapshot performance: naive full mesh copies every frame will be expensive.
  Use revision-keyed immutable payloads.
- Live resize and native chrome: behavior is split across `PlatformWindow`,
  `App`, and `UISystem`; preserve ordering and callback behavior.
- Input semantics: ImGui and Qt differ in focus, capture, high DPI, text input,
  key repeat, drag thresholds, and mouse grab semantics.
- Resource packaging: Filament material compilation, KTX environments, ImGui font
  atlases, and Qt icon/font use must not fight over one runtime layout.
- Guardrail inversion: several current guardrails assert old ImGui/Filament paths.
  Update them in the same changes that introduce the replacement contracts.

## Suggested Execution Order

1. Land guardrails and target split scaffolding with allowlists.
2. Add platform/window/input contracts and keep SDL behavior behind them.
3. Split UI context/hooks/settings/menu/panel models without changing visuals.
4. Extract viewport surface/input presentation while preserving
   `ViewportWorkspaceService` and `ViewportInputRouter`.
5. Add render scene snapshots and convert render sync/caches in narrow slices.
6. Move overlays and tool previews to snapshot/projection records.
7. Hide Filament behind the render adapter and move UI textures into render-owned
   services.
8. Add runtime resource manifest and backend variants.
9. Prototype Qt and the custom renderer side by side behind the stable contracts.
