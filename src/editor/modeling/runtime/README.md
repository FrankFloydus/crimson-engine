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
