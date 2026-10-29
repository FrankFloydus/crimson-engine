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
and Vertex-mode points, persist across empty-space hover, viewport leave, and camera
navigation (orbit, pan, zoom and fly). Preserve that candidate through the captured
navigation release; the navigation cursor must neither replace nor clear it. A
subsequent ordinary pointer hover over an eligible mesh replaces the candidate;
meshes with selected components remain additive. These transitions do not mutate
component selection or history. An empty target carried from object selection must
not keep a stale wire when a different candidate takes over.

Keep immediate component highlighting separate from the retained candidate. Blank
clicks and finished box/lasso selection clear the immediate highlight but preserve
the candidate. Treat navigation and camera-only suppression as transient spatial
hover loss, including interrupted Paint and traversal feedback. Apply that distinction
to every selection method and the remembered method beneath transform tools.
Tool/gizmo ownership and invalid selection context still retire the candidate and
immediate highlight while leaving selected components visible. Tool surface
acquisition can target eligible peers through the same pick universe; operation
validation must not reinstate a fixed-participant gate after a valid peer hit.

Candidate changes and clears must reach persistent and volatile retained submission
gates, including points. Preserve the existing wire colors, user settings, depth
classification and Wireframe suppression policy. Verify actual submitted wire and
point data and GPU output; a fresh semantic overlay alone cannot prove the visible
transition. Coverage lives in
[interaction tests](../../../../tests/viewport_interaction_controller_tests.cpp),
[Qt input tests](../../../../tests/qt_input_adapter_tests.cpp),
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

Knife publishes a world-space straight reference from the accepted seed to the first boundary
of its eligible face. Edge seeds use the face-plane perpendicular to that edge; other seeds use
the existing face directions. The smaller unsigned line angle (0–90 degrees) is independent of
the camera and does not change Shift constraints, snapping, accepted points or history. The
volatile source carries both the orange reference and angle context through retained submission;
the shared dimension HUD gates the angle and live-segment length with Show Dimensions. The
segment endpoints remain independent of reference availability, and length is placed beside
the current endpoint. Boundary validation accounts for world-space float rounding without
changing topology or snapping tolerances. Hover clear and stroke
teardown retire both. See the [Knife submission tests](../../../../tests/viewport_overlay_submission_coordinator_tests.cpp)
and [HUD tests](../../../../tests/qt_viewport_edge_length_hud_tests.cpp).

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

Bounds resize captures one common frame, its extents, the moving side and the opposite
anchor before starting the existing transform preview. Samples are absolute outward world
displacements from that capture. The constrained factor is `1 + displacement / extent`,
clamped to `[0.001, 10000]`; snapping resolves the moving coordinate in the captured frame.
Zero displacement remains neutral even for an off-grid boundary. Rotated, reflected and
parented mesh selections use the same common affine, preserving their relative placement.
The bounds-specific scene preview retains a necessary affine remainder through the existing
transform representation and history transaction; ordinary transform tools retain their own
representability policy. Each candidate is derived from the capture, including its parent
frame, so repeated samples never accumulate an affine remainder. Invalid candidates preserve
the last accepted preview. See the [bounds API regressions](../../../../tests/bounds_resize_api_tests.cpp).

The shared [bounds interaction](../../tools/bounds_resize_interaction.hpp) publishes an immutable
pane-local layout for drawing and picking. A pressed mesh handle does not start history or
settle another tool until its drag threshold is crossed. Selection, scene and document fences
retire stale captures. Primitive creation consumes the same six-side presentation through its
parametric owner. Accepted preview revisions must reach surfaces, wires, outlines, picking,
depth and dimensions together; passive panes project the accepted state without running input
lifecycle code. Unchanged frames reuse both the bounds query and retained overlay submission.
