# Crimson integration in the full Quader application

The `crimson-renderer` application uses Quader's complete modeling application,
native Qt main window, document sessions, dock layout, tools and input owners.
`CRIMSON_ENGINE_APPLICATION` adds the Engine domains and panels to that shell.
Quader's source modeling documents retain their authority; an Engine level is a
separate schema-25 document associated with its modeling session.

This directory adapts immutable Quader rendering publications to the canonical
renderer in [`thirdparty/crimson-renderer`](../../../thirdparty/crimson-renderer).
Renderer algorithms and backend changes belong in that submodule's canonical
repository. The consumer keeps the matching gitlink and dependency recipe;
another renderer checkout or a local submodule URL is not a distribution input.

## Native ownership and frame boundary

- `QApplication`, `QMainWindow` and Quader's Qt widgets own application lifetime,
  window state, keyboard focus, pointer capture, docking and geometry.
- `QtVulkanCompositionWidget` selects its QRhi backend before exposure. Its name
  is retained from the original Quader architecture; the Engine application
  uses the Qt-owned Direct3D 12 device and queue. The composed target carries the
  actual backend, native resource and destination generation.
- `CrimsonRenderHost` owns one canonical `crimson::shared::Renderer`. It visits
  the complete visible pane set before recording and submits the shared frame
  once. Modeling panes, Engine Scene/Game, material previews, typed document
  previews and asset cards use this owner. A pane copy does not advance a frame.
- `begin_application_frame` pumps bounded model/texture uploads and retirement
  once before preparation. Worker completion and readback polling do not submit
  or wait for GPU work.
- Direct3D11 borrowed work runs inside `CrimsonD3D11ContextState`. The primary
  device/context owns a separate native context-state object; nested callbacks,
  initialization and shutdown capture and restore the complete prior Qt state.
  A detached consumer's ExternalContent scope alone cannot invalidate the
  primary device's QRhi bookkeeping. Native state is restored before returning
  to Qt, and teardown defers COM release until the outer scope exits.
- `CrimsonNativeQueueHandoff` orders Qt command recording and canonical native
  copies on the borrowed queue. Submitted-copy tickets remain retained after a
  document callback is cleared; destination resize/reuse needs confirmed native
  completion. A timeout never authorizes reuse.

Engine `LevelSession` owns CPU resource preparation, assets, scene/history,
runtime simulation and Play input. It receives the canonical renderer after the
native host is initialized. `LevelPanels` publishes a prepared Engine frame
before Quader starts recording. Repeated prepare callbacks reuse that frame
within the same update epoch unless geometry, camera, visibility, preferences or
document state changes. No Engine SDL window, borrowed SDL child or independent
application loop participates in this path.

## Modeling geometry and presentation

`CrimsonSceneProjectionCache` translates a complete pinned render snapshot and
its transient tool preview into one candidate generation. Independent position,
attribute and index revisions retain their original owners. Tool suppression
removes replaced committed occurrences and lights. Solid curve previews use the
existing `CurveSolidGeometryCache`; sampled curves, light markers and group
bounds remain ordinary editor overlays. Rejected candidates retain the accepted
publication and its owners.

Point-light icons retain Quader's original procedural bulb image and projected
quad plan through the canonical textured editor primitive. The image generator
is shared with the original renderer adapter; sampled alpha, selection tint,
physical pixel size, depth state and backing crop remain part of the accepted
editor publication. Spot and Directional lights retain their original point and
direction-line gizmos. They do not acquire a different icon recipe.

The lightweight modeling candidate retains its original albedo transport.
`source_materials` additionally pins the complete Standard material snapshot for
Rendered presentation, and `instance_cast_shadows` retains each occurrence's
effective shadow flag. These values share the same accepted scene generation.

`CrimsonRichScene` prepares the Rendered candidate through the existing Engine
`ResourceManager`. Geometry workers consume immutable streams and exact
occurrence sampling matrices, preserve original LOD0 triangles, source material
slots, circular/quad UV payloads and vertex color, and produce no GPU work.
`CrimsonRichGeometry` holds stripped model metadata alongside its original-LOD
upload packets. The GUI configures and queues those uploads through the
canonical renderer.

Standard materials use `CrimsonStandardMaterial` packets containing surface
values, sampled bindings, source identity and GPU leases. Encoded or disk images
are decoded on resource workers. Scalar edits reuse unchanged texture leases;
separate roughness/metallic maps, normal convention, UV transforms and the
authored diffuse model remain independent. Base-color image alpha selects the
canonical alpha-blended surface path. Quader stores source light RGB in linear
space; the adapter encodes the renderer's display-color transport once before
its canonical decoder. Energy conversion uses `light_engine_values` and the
original Quader preview exposure, and preserves its local minus-Y light axis.

Accepted rich worlds pin occurrence geometry, material arrays, textures, lights
and source publications. Each view frame owns its settings and accepted world;
its `RenderScene` borrows only from those immutable owners. Geometry/image cache
keys include all three `RenderSceneBindingKey` fields and a fresh activation
domain. Shadow/GI world identity changes with that domain. Numeric mesh,
material or entity IDs from another scene activation therefore cannot alias an
old world. Superseded worker interests are canceled and old geometry is evicted
only after its frozen occurrence leases are released.

The host preflights every Rendered pane using `prepare_rendered_view`, records
using `render_rendered_view`, and submits alongside the rest of the frame. The
pane's exact `ViewFrame` matrices, projection, depth convention and editor frame
remain authoritative. Packaged IBL preparation is shared with material previews
and asset cards through `prepare_crimson_packaged_environment`.

