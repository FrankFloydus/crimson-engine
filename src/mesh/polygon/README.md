<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Polygon Document Package

Native compatibility declarations for the canonical portable polygon core.

The implementation lives under
`src/sdk/core/modeling/internal/mesh/polygon` and is compiled into both the native and SDK targets
from one CMake source list. These headers temporarily keep existing native include paths stable;
guardrails require them to remain byte-identical to the canonical internal headers.

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

`quader_modeling_polygon` is built from the canonical portable `.cpp` files. No native polygon
implementation `.cpp` files belong in this directory.

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

Future implementation slices belong in the canonical portable package, not beside these
compatibility declarations.
