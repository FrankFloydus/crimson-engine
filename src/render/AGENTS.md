<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Render Package Guide

- Render extraction, Filament resources, material/texture binding, overlays, and viewport drawing
  belong here. Scene, assets, and modeling remain authoritative; render caches never mutate them.
- The shared default-material image preference arrives through the narrow
  `RenderProjectMaterialResolver::default_material_override()` snapshot. Apply it to
  meshes, instances and previews without changing scene asset references or consulting
  authored material resolution in restricted profiles. Its revision invalidates retained
  bindings only when the choice changes; unchanged frames perform no image reads.
  Preserve Solid's flat-color shader behavior. See `tests/public_release_render_tests.cpp`
  and the default-material cases in `tests/render_extraction_tests.cpp`.
- `RenderSourceStamp` gates extraction; `RenderSceneSnapshot` feeds `SceneRenderBridge` and caches.
  Put dirty/revision checks before topology hashes, geometry compilation, snapshots, or uploads.
  Snapshot-owned material bindings use exact authored keys and material catalog/project revisions.
  Geometry edits can reuse bindings across ordinary/instance regrouping, but texture/material edits
  and resolver/default-override changes must invalidate them. Keep compatibility grouping exact;
  material reuse does not authorize reusing geometry groups after a payload changes.