## Engine document previews

`AssetDocuments` owns canonical typed document/history/save/conflict controllers
and their native Qt windows. `LevelDocumentPreviews` adds transient Ragdoll,
Animation and Particle controls inside those windows. The document owns authored
state; the pane owns camera, playback and isolated simulation state. Before a
document or preview disappears, its typed preview binding is cleared and its
unique weak lifetime expires. The host rejects expired record callbacks while
retaining already-submitted copy tickets through native completion.

Ragdoll authoring keeps a full-height Bones sidebar, a composed center viewport
and a resizable right split for simulation and body/joint properties. It uses
the existing source validator, body fitting, posed collider picker, isolated
60-Hz `PhysicsWorld`, native mouse constraints and body-local authoring math.
Stopped-preview T/R/E gestures use an immutable baseline and the document's
transaction identity; cancellation cannot cancel a newer replacement gesture.
Test scope and manual pins remain transient. Scene gizmos have separate input
and transaction owners.

Animation and Particle panes reuse their canonical binding/evaluation and CPU
simulation owners. The Decal pane restores its Plane, Sphere, Cylinder, Cube
and Rounded Cube receiver choices, transient camera and Edit Material action.
Its conforming patches use the existing resource-worker chart builder, with
separate preview geometry and target ownership from Material documents.
Their composed outputs use distinct renderer preview targets
and the shared host submission. Material preview focus is arbitrated by the
existing Quader semantic preview surface so an inactive tool retains its own
accepted image.

## Remaining presentation limits

Modeling presentation uses a transient canonical `ModelingPresentation` packet.
It binds the original independent sky/IBL intensity, source tone operators,
atmospheric fog and material diagnostics without editing Engine Environment
persistence or generated graph ABI/source revisions. Retired ACES Legacy and
Display Range names retain their authored values and resolve to the same ACES
owner as the source application. Filmic remains its distinct source curve.
Fog is evaluated per surface before alpha composition; authored distance,
cutoff, maximum opacity, height/color/IBL and in-scattering controls are retained.
Surface-grid hints reuse Quader's original authored-face planner in an optional
GPU stream; adjacent equal-material face ranges still coalesce into draw calls.
The primary vertex layout remains 108 bytes.

Shaded views with a surface grid reuse the fixed Standard program's existing
camera-relative shaded-light recipe. Diffuse/Specular modes use the source
material channel substitutions; UV diagnostics retain the packaged sampled
checker. Scalar material edits, view modes and grid controls are transient and do
not edit model documents. Instance Edit context styling retains the original
`InstanceEditStyle` linear tint/desaturation/source-opacity recipe. The scene
publication preserves each occurrence's dim flag; view-owned immutable style
packets keep panes independent. Compatible instances batch within the same dim
class, and style alpha participates in view-local transparent ordering. Rich
views apply the opacity through the existing camera-local presentation path;
the shared authored shadow world stays unchanged. Context activation, settings
edits and reset invalidate that view's settings/background without geometry
uploads. The canonical implementation and source/license
provenance are in
[`modeling-presentation.md`](../../../thirdparty/crimson-renderer/src/modeling/modeling-presentation.md).
Source integration alone does not establish complete pixel or performance parity;
the native fixtures and their recorded results remain the verification boundary.

The original `quader-crimson` optional GPU selection-depth source is absent in the
retained backend. Exact CPU surface-query selection remains authoritative:
`MarqueeSurfaceDepthCache` reports unavailable certificates and the existing
candidate resolver falls through to cached `scene_surfaces.nearest_hit`, retaining
visibility and sidedness. The optional GPU certificates accelerate only vertex/
face marquee selection; their absence does not remove selection operations.

## Regression entry points

- `CrimsonSceneProjectionTests`: immutable stream reuse, partial preview
  suppression, complete curve/light generations, shadow provenance and rejected
  candidate retention.
- `CrimsonEditorProjectionTests`: actual preview coordinates, suppression,
  selection dimensions and camera-only reuse.
- `CrimsonIconProjectionTests`: original sampled bulb pixels, alpha, pixel size,
  backing crop, camera-only stream reuse, preview suppression and source-specific
  Point/Spot gizmos. The borrowed-DX12 `verify_editor_icons` helper checks actual
  textured pixels, image reuse, resize and rejected-preflight retention.
- `CrimsonRichGeometryTests`: original LOD/source-slot preservation, analytic UV
  attributes, occurrence sampling, authored-face projection hints, adjacent-range
  coalescing, distinct primitive residency and invalid/canceled worker candidates.
- `CrimsonModelingPresentationTests` and native `verify_modeling_presentation`:
  atmospheric integration, source tone references and actual tone/fog/UV pixels,
  odd resize and rejected-preflight recovery. Native context checks compare two
  simultaneous style packets, mixed dim/normal instance groups, source opacity,
  view independence and reset. CPU checks retain dim metadata and verify actual
  Host settings invalidation without constructing a GPU device.
- `crimson_engine_level_viewport_tools_core`: actual native Qt event delivery to
  Scene gizmos, transaction ownership, cancellation and exact one-command history.
- `crimson_engine_native_document_preview_core`: minimal-Qt typed preview layout,
  isolated Ragdoll simulation, clean document replacement, restored property
  ownership and expired callback lifetimes, without constructing a GPU device.
- Canonical borrowed-DX12 and actual Qt composition checks: shared queue,
  destination lifetime, resize/recreate and final composed pixels. A CPU/mock
  lifecycle test does not replace these native GPU checks.

This developer note describes the Engine full-profile integration. It does not
change the public Quader feature catalog or the published Quader manual.
