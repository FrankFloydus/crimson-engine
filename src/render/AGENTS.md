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
- Dynamic preview preparation validates every group before uploads. Parallel work owns candidate
  CPU streams and upload descriptors; publish serially only after the whole batch succeeds. Reuse
  unchanged groups and preserve canonical normals, UVs, indices and bounds.
- Vulkan object/group silhouettes use retained ordinary/active geometry batches in a separate
  selection scene and per-pane occupancy/depth targets. Camera movement updates projection, never
  CPU triangle unions or geometry uploads. Keep the two class boundaries independent, with active
  color winning equal-depth ribbons; preserve complementary visible/occluded depth passes.
  Mask targets include a raster-width/feather guardband. Retire pane views before their shared
  mask scene and materials. Source-replacing tool previews retain the existing projected CPU path.
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
- `RenderSystem` owns native surface records, swapchains, per-surface renderers, requested VSync,
  and backend-confirmed presentation modes. Retire pending GPU work before swapchain replacement;
  callback publication must stay thread-safe. Preview surfaces honor `needs_initial_frame()` after
  recreation even when their content is unchanged.
- Bulk resource creation and retirement, including mesh, instance and wire teardown, must submit
  bounded command batches before `beginFrame`; large scenes can exhaust Filament's command arena
  even during shutdown.
  Detach the complete retiring set of renderables before releasing their materials, avoiding
  repeated material-lifetime validation scans. Keep unchanged synchronization free of submissions.
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
- VSync requires the maintained Filament patch and matching headers/libraries. Dependency upgrades
  must preserve provenance checks and test actual presentation modes, not only timer intervals.

Verification pointers: `tests/render_extraction_tests.cpp`, `tests/render_source_stamp_tests.cpp`,
`tests/render_presentation_tests.cpp`, and `tests/render_source_policy_guardrail_tests.cpp`.
Use `tests/render_vsync_runtime_tests.cpp` for opt-in Vulkan presentation coverage and
`tools/boot_smoke.ps1` for window/GPU/pixel behavior; follow root build/test requirements.