- Apply the [preview projection contract](../editor/modeling/runtime/README.md#live-preview-projection-contract)
  to dynamic meshes and every associated wire, selection, silhouette and depth stream. Stable
  topology or committed asset revisions do not imply stable preview positions.
- Snapshot spans need explicit lifetime: pin immutable payloads with their owners, or document and
  enforce same-frame borrowing. Filament asynchronous uploads retain owned bytes until completion;
  use `filament_buffer_descriptor_factory.*` instead of borrowing temporary vectors.
- Overlay order, colors, depth, widths, and points belong to overlay policy/style resolvers. Keep
  persistent and volatile overlay streams separate; do not restore UI wire fallbacks or raw
  Filament line/point shortcuts.
- Camera-only point classification retains geometry and uploads visibility flags only. Worker
  queries pin immutable geometry and private scratch; Filament calls stay on the render owner.
  Packed preview begin must resolve viewport-skipped points before positions move. Refresh clip
  uniforms on camera changes even when unrelated frame layers also change.
- Compatible component vertex layers share one ordered marker geometry across selected/unselected
  outline/fill passes. Selection colors, sizes and class occupy a separate appearance stream;
  geometry, visibility and preview position streams retain independent lifetimes. Preserve exact
  within-class order and use the generic path for incompatible size/depth partitions or membership.
  Reconcile source-face visibility BVHs per object with exact geometry comparisons, evict absent
  sources, and keep query keys independent of color while retaining marker extent and eligibility.
  Factory upload counters include generic rebuilds; use them with actual pixel parity tests rather
  than relying only on preview-specific upload counters.
- Compatible component SourceWire layers retain edge slots and GPU buffers across membership
  changes. Keep active indices in incoming draw order and update only dirty endpoint/color ranges;
  retain object and canonical endpoint identity without merging ambiguous occurrences. Bound capacity
  to four times the active population and release empty layers. Restore, rebind and reapply live
  preview positions when base membership changes. Bound fragmented upload command counts as well
  as buffer capacity: consolidate large dirty-range sets and preserve retained positions in unchanged
  gaps. Per-edge uploads can overflow Filament's command stream during a dense commit.
  The generic builder remains the fallback for
  incompatible widths, sources or occurrence identities. Slot and pixel/preview contracts live in
  `tests/overlay_edit_wire_retention_tests.cpp` and `tests/selection_overlay_runtime_tests.cpp`.
- Dynamic preview preparation validates every group before uploads. Parallel work owns candidate
  CPU streams and upload descriptors; publish serially only after the whole batch succeeds. Reuse
  unchanged groups and preserve canonical normals, UVs, indices and bounds.
  Consume immutable effective geometry/position streams through their snapshot pins; the baseline
  mesh alone may not contain preview attributes or triangulation. Unchanged position proofs include
  payload ownership as well as source, layout and revision.
  Equivalent inputs may share immutable packed CPU bytes after exact geometry and effective-position
  equality. Each destination retains its own validated single-use update and asynchronous descriptor
  lifetime; changed normals, UVs, carriers, indices, partitions or positions invalidate reuse.
- `EditableMeshSurfaceCache` retains coarse batches of different ordinary meshes across preview,
  commit and cancel. Each enrolled source has one effective presentation: its accepted override or
  the current committed snapshot. Exclude only successfully covered sources from ordinary caches;
  unsupported records keep the legacy preview path alongside retained peers. Prepare every lane
  before changing coverage, and preserve the accepted display on candidate failure. Scene sync and
  later instance suppression must use the same filtered records so covered sources cannot reappear.
  Keep source geometry, positions and material pins immutable; commit/cancel changes effective
  inputs without promoting render state into document truth. Keep occurrence transforms relative
  to a retained chunk anchor with bounded placement extent; Filament applies the anchor through its
  camera-relative entity transform. Reanchoring must not recreate compatible geometry buffers.
  Share canonical CPU packing across chunks only after exact effective-stream comparison; bounded
  workers must join before any Filament resource creation or publication. Pixel, malformed-candidate and handoff
  coverage lives in `tests/render_normal_mapping_runtime_tests.cpp`; exclusion restoration,
  occurrence filtering and light-only changes live in `tests/render_texture_lifetime_tests.cpp`.
- Exact equivalent dynamic previews share one buffer-only GPU geometry owner with instanced
  rendered/shaded/depth chunks. Keep logical records for object identity, suppression and overlays.
  Stage geometry and exact-count instance updates before live publication; incompatible populations
  grow hidden candidates. Retire borrowers before their owner, bound inactive material-bearing
  resources, and drain ended generations under count/time budgets. Reflected or singular placements
  retain per-object rendering because Filament determines winding from the entity transform.
  Both batching paths inspect only materials used by drawn primitives. Alpha-textured or
  not-yet-classified materials retain ordinary per-object depth sorting; unsupported placements
  retain their established rendering path. Read opacity from the existing texture upload cache;
  eligibility checks must never decode images.
  Pixel, rejection, split/merge and retirement contracts live in
  `tests/render_normal_mapping_runtime_tests.cpp`.
- Visible component marquee queries may use retained GPU triangle-ID/depth snapshots only when
  geometry and camera identity match. Conservative pixel coverage supplies candidates for the
  existing exact CPU intersection rule; uncertain coverage, near-plane loss, overflow and stale
  readbacks must fall back. X-ray and exact edge rules retain their CPU path. Completed callbacks
  alone do not prove valid pixel contents. Keep asynchronous storage alive through shutdown and
  avoid warming during active navigation or editing. CPU-oracle and actual readback coverage live
  in `tests/component_selection_depth_tests.cpp` and `tests/component_selection_runtime_tests.cpp`.
- Vulkan object/group silhouettes use retained ordinary/active geometry batches in a separate
  selection scene and per-pane occupancy/depth targets. Camera movement updates projection, never
  CPU triangle unions or geometry uploads. Keep the two class boundaries independent, with active
  color winning equal-depth ribbons; preserve complementary visible/occluded depth passes.
  Mask targets include a raster-width/feather guardband. Retire pane views before their shared
  mask scene and materials. Source-replacing tool previews retain the existing projected CPU path.
  Primary selection has its own persisted pink color and logical line width, used only when more
  than one semantic element is selected in the current Object/Group/Vertex/Edge/Face mode. Count
  explicit objects/groups or selected components, before hierarchy/instance or primitive expansion;
  singleton selections use ordinary appearance. Resolve both classes
  independently, include active width in retained style keys and size mask guards for the wider
  class. Instance-fill color changes patch uniforms and stencil eligibility without rebuilding
  geometry; unchanged styles submit no updates. Component primaries, light markers and group cages
  consume the same active style roles instead of deriving a brighter ordinary-selection color.
  The CPU fallback still shares ribbons between depth passes and conservatively culls occurrences
  using resolved width, cap and feather extent. Pixel and camera-retention regressions live in
  `tests/selection_overlay_runtime_tests.cpp` and `tests/selected_mesh_outline_mask_cache_tests.cpp`.
- Vulkan face tints share the stencil policy in `overlay/overlay_face_fill_composition.*`:
  coverage chooses hover/removal over active over ordinary selection, then consumes one
  authored-alpha tint per pixel. Zero-alpha fills neither claim nor consume coverage.
  Never write scene depth from these masks/fills. Reset the
  reserved stencil bits for each pane/pass, including retained offscreen targets; preserve
  component outlines and real occlusion. The pinned SDK cannot attach custom-target stencil
  on OpenGL, so non-Vulkan backends retain the previous fill path. Native and offscreen pixel
  regressions live in `tests/selection_overlay_runtime_tests.cpp`.
- Collision display has an independent retained snapshot/cache; it follows mesh collision metadata,
  visibility, and precise tool-preview suppression. Selection and camera changes must not derive
  or upload collision geometry; tint changes update uniforms only.
  Godot editor parity uses deduplicated triangle wires, selected/unselected opacity factors
  of 1.6/0.4, and a backface-culled fill at 0.0625 times the configured opacity.
- `RenderSystem` owns surface records, Filament swapchains and per-surface renderers. Production
  Vulkan scene panes borrow Qt's device/graphics queue through `RenderVulkanSharedContext`;
  `RenderVulkanCompositionPlatform` owns their retained offscreen color/depth images and GPU copy.
  Qt owns the window swapchain and lends only its current-frame destination image. Copy the current
  visible crop in that Qt frame, returning the destination in shader-read layout. No CPU pixel
  readback or independently presented scene HWND belongs in this path.
- Shared-queue handoff has two obligations: drain Filament's CPU command producer before Qt or the
  copy submits to the same queue, and consume Filament's finished-drawing semaphore before copying.
  `RenderSharedQueueSubmissionState` tracks pending work and nested producer batches; ending an
  outer batch must mark pending again after a nested paint. Unchanged, closed batches add no drain.
  Retire images only after both Filament and external-copy GPU use completes. The borrowed Qt device
  must outlive all these resources and the Engine; ordinary handoff is not a GPU-idle wait.
- Native preview/standalone surfaces retain their platform swapchains, requested VSync and
  backend-confirmed modes. Qt's composition adapter reports the composed window's resolved mode
  to render. Retire pending GPU work before replacement; backend callback publication stays
  thread-safe. Previews honor `needs_initial_frame()` after recreation even when content is unchanged.
- Retained offscreen or native viewport backing may exceed its visible pane. Keep camera/input and selection
  readback dimensions visible-sized; rasterize the main/overlay views over the backing with the
  measured crop projection. View material global 1 carries visible-to-backing NDC scale/translation;
  global 2 carries the visible crop in physical pixels for visible-sized outline masks. Their
  unset defaults preserve standalone views. Extend orthographic grid coverage over hidden backing
  pixels without changing semantic grid spacing or line widths. Retain semantic bloom options
  and compensate its vertical buffer resolution for backing height to preserve visible blur scale.
- Bulk resource creation and retirement, including mesh, instance and wire teardown, must submit
  bounded command batches before `beginFrame`; large scenes can exhaust Filament's command arena
  even during shutdown.
  Detach the complete retiring set of renderables before releasing their materials, avoiding
  repeated material-lifetime validation scans. Ordinary proxy retirement removes renderable
  components immediately, then drains material/geometry owners under count and elapsed-time budgets,
  including unchanged syncs while a backlog exists. Otherwise unchanged synchronization submits nothing.
  See `SelectedObjectOverlayCache` and the
  6,400-mesh regression in `tests/selection_overlay_runtime_tests.cpp`.
  Vulkan selected face fills share indexed buffers across their four composition passes and
  combine meshes only within the same active/ordinary class. Color edits update uniforms;
  camera moves never rebuild batches. Retain reflected winding and local origins for precision.
  Non-Vulkan backends preserve per-mesh ordering because they lack the stencil composition path.
- Geometry-only instance changes may retain compatible entities, materials and instance buffers.
  Match exact occurrence order, materials, passes, topology and capacity; prepare the entire rebind
  batch before changing a live proxy. Incompatible groups use normal replacement. See
  `tests/render_texture_lifetime_tests.cpp` for retained identity and bulk retirement coverage.
- When compatible geometry populations merge again, retained instance slots must not preserve
  excess chunks from earlier splits. Rebuild through the existing planner when the current exact
  keys require fewer chunks; keep compatible full-capacity layouts retained. Planner regressions
  in `tests/render_extraction_tests.cpp` cover repeated split/restore cycles.
- Native-surface VSync requires the maintained Filament patch and matching headers/libraries.
  Composed-window VSync belongs to Qt. Dependency upgrades must preserve provenance checks and
  test actual presentation modes for both routes, not only timer intervals.

Verification pointers: `tests/render_extraction_tests.cpp`, `tests/render_source_stamp_tests.cpp`,
`tests/render_presentation_tests.cpp`, and `tests/render_source_policy_guardrail_tests.cpp`.
Use `tests/render_vsync_runtime_tests.cpp` for opt-in Vulkan presentation coverage and
`tools/boot_smoke.ps1` for window/GPU/pixel behavior; follow root build/test requirements.
