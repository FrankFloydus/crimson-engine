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
