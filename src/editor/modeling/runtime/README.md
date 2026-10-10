<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native Modeling Runtime Projections

This package works on ephemeral `quader_poly::Document` views projected from the portable core.
The local `quader_editor_core` namespace does not make it the SDK boundary.

- Owns native picking/hover, rectangle selection and pick cycling, selection summaries and semantic
  overlays, transform-drag/tool input state, and native command-to-operation mapping.
- `quader::modeling` owns durable modeling documents, component selection, operations, previews,
  and modeling history. Scene identity/hierarchy and ordered editor history stay with native owners;
  render owns overlay styling/GPU resources, and viewport owns workspace/navigation state.
- Production picking, overlay, bounds, and input helpers consume immutable snapshots or narrow
  `EditorRuntimeSelectionView` values. Do not retain/materialize full `EditorSession` instances as
  production selection projections; that compatibility surface serves local mutating algorithms/tests.

Use the matching `tests/modeling_domain/tool` and runtime suites for host interaction, and SDK
consumer/parity tests for portable behavior. Real window/GPU output follows the root smoke workflow.

## Component candidate presentation

Component hover and picking use eligible scene meshes rather than a fixed Edit
participant list. Retain the last eligible hovered mesh as a presentation candidate
without mutating SDK component selection or scene membership. Its component wire,
and Vertex-mode points, persist across empty-space hover and viewport leave. A new
eligible mesh replaces the candidate; meshes with selected components remain
additive. An empty target carried from object selection must not keep a stale wire
when a different candidate takes over.

Keep immediate component highlighting separate from the retained candidate. Blank
clicks and finished box/lasso selection clear the immediate highlight but preserve
the candidate. Gizmo ownership performs a hard clear of both while leaving selected
components visible. Tool surface acquisition can target eligible peers through the
same pick universe; operation validation must not reinstate a fixed-participant
gate after a valid peer hit.

Candidate changes and clears must reach persistent and volatile retained submission
gates, including points. Preserve the existing wire colors, user settings, depth
classification and Wireframe suppression policy. Verify actual submitted wire and
point data and GPU output; a fresh semantic overlay alone cannot prove the visible
transition. Coverage lives in
[interaction tests](../../../../tests/viewport_interaction_controller_tests.cpp),
[retained presentation tests](../../../../tests/selected_mesh_outline_presentation_tests.cpp)
and [GPU overlay tests](../../../../tests/selection_overlay_runtime_tests.cpp).

## Live preview projection contract

Before adding or changing an operation that displays temporary geometry, trace its candidate
through every retained consumer. Filled mesh geometry, component/source wires, selected edges
and faces, object silhouettes, depth masks, picking and bounds must describe the same accepted
candidate and occurrence transform. Keeping the committed asset unchanged during a drag is valid;
using that asset's revision as the only freshness key for a preview consumer is not.

- The modeling owner supplies a cheap projection revision/signature that changes whenever the
  visible candidate changes, even when topology, element IDs, selection and committed content
  revisions stay identical. The signature must cover preview removal and failure compensation too.
- Carry that owner signature through **every** outer frame/submission gate and inner snapshot/GPU
  cache that retains derived geometry. Invalidating a service's private cache is insufficient if
  an outer viewport stamp prevents the provider from being called. Audit persistent and volatile
  overlays separately, including the component wires used in Face/Edge/Vertex mode.
- Begin, accepted updates, zero displacement, commit, cancel, undo/redo, and compensated failures
  must either publish the matching projection or retire it. Failed preparation that preserves the
  last accepted candidate must preserve its display. Keep these transitions with their existing
  owner; do not patch them with tool-specific Qt invalidations, unconditional frame rebuilds or
  asset writes on every mouse move.
- A regression must pass the public owner signature and provider through the production retained
  submission coordinator. Warm its caches, change only preview positions several times with stable
  IDs and unchanged committed revisions, then assert **submitted** wire/selection coordinates match
  the candidate and stale coordinates disappear. Also verify cancel/commit/zero/failure cleanup and
  that repeated unchanged frames do not call providers, rebuild geometry or upload it again.

A direct `selection_overlay()` assertion can pass while the screen remains stale. Protect the
complete stamp → provider → retained submission chain, and add a GPU/pixel check where submission
data alone cannot establish the displayed result. Measure complete preview submission, including
retained overlays, when reporting viewport performance.

Regression examples: [submitted extrusion wires](../../../../tests/selected_mesh_outline_presentation_tests.cpp)
and [Vulkan outline pixels](../../../../tests/selection_overlay_runtime_tests.cpp).

Cut captures its Object, Face or Edge targets before plane placement. Its first surface hit
sets the support plane used by subsequent endpoint and line drags. The SDK projection adapter
prepares candidates from committed documents and retains the accepted result for surfaces,
picking and commit. Candidate revisions also invalidate selection topology and depth geometry.
Face scope clips only captured regions; Edge scope inserts intersection vertices without
splitting face interiors or applying discard settings. Cap is off by default and closes only
complete newly exposed planar loops when enabled; open chains and unrelated existing holes
remain open. Nested rings preserve holes through cap triangulation, while disconnected shells
retain separate caps; branching boundaries reject atomically. Cut hides ordinary selection overlays
and dimensions from activation without
changing selection. Accepted candidates show white retained edges, an orange cut contour and
red discarded edges. Cancellation retires that presentation and restores selection overlays.
Dragging is continuous unless Ctrl enables grid snapping; Shift straightens within the retained
support plane. See the
[scoped operation tests](../../../../tests/modeling_domain/operation/qdr_document_operation_plane_cut_tests.cpp)
and [retained submission tests](../../../../tests/tool_preview_submission_tests.cpp).

Ordinary component transforms retain SDK topology and publish immutable coordinate snapshots.
The native projection prepares authored-to-compiled mappings once, then publishes
`MeshGeometryProjection` position, attribute and index streams. When that pin is present,
`preview.mesh` supplies source metadata; its stored vertices are not the current candidate.
Consumers must use the effective stream spans and authoritative positions, including tangent,
UV, wire and depth preparation. Unchanged stream owners remain pinned across updates.

Keep full geometry materialization out of frame preparation. Explicit API export and commit may
materialize the accepted streams lazily; commit adopts that cached geometry only after the
accepted SDK content identity matches. Equivalent accepted families also retain a shared immutable
authored document after exact SDK content comparison, keeping local picking reuse without merging
independent SDK objects or their identity cursors. UV-preservation or topology/layout changes retain canonical
fallback. Rejection must preserve every accepted stream and revision, and no reusable position
buffer may be written while an accepted or older snapshot pins it. These contracts are covered by
[coordinate projection parity](../../../../tests/uv_native_sampling_tests.cpp) and
[component transform lifecycle tests](../../../../tests/component_transform_persistence_tests.cpp).
