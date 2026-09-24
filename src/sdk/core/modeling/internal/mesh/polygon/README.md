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
- `quader_poly_document_selection_api.cpp`: selection conversion, containment, edge loops, and selection center.
- `operations/`: one compiled owner file per public polygon operation.
- `quader_poly_document_picking_overlay.cpp`: document-local picking and primitive overlay/preview data.
- `quader_poly_document_*_helpers.cpp`: private topology, hull, OpenMesh, knife, UV, picking, bridge,
  bevel, dissolve, copy, and surface-operation helpers.

Keep backend-neutral topology query/mutation declarations in
`quader_poly_document_topology_backend.hpp`; OpenMesh implementation types stay in
`quader_poly_document_openmesh_helpers.cpp`. OpenMesh-named shims support focused backend tests.

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
vertex displacement. Region modes retain fully open source regions; Individual keeps independent
caps attached to their original base edges. The options path validates authored connectivity,
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
