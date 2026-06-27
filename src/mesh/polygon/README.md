<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Polygon Document Package

Canonical Quader polygon document model and operations.

This package is app-owned native source under the `quader_modeling_polygon` target. It keeps the
legacy-compatible `quader_poly` namespace/API, but it is no longer an external SDK extraction
boundary.

Owns:

- objects and polygon mesh documents;
- vertices, edges, faces, and stable component IDs;
- material slots and material references;
- generated UV metadata and explicit loop UV overrides;
- texture-lock state;
- document snapshots and patches;
- deterministic polygon operations and topology helpers;
- document-local picking geometry and primitive overlay/preview data.

## Internal Decomposition

`quader_modeling_polygon` is built from focused `.cpp` files with explicit CMake entries. Shared
declarations and helper types live in private headers under `src/mesh/polygon/internal`.

Current owner slices:

- `quader_poly_document_model_api.cpp`: public value helpers, vector math, and edge factor helpers.
- `quader_poly_document_storage_api.cpp`: public document storage and lookup helpers.
- `quader_poly_document_primitives.cpp`: primitive document construction.
- `quader_poly_document_mesh_api.cpp`: public face-normal and mesh compilation API.
- `quader_poly_document_mesh_internal.cpp`: private triangulation, generated-UV basis, and mesh/UV helpers.
- `quader_poly_document_face_api.cpp`: face perimeter helpers and public UV entry points.
- `quader_poly_document_selection_api.cpp`: selection conversion, containment, edge loops, and selection center.
- `operations/`: one compiled owner file per public polygon operation.
- `quader_poly_document_picking_overlay.cpp`: document-local picking and primitive overlay/preview data.
- `quader_poly_document_*_helpers.cpp`: private topology, hull, OpenMesh, knife, UV, picking, bridge,
  bevel, dissolve, copy, and surface-operation helpers.

Future slices should keep adding normal `.cpp` files plus narrow internal headers. Do not add new
permanent `.inc` implementation shards.
