<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Rendering And Overlays

Vulkan is the first runtime backend. Do not introduce OpenGL-only assumptions into shared code.

Backend-specific behavior belongs behind `RenderSystem`, `PlatformWindow`, or explicit backend
adapters so OpenGL and Metal can be added later.

For selection/source-wire overlays, also read `docs/overlay-handling.md` before changing behavior.
That document records the current object/component mode matrix, source-wire depth-stamp contract,
inverted-mesh visualization rules, and regression test map.

## Render Ownership

- `SceneDocument` is editor truth.
- Filament entities, buffers, materials, and views are render proxies derived from document state.
- UI panels do not own Filament resources directly.
- Route rendering and texture requests through renderer-facing services.
- `SceneRenderBridge` is a renderer facade/composition root, not the owner of new render algorithms.
- Filament resource ownership and render extraction belong in `src/render/**`.

## Overlay Rules

- `src/render/viewport_overlay_policy.hpp` is the native app overlay presentation policy.
- Keep layer draw order, colors, alpha, widths, point sizes, and depth/draw-on-top decisions
  centralized there.
- Do not scatter overlay color constants through unrelated files.
- Runtime selection overlays flow through `ModelingOverlayService`, `SelectionOverlayProjection`,
  `QuaderRenderApiRuntimeAccess::sync_selection_overlay`, and then `SceneRenderBridge`.
- `SceneRenderBridge` submits overlay frames through `OverlayRenderableCache` and
  `OverlayProxyFactory`; extraction and build policy belong in `src/render/overlay/**`, especially
  `ViewportSceneOverlayBuilder`, `OverlayRenderableBuilder`, and `ViewportOverlayStyleResolver`.
- Do not reintroduce raw Filament `LINES`/`POINTS`, UI-backend wire fallbacks, or unfeathered
  triangle-strip shortcuts for selection overlays.
- Wire and vertex overlays are screen-space quads.
- `ViewportOverlayStyleResolver` applies `ViewportSettings` colors, sizes, and selection-mode scoped
  overrides. Do not bypass it with one-off render constants.
- Overlay materials live at:
  - `resources/materials/quader_overlay_line.mat`
  - `resources/materials/quader_overlay_point.mat`
- Filament device-domain overlay vertices must be normalized device coordinates with `w = 1`.
- Clip lines against the near plane before pixel expansion using unbiased view-space geometry.
- Apply line and point overlay depth bias as a tiny post-projection device-depth offset only; do not
  move line or point overlay geometry in screen X/Y.
- Device-domain overlay renderables must disable renderable culling.
- Filament uses reversed depth. For depth-tested overlay passes, use `DepthFunc::GE`.

## Overlay Contract

- Depth-tested overlay layers include `WireframeBase`, `OpenEdge`, and unselected vertices unless
  `ViewportOverlayPolicy` marks the layer/geometry draw-on-top.
- Draw-on-top: `SourceWire`, `SelectedFaceEdge`, `SelectedEdge`, `HoverFaceEdge`, `HoverEdge`,
  `GroupBounds`, `DiagnosticEdge`, `WireframeXray`, selected/hover/snap vertices, tool line/handle
  overlays, and tool fill layers such as `ToolPlane` and `ToolPreviewFill`.
- Wireframe viewport mode uses `WireframeXray` for unselected scene topology. Do not route that
  mode through the depth-tested `WireframeBase` layer.
- Global depth-only mesh proxies live on `kViewportOverlayLayerMask` and must only be visible in the
  overlay pass while the shaded/rendered scene is visible. Wireframe mode excludes the layer so
  invisible mesh depth cannot mask the grid or x-ray topology.
- Active source-wire topology is interactive edit geometry: component hover and pick paths must test the
  visible active source-wire handles before falling back to shaded surface-depth picking.
- Active `SourceWire` handles are tested first, but accepted only when not occluded by the nearest
  visible surface hit. Editor surface picking enables backfaces and disables authored-front-face
  preference: `pick_backfaces = true`, `prefer_front_facing_faces = false`.
- `SelectedVisibleFace`, `SelectedXrayFace`, `SelectedFaceMask`, and `HoverFace` use depth-stamped
  two-pass, two-sided face fills: depth-only `GE` pass, then equal-depth color pass, no triangle
  culling. Do not restore backface culling for selected or hover face fills.
- `SourceWireDepthStamp` remains a non-rendered metadata layer. Do not use it to CPU-delete source-wire
  edges during render extraction. It may appear as frame triangle metadata, but
  `OverlayRenderableBuilder` must emit no render batches for `SourceWireDepthStamp`. Active
  source-wire topology should render as a crisp draw-on-top editor wire, while picking/hover
  ownership decides which handles are interactive.
- Object-mode selected mesh wires remain on the selected-edge path.
- Native layer order lives in `kViewportOverlayDrawOrder` and is guarded by tests. Do not copy the
  full order into a second source of truth.

## Picking And Caches

- Runtime source-wire picking lives in `src/editor/modeling/runtime/session_picking.cpp`.
- `PickingCacheProjection` world-transforms pickable objects, filters invisible or unselectable
  scene entities, and includes every pick option that affects cached face geometry in its signature.
- `ModelingSelectionService` owns picking-cache invalidation. Do not duplicate picking cache
  invalidation or source-wire-first policies in UI, API, or tool call sites.
