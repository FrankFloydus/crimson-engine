<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Polygon Document Package

Canonical polygon document and operation implementation, compiled by both
`quader_modeling_polygon` and `quader_modeling_sdk_polygon`. The `quader_poly` declarations live
under `neutral/include/quader/modeling/neutral/polygon`; the public SDK facade uses
`quader::modeling`.

Owns topology and stable component IDs, material slots, generated/explicit UVs and texture
transforms, document snapshots/patches, operations, and document-local picking geometry.

Geometry edits preserve exact texture mapping where the resulting topology can represent it.
When it cannot, retain the available boundary/corner UVs or use finite inherited mapping;
an interpolation mismatch alone must not reject valid geometry. Connectivity, geometry and
invalid-data failures remain errors. Operations whose purpose is editing UVs still validate
the requested UV result strictly.

Polygon triangulation prefers complete 3D boundary coverage over partial axis
projections. Folds that collapse in those projections use an anchored,
normalized Newell-plane projection; an unchecked triangle fan is not a fallback.
Projected straight corners that bend in 3D are restored by splitting the exposed
triangle boundary. Only straight 3D corners and exact retraced spurs may be skipped.
An incomplete legacy axis result remains a final rendering fallback for existing
malformed faces. Fill Hole and Combine Faces require the complete-boundary check,
so that fallback cannot authorize a new cap or merge. Combine Faces command
availability uses the same check.

## Internal Decomposition

Both targets use `QUADER_CANONICAL_POLYGON_SOURCES` in root `cmake/QuaderModeling.cmake`.
Helper declarations are under the neutral polygon `internal/` include directory, not beside
these implementation files.

Current owner slices:

- `quader_poly_document_model_api.cpp`: public value helpers, vector math, and edge factor helpers.
- `quader_poly_document_storage_api.cpp`: public document storage and lookup helpers.
- `quader_poly_document_liveness.cpp` and `document_validation.cpp`: live component identity and validation.
- `quader_poly_document_primitives.cpp`: primitive document construction.
- `quader_poly_document_mesh_api.cpp`: public face-normal and mesh compilation API.
- `quader_poly_document_mesh_internal.cpp`: private triangulation, generated-UV basis, and mesh/UV helpers.
- `quader_poly_document_face_api.cpp`: face perimeter helpers and public UV entry points.
- `quader_poly_document_selection_api.cpp`: selection conversion, containment, mutation, and selection center.
- `quader_poly_document_component_traversal.cpp`: component connectivity queries and typed loop direction.
- `quader_poly_document_bevel_corner_patch.cpp`: profile sampling and boundary-constrained bevel corner patches.
- `operations/`: one compiled owner file per public polygon operation.
- `quader_poly_document_picking_overlay.cpp`: document-local picking and primitive overlay/preview data.
- `quader_poly_document_*_helpers.cpp`: private topology, hull, OpenMesh, knife, UV, picking, bridge,
  bevel, dissolve, copy, and surface-operation helpers.

Keep backend-neutral topology query/mutation declarations in
`quader_poly_document_topology_backend.hpp`; OpenMesh implementation types stay in
`quader_poly_document_openmesh_helpers.cpp`. OpenMesh-named shims support focused backend tests.

Component loop queries live in `quader_poly_document_component_traversal.cpp` behind the neutral
`document_topology.hpp` surface. Each query builds an immutable incidence index from stable vertex
IDs and face corners. Edge loops use continuation relations, edge rings connect opposite quad
edges, and face bands connect quad-axis nodes. Vertex loops return unique endpoints in traversal
order. Geometry coordinates do not choose branches. Picked-face context chooses deterministic
face order without limiting the strip; boundaries, non-quads, malformed identities/corners and
nonmanifold crossings stop face traversal. Typed direction uses the selected active component,
then an unambiguous already-selected loop or band, then the validated pointer hint.

`QdrDocumentLoopSelectionQueryTests` covers component behavior and compatibility.
`QdrDocumentOperationComponentTraversalTests` separately checks authored component sets under
coordinate collapse, sparse ID remapping, storage permutation, corner rotation and winding
changes, plus repeated direction recovery with unrelated selected components.

The graph implementation was authored from Quader's interfaces and behavioral fixtures without
consulting the preceding traversal implementation or Blender source. Earlier reference research
and the preceding implementation remain in Git history; this engineering separation does not
establish a formal clean-room or legal clearance.

Edge dissolve captures vertex eligibility from original authored topology in
`operations/dissolve_edges_operation.cpp`. Edges connected through degree-two vertices form
seam components; selected components own all their continuations. Terminal nominations use
the complete selected batch and original triangle-pair policy. A multi-seam terminal remains
protected when an untouched face corner uses neither neighboring seam edge. Closed degree-two
circuits have no terminal. Face joining, boundary UV transfer, normal-mode donors and final
endpoint cleanup remain with their existing owners.

