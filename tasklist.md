<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader task list

## Serious open defects

Reviewed 2026-09-05. Keep only verified serious open defects; remove an item after its fix and
regression are verified. Durable invariants belong in [memory.md](memory.md) or the owner guide.
This is a broad source audit, not a proof of every line. Optional features and style work are
excluded from this defect section.

No verified serious open findings remained from that 2026-09-05 audit. Subsequent
verified findings follow.

- [ ] **Bulk component selection, dragging and commit still stall on dense scenes**
  (verified 2026-09-28). The original user capture established expensive modeling,
  scene synchronization and viewport preparation; evidence remains in
  `build/tracy-user-capture-10/findings.md`. Several causes are now addressed:
  preview eligibility is per record and checks only primitive-used material slots,
  preserving eligible opaque families beside transparent, reflected or static
  fallback records. Immutable coordinate, attribute and index streams retain their
  topology and owners; coarse GPU batches now combine compatible distinct meshes
  without requiring identical deformed geometry. Selection overlays retain compatible
  geometry, and rejected mixed submissions preserve the accepted frame.
  The [full native replay](tests/dense_component_frame_runtime_tests.cpp),
  `ClonedProjectFullEditorMixedComponentWorkflow`, uses the complete cloned project,
  all-object vertex/edge/face selections, visible editor panels and native pointer
  input through repeated edits, final release updates, commit, undo/redo and navigation.
  Its current results still show substantial dragging, commit and selection stalls;
  implementing the retained paths has not completed this defect. The visible-selection
  replay records no accepted GPU visibility witnesses: conservative uncertainty still
  requires CPU visibility tests. Current replay evidence is in
  `build/component11/after.log`, `build/component11/comparison.json` and
  `build/component11/findings.md`. The final comparison requires all measured drag
  samples to change both modeling and render publication, and matching native inputs,
  selection and geometry fingerprints across the baseline and final executable.
  Navigation-release/first-hover spikes also remain visible in that comparison.
  Remaining work includes large edit preparation/publication, final commit work and
  visibility/selection queries. Owners:
  [native preview projection](src/editor/modeling/services/modeling_sdk_projection_adapter.cpp),
  [mesh position projection](src/editor/projection/mesh_projection_service.cpp),
  [mixed preview submission](src/render/tool_preview_renderable_cache.cpp),
  [retained surface batches](src/render/editable_mesh_surface_cache.cpp),
  [overlay publication](src/render/overlay/overlay_renderable_cache.cpp) and
  [GPU selection witnesses](src/render/component_selection_renderer.cpp).
  Acceptance: the same-scene full native replay sustains interactive drag cadence and
  bounds selection/start/commit stalls across distinct geometry, fallback materials
  and repeated mixed edits, while preserving canonical geometry, visible overlays,
  rejection, commit/cancel and undo/redo. Report total frames, individual phases,
  actual fallback/GPU-selection decisions and resource/upload counts from matching
  final binaries. Retain [projection parity](tests/uv_native_sampling_tests.cpp),
  [component persistence](tests/component_transform_persistence_tests.cpp),
  [rendered stream and mixed-lane parity](tests/render_normal_mapping_runtime_tests.cpp),
  [selection depth policy](tests/component_selection_depth_tests.cpp) and
  [GPU selection readback](tests/component_selection_runtime_tests.cpp) coverage.

## Future development

- [ ] [Unreal and Unity bridges](UNREAL_UNITY_BRIDGES.md) — development brief requested
  2026-09-07, covering shared reader reuse, light mapping, attenuation, exposure, source access
  and acceptance criteria. Implementation has not started; target engine versions and Unity
  pipelines remain to be selected.
