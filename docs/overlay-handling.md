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

- The active or selected mesh emits ordinary `SourceWire` lines for the editable topology.
- The active or selected mesh also emits `SourceWireDepthStamp` triangle metadata. This is metadata
  for visibility/picking support, not a rendered fill layer.
- Component modes must not emit object-mode `SelectedXrayFace` fill for the whole active mesh.
- Hover candidates in component modes may emit source-wire lines and source-wire depth stamps before
  click selection. This is how component handles appear when the mesh is not yet selected.
- Selected component overlays are additional highlights over the source wire. They must not delete
  source-wire topology.

The common failure mode is mixing these rows: adding object-mode x-ray to component modes, or
removing component source-wire visibility while trying to suppress object-mode x-ray.

## Source Wire And Depth Stamps

`SourceWire` is editable topology. It should be crisp, stable, screen-space expanded, and drawn on
top after render extraction. Do not depth-test ordinary source-wire line batches directly against
the shaded mesh, because that causes disappearing or fading edges at distance and shallow angles.

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

## Regression Tests

Before changing overlays, find or add coverage in these suites:

- `tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp`
  - object mode selected mesh x-ray and selected outline;
  - component mode source-wire color and no object x-ray;
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
  - outward source-wire depth-stamp culling;
  - inside-out source-wire depth-stamp visibility;
  - source-wire depth stamps producing no render batches;
  - selected/hover face fills being two-sided.
- `tests/core_tests.cpp`
  - native overlay layer order;
  - depth mode, draw-on-top, color, width, and style resolver policy;
  - service-level selection overlay extraction.

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