The eligibility query was independently authored from Quader's interface and behavioral
requirements without consulting the preceding query or external editor implementations.
This statement covers that query only; it does not establish formal clean-room or legal clearance.
`QdrDocumentOperationDissolveEdgesTests` protects the behavior, with SDK/native tests covering
publication and history. Preserve original triangle corners and unrelated junctions; do not
replace the operation's cleanup with generic UV-redundancy filtering.

Edge bevel keeps support limits, retained faces, strip construction, UV/normal transfer and
publication in `operations/bevel_edges_operation.cpp`. Its corner helper receives shared boundary
identities and stages a patch in the disposable candidate. Multi-edge corner samples retain
distinct identities even when a square profile puts them at the same position; positional welding
must not destroy the patch boundary before its coincident samples can be separated.

Consecutive selected edges crossing noncoplanar face bands share their cuts on the original
unselected support edge. Width measures travel along that edge before rebuilding strip profiles;
perpendicular face offsets would widen a strip as it crosses increasingly sloped bands. Each cut
respects its available support length, and opposing cuts must not cross. Separate offset endpoints
must not be closed with thin junction faces. Existing shared coplanar miters retain their positions.
At a three-edge terminal, both cuts use the same along-edge width rule and the profile remains on
the unselected cap face. Width feedback excludes provisional face limits when both offset
endpoints were replaced, but preserves constraints belonging to surviving offsets and miters.
The sequential-bevel fixtures in `QdrDocumentOperationBevelChainTests`
protect support alignment, transverse spacing through every band and terminal, quad-strip counts,
oriented closure and unchanged surrounding geometry. Native retained-overlay and Vulkan tests
cover the same two-step workflow through preview, acceptance, cancellation and history.

The replacement profile sampler blends a rational conic with its inward/outward polyline limits.
It spaces samples in normalized support-ray coordinates so a stretched miter keeps the same profile
parameters. Corner topology consists of concentric quad rings ending in a shared center or polygon.
A boundary-constrained harmonic solve places interior nodes; circular boundaries that pass a sphere
fit can lift the interior onto that sphere. If an inward profile folds the initial cells and its
projected boundary is star-shaped, a convex reference polygon supplies an orientation-checked radial
map. Bounded radial redistribution preserves the boundary while keeping its center cells apart.
The helper checks its generated cells before staging;
the owner then completes winding and attribute transfer before publication. A rejected corner
must leave the document and selection untouched.

This profile and corner implementation was authored from Quader's value interfaces and behavioral
tests without consulting the preceding implementation or external editor source. Integration review
removed the preceding machinery; earlier source research remains in Git history. This describes
engineering inputs, not a formal clean-room or legal determination. Existing
`QdrDocumentOperationBevelEdgesTests` retain their geometry, topology, UV and normal expectations;
`QdrDocumentOperationBevelPatchTests` independently checks rigid-transform and uniform-scale
equivariance, oriented closure, generated selection and nondegenerate rendering triangles.

Use normal `.cpp` files and narrow helper headers; do not add permanent `.inc` implementation
shards. Document overlay APIs emit primitive geometry only. Semantic host projection belongs in
`src/editor`; styling, layers, draw policy, and render resources belong in `src/render`.

`auto_merge_selected_vertices` welds component-selected vertices into the nearest unselected
target before clustering remaining selected vertices. It keeps existing survivor coordinates,
corner attributes, selection identity and loose vertices. The fixed native Move default is
0.001 local units; previews stay outside this operation. Unlike the explicit Merge by Distance
command, auto-merge never relocates the survivor to a centroid. Its topology and SDK receipt
contracts are covered by the auto-merge document-operation and SDK consumer suites.

Dedicated face extrusion uses the typed `FaceExtrudeOptions` contract and the shared cap/wall
builder in `quader_poly_document_surface_operations.cpp`. Its affine operation space owns both
the amount metric and geometric winding, including mirrored transforms. Region displacement
uses equal unit-face weights; Along Normals uses selected corner-angle weights with unit-length
vertex displacement. Region modes replace selected source faces with moved caps and boundary
walls, leaving an open source boundary open. Only abandoned vertices from the selected source
are removed; unrelated loose vertices remain authored. Individual keeps independent caps attached
to their original base edges. The options path validates authored connectivity,
identities and finite attributes atomically. Selected source/result caps must triangulate in an
anchored, normalized face metric; document location and uniform scale do not change this check.
Finite thin or tangent side walls remain authored faces, and unchanged unselected geometric
defects remain intact. Strict document diagnostics and the older vector/float routes retain
their behavior. Coverage lives in
`QdrDocumentOperationExtrudeFacesTests` and `ModelingSdkFaceExtrudeTests`.

Prepared face extrusion retains one immutable source, a zero-displacement topology template,
and the captured operation-space directions. Preparation validates source and connectivity;
each signed sample evaluates absolute positions and validates only its moved caps, wall crossings,
and position-dependent texture mapping. The template's IDs are stable while it remains live.
Zero evaluates to the source; publishing zero or retiring a generation requires a fresh preparation
above the observed allocation high-water. The public SDK evaluator returns both the candidate and
the complete source-relative receipt, leaving preview publication and history with the session.
