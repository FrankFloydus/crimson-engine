<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Overlay Handling Internals

This is an internal architecture note for Quader's native viewport overlays. It records the current
selection/source-wire behavior so future changes do not reintroduce the regressions around x-ray
wires, inverted meshes, fading edges, hidden handles, and hover-only mesh wires.

Current code, tests, and the active user request are always authoritative. When behavior changes,
update this file and the tests named below in the same change.

## Owners

- Modeling overlay extraction:
  `src/editor/modeling/runtime/session_selection_overlay.cpp`
- Modeling source-wire picking and hover policy:
  `src/editor/modeling/runtime/session_picking.cpp`
- Polygon overlay topology:
  `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- Picking cache signatures:
  `src/editor/projection/picking_cache_projection.cpp`
- Native overlay presentation policy:
  `src/render/viewport_overlay_policy.cpp`
- Render extraction and batching:
  `src/render/overlay/overlay_renderable_builder.cpp`
  and the line/point/face-fill builders under `src/render/overlay/`
- User-configurable overlay style mapping:
  `src/render/overlay/viewport_overlay_style_resolver.cpp`

Do not solve overlay problems in UI event handlers, viewport shell code, or one-off Filament
fallbacks. UI code should request overlay frames and display render-owned proxies.

## Pipeline

The overlay path is intentionally split:

1. Modeling runtime builds semantic overlay primitives:
   lines, points, triangles, layer ids, owner object ids, and component ids.
2. Render projection converts runtime overlay layers to native viewport overlay layers and style
   roles.
3. `ViewportOverlayStyleResolver` applies viewport settings for colors, widths, point sizes, and
   role-specific overrides.
4. `OverlayRenderableBuilder` converts the styled frame into screen-space line/point quads and
   face-fill batches.
5. `OverlayRenderableCache` and `OverlayProxyFactory` own render proxies and Filament state.

The runtime decides what editor topology exists. The renderer decides how that topology is drawn.
Picking decides what is interactive. Do not let one of those responsibilities erase another.

## Selection-Mode Status Matrix

Object mode:

- Hovering a mesh does not draw a mesh wire. Hover may keep a candidate internally so clicking can
  select the mesh, but the source wire stays invisible until selection.
- A selected mesh emits selected mesh topology lines. Small object selections also emit the
  object-mode x-ray face fill; dense multi-selections suppress that face fill and keep the selected
  wire as the stable selection signal so viewport navigation does not multiply face-fill renderables
  by every selected mesh.
- A selected mesh in object mode must not emit ordinary `SourceWire` lines or
  `SourceWireDepthStamp` metadata.

Vertex, edge, and face modes:

- The active edit mesh emits ordinary `SourceWire` lines for editable topology. Other selected
  meshes in component modes must not emit full-mesh source wire just because they are selected.
- Entering component mode from a selected mesh must immediately show that mesh's `SourceWire`,
  even before any component has been selected. This is the default active-edit-mesh wire.
- The active edit mesh also emits `SourceWireDepthStamp` triangle metadata. This is metadata for
  visibility/picking support, not a rendered fill layer.
- Component modes must not emit object-mode `SelectedXrayFace` fill for the whole active mesh.
- Hover candidates in component modes may emit source-wire lines and source-wire depth stamps before
  click selection. This is how component handles appear when the mesh is not yet selected.
- Selected component overlays are additional highlights over the source wire. They must not delete
  source-wire topology.

The common failure mode is mixing these rows: adding object-mode x-ray to component modes, or
removing component source-wire visibility while trying to suppress object-mode x-ray.

## Component Source-Wire Stickiness

Component source-wire ownership has two different states:

- Default active edit mesh: selected object or active object entered from object mode, with no
  selected components yet.
- Sticky component edit mesh: object with selected components for the current component mode.

The default active edit mesh is only a fallback. It draws `SourceWire` while there is no component
hover candidate, so switching from object mode into face, edge, or vertex mode immediately shows
editable topology. Once the user hovers another mesh, the fallback active wire yields to the hover
candidate unless the active mesh has a real component selection for the current mode.

Sticky component ownership is mode-specific:

- Face mode sticks only when `selection.faces` is non-empty.
- Edge mode sticks only when `selection.edges` is non-empty.
- Vertex mode sticks only when `selection.vertices` is non-empty.
- Object selection, `active_object_id`, or a component selection from another mode is not enough.
  For example, selected faces do not make the source wire stick in edge mode.

The intended runtime behavior is:

1. Select a mesh in object mode, switch to face mode, and see that mesh's cyan source wire
   immediately.
2. Hover a different mesh before selecting any face; the cyan source wire moves to the hovered mesh.
3. Select a face on the first mesh; the cyan source wire sticks to that mesh and beats hover
   candidates until the face selection is removed or converted away.
4. Remove the last selected component; with no hover candidate, the active edit mesh wire is visible
   again as fallback.

Implementation ownership:

- `component_selection_has_mode_components(...)` owns the mode-specific selection predicate.
- `inferred_component_source_wire_object_id(...)` owns semantic frame fallback selection:
  sticky component selection, then hover candidate, then fallback active selected mesh.
- `ModelingOverlayService::selection_overlay_base_frame(...)` caches the fallback active edit mesh
  source wire.
- `ModelingOverlayService::selection_overlay_current_frame(...)` composes hover overlays and removes
  the fallback active source wire when a different hover candidate replaces it.

Do not make `object.selected` alone sticky in component mode. That recreates the old bug where
clicking anywhere in component mode pinned the cyan wire to the previous object. Also do not require
selected components just to draw the initial wire after a mode switch; that breaks the object-to-edit
mode workflow.

## Source Wire And Depth Stamps

`SourceWire` is editable topology. It should be crisp, stable, screen-space expanded, and drawn on
top after render extraction. Do not depth-test ordinary source-wire line batches directly against
the shaded mesh, because that causes disappearing or fading edges at distance and shallow angles.

Component selected and hover handles are different from source wire. Projection marks selected and
hover component edge/vertex handles with component-specific overlay sources, and render extraction
depth-tests only those marked primitives. This prevents selected component highlights from x-raying
through opaque geometry while preserving the old source-wire behavior for the active edit mesh in
inverted-normal rooms.

`SourceWireDepthStamp` is metadata:

- It can exist in the overlay frame as triangles.
- `OverlayRenderableBuilder` must emit no render batches for the `SourceWireDepthStamp` layer.
- It is used by render-side visibility filtering for outward-facing meshes and by interaction
  policy to keep source-wire handles coherent.
- It must not CPU-delete the source-wire topology from the runtime overlay frame.

Render-side source-wire visibility has one special case:

- Outward-facing source-wire depth stamps may cull source-wire handles that are occluded by the
  same mesh surface.
- Inside-out source-wire depth stamps must keep internal topology visible, because an inverted room
  needs its internal edges and vertices drawn from outside and inside.

This means source-wire depth stamps are allowed to affect which handles are visible for a camera,
but they are not a replacement wire layer and they are not a rendered x-ray fill.

## Picking Is Separate From Visualization

Do not use render visibility as picking truth.

Picking policy lives in modeling runtime picking owners. Editor surface picking uses:

- `pick_backfaces = true`
- `prefer_front_facing_faces = false`

This makes flipped/backfacing meshes pick the nearest visible surface rather than preferring an
authored front-facing face behind it.

For source-wire component picking:

- Active source-wire handles are tested before shaded surface fallback.
- A source-wire hit is accepted only when it is not occluded by the nearest visible same-mesh
  surface.
- From outside an inverted room, occluded interior edges/vertices should not be pickable.
- From inside that room, those same visible interior edges/vertices should be pickable.

Visualization and picking can therefore differ: an inverted room can show internal source-wire
topology from outside, while picking rejects handles hidden behind the nearest surface until the
camera enters the room.

## Face Fills

Selected and hover face fills use the depth-stamped two-pass face-fill pipeline:

- depth-only pass with reversed-depth `GreaterOrEqual`;
- equal-depth color pass;
- no triangle culling;
- two-sided transparency mode.

This is required for inverted faces. Do not restore backface culling for selected or hover face
fill, and do not duplicate reversed triangles as a workaround.

Object-mode selected mesh x-ray fill is not the same thing as component-mode source-wire
visualization. Keep those concepts separate in tests and code.

## Render Policy Invariants

The policy owner is `src/render/viewport_overlay_policy.cpp`.

- Draw-on-top lines include `SourceWire`, `SelectedFaceEdge`, `SelectedEdge`, `HoverFaceEdge`,
  `HoverEdge`, `GroupBounds`, `DiagnosticEdge`, `WireframeXray`, and tool line/handle layers.
- Draw-on-top points include selected, hover, snap, and tool handles.
- Component-mode selected/hover edge and vertex handles carry component overlay sources and are
  batched as depth-tested primitives even though their layers remain draw-on-top by default.
- Depth-tested layers include base wireframe, open edges, and unselected vertices unless policy says
  otherwise.
- `WireframeBase` is the depth-tested overlay policy layer. Wireframe viewport mode must use
  `WireframeXray` for unselected scene topology so hidden/back edges remain visible and authored
  polygon edges are not lost behind depth-only mesh proxies.
- Global depth-only mesh proxies live on `kViewportOverlayLayerMask`; `ViewportRenderPassCoordinator`
  includes that layer only while the shaded/rendered scene is visible. Wireframe mode excludes it so
  invisible mesh depth cannot make the view read as a filled solid.
- Filament uses reversed depth, so depth-tested overlay passes use `GreaterOrEqual`.
- Line and point overlay bias is a small post-projection device-depth offset only. Never move line
  or point geometry in screen X/Y to fix z fighting.
- Device-domain line and point overlay renderables must disable renderable culling.
- Layer order lives in `kViewportOverlayDrawOrder`; do not duplicate the order elsewhere.
- Camera-only viewport navigation must keep selected overlays visible while avoiding whole-frame
  signature rescans and Filament proxy churn. `OverlayRenderableCache` owns this by caching
  `OverlayFrameSignatures`, rebuilding only camera-dependent layers, updating compatible line/point
  proxy buffers in place, and constructing source-wire depth-stamp visibility filters only when the
  frame actually contains `SourceWireDepthStamp` metadata.
- Persistent selected-object rendering batches selected mesh wires into one renderable, caches local
  wire topology per selected entity/mesh revision, and streams the transformed line vertex buffer in
  place on transform-only scene revisions. It only keeps the translucent object face-fill proxies
  below the dense-selection threshold. Wireframe viewport mode must color selected scene-wire entries
  with the selected wire color instead of relying on the normal selected-object overlay layer,
  because wireframe mode uses per-pane grid layer masks.

## bgfx Renderer Translation

The bgfx port should keep the same ownership split as Quader: build semantic overlay frames outside
the renderer, then let the renderer batch and draw those primitives. Do not put selection rules in
bgfx draw code.

### Semantic Overlay Data

Keep renderer-facing overlay records close to these shapes:

- Line: layer, style role, source kind, world-space start/end, color token or resolved color, pixel
  width, object id, face id, edge id/endpoints.
- Point: layer, style role, source kind, world-space position, color, pixel size, object id, vertex
  id.
- Triangle: layer, style role, world-space vertices, color, object id, face id.

The `source kind` must distinguish at least:

- object selection;
- component selection;
- component hover;
- source wire;
- tool preview;
- scene/wireframe diagnostics.

This is required because selected/hover component edges may use the same visual layer names as
draw-on-top overlays, but they need depth-tested batches. Layer alone is not enough.

### Selection Policy Before Rendering

Implement the component source-wire owner before producing bgfx draw batches:

1. If object mode, selected meshes emit selected object wire and optional object x-ray fill. They do
   not emit `SourceWire`.
2. If component mode and an object has selected components for that exact mode, that object owns the
   sticky source wire.
3. Else if a component hover candidate exists, the candidate owns the source wire.
4. Else if a selected/active mesh exists, that mesh owns the fallback edit source wire.
5. No other selected mesh emits full source wire.

Selected component handles are emitted separately from source wire. Hover component handles are also
separate. A selected face/edge/vertex can add highlighted handles over the wire, but must not remove
the source-wire topology.

### Recommended bgfx Views

Use explicit views or explicit submit ordering for these overlay phases:

1. Scene/depth view: normal shaded scene depth.
2. Optional overlay depth-proxy view: depth-only mesh proxies when shaded/rendered scene is visible.
3. Face-fill depth view: selected/hover face fill depth stamp.
4. Face-fill color view: selected/hover face fill color where depth equals the stamped fill depth.
5. Depth-tested component handle view: selected/hover component lines and points.
6. Draw-on-top line/point view: source wire, object selected wire, hover helpers, tool lines, snap
   handles, diagnostics, and x-ray wireframe.

Do not depth-test `SourceWire` in the component handle view. It belongs in the draw-on-top view.

### bgfx State Mapping

Draw-on-top source wire and object/helper lines:

- Build screen-space-expanded line quads from world-space endpoints.
- Use alpha blending and color writes.
- Disable depth test by omitting bgfx depth-test state bits, or use an always-pass state if your
  renderer wrapper requires one.
- Disable depth writes.
- Disable culling.
- Keep stable pixel width from viewport settings.

Depth-tested component selected/hover lines and points:

- Build the same screen-space line/point quads, but submit them in the component handle view.
- Bind the scene/overlay depth buffer for testing.
- Use `BGFX_STATE_DEPTH_TEST_LEQUAL` for normal depth buffers.
- Use `BGFX_STATE_DEPTH_TEST_GEQUAL` for reversed-depth buffers.
- Disable depth writes.
- Use alpha blending and color writes.
- Disable culling.
- Apply a tiny post-projection depth bias toward the camera. Do not move X/Y.

Depth bias rule:

- Normal depth: closer means smaller depth, so subtract a small NDC/device depth offset.
- Reversed depth: closer means larger depth, so add a small NDC/device depth offset.
- In clip-space terms, apply the offset as `clip.z += ndcOffset * clip.w`, with the sign chosen for
  the depth convention, then clamp to the valid clip/depth range.
- Keep the offset tiny and capped; it is only to avoid same-surface z fighting, not to make hidden
  component handles x-ray.

Selected and hover face fills:

- Submit a depth-only pass first with depth writes enabled.
- Submit a color pass with alpha blending and equal-depth testing against the fill depth pass.
- Use `LEQUAL`/`EQUAL` for normal depth or the equivalent reversed-depth tests for a reversed buffer.
- Disable triangle culling for both passes.
- Treat the fill as two-sided transparency; do not duplicate reversed triangles as a workaround.

Source-wire depth stamps:

- Keep them as semantic metadata, not a bgfx draw batch.
- If the bgfx project implements source-wire self-visibility filtering, compute it as a separate
  camera-dependent filter over the metadata. Never erase source-wire topology from the semantic
  overlay frame.
- Preserve the inverted-normal rule: inside-out rooms keep internal topology visible. Do not use
  face winding or normal direction to hide all backfacing/internal source-wire edges.

### Batching And Caching

Cache by primitive kind, layer, style role, source kind, depth mode, texture/material, and viewport
view id. A single visual layer can split into two bgfx batches when one subset is component
depth-tested and another subset is draw-on-top.

Camera-only changes:

- Rebuild screen-space line and point buffers because quad expansion depends on projection.
- Do not rebuild world-space face-fill geometry unless the semantic overlay frame changed.
- Recompute any source-wire depth-stamp visibility filter only when the frame contains depth-stamp
  metadata.

Scene transform-only changes:

- Stream transformed line/point vertices where possible.
- Keep selected object wire topology cached by mesh/entity revision.

### bgfx Regression Checklist

Before accepting the port, verify these cases visually and with headless policy tests where
possible:

- Object mode hover does not show source wire before selection.
- Object mode selection shows selected wire and optional object x-ray fill, but no `SourceWire`.
- Switching a selected mesh into component mode immediately shows fallback source wire.
- Hovering another mesh before selecting a component moves source wire to the hover candidate.
- Selected face, edge, or vertex sticks source wire only in the matching mode.
- Component mode never shows whole-object x-ray fill.
- `SourceWire` stays crisp and draw-on-top on inverted rooms from outside and inside.
- Component selected/hover handles depth-test and do not x-ray through opaque geometry.
- Inverted selected/hover face fills are visible because face-fill passes are two-sided.

## Regression Tests

Before changing overlays, find or add coverage in these suites:

- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
  - object mode selected mesh x-ray and selected outline;
  - component mode source-wire color and no object x-ray;
  - component mode source wire emitted only by the active edit mesh;
  - component source-wire stickiness, including fallback active wire, hover replacement, and
    mode-specific selected components;
  - component hover candidate source-wire behavior;
  - inverted open-cube inner edge hover/deselect color;
  - selected component overlays preserving active mesh source wire;
  - hover-only frame omissions and owner keys.
- `tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp`
  - flipped surface nearest-pick behavior;
  - outside-vs-inside source-wire handle picking for inverted meshes.
- `tests/render_extraction_tests.cpp`
  - screen-space line expansion;
  - source-wire draw-on-top batches;
  - component selected/hover line and point batches split into depth-tested render state;
  - outward source-wire depth-stamp culling;
  - inside-out source-wire depth-stamp visibility;
  - source-wire depth stamps producing no render batches;
  - selected/hover face fills being two-sided.
- `tests/core_tests.cpp`
  - native overlay layer order;
  - depth mode, draw-on-top, color, width, and style resolver policy;
  - service-level selection overlay extraction, including active-only component source wire.

If a visual overlay bug is fixed without a test in one of those suites, expect the same regression
to return.

## Change Checklist

When touching overlay behavior:

1. Identify whether the problem is modeling topology, picking, render extraction, render policy, or
   user style mapping.
2. Add or update a focused headless test for that owner first.
3. Preserve the selection-mode matrix above.
4. Preserve the distinction between `SourceWire` rendered lines and `SourceWireDepthStamp`
   metadata.
5. Rebuild the relevant test target and relink `quader` when a manual viewport check is expected.
6. Update this document and `memory.md` when resolving a hard-to-rediscover overlay issue.

If a quick visual experiment breaks another overlay mode, revert that experiment and return to this
matrix instead of layering more special cases on top.
