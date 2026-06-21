# Document Package

Canonical portable Quader document model.

Owns:

- objects and polygon meshes
- vertices, edges, faces, and stable component IDs
- material slots and material references
- generated UV metadata and explicit loop UV overrides
- texture-lock state
- document snapshots and patches

The current portable seed is copied from `src/poly/quader_poly/document.h` and keeps the `quader_poly` namespace/API for the first extraction pass. Rename/split into `qdr::*` packages only after the Godot build consumes the SDK package cleanly.

## Current Internal Decomposition

`qdr_document` is built from focused source files with explicit CMake entries. The old include-only implementation shards have been removed; shared declarations and helper types live in private headers under `src/internal`.

The intended ownership split is:

- `../quader_poly_document_model_api.cpp`: public value helpers, vector math, and edge factor helpers.
- `../quader_poly_document_storage_api.cpp`: public document storage and lookup helpers (`add_vertex`, `add_face`, `find_vertex`, `find_face`, and `document_edges`).
- `../quader_poly_document_primitives.cpp`: primitive document construction and primitive-only private helpers.
- `../quader_poly_document_mesh_api.cpp`: public face-normal and mesh compilation API.
- `../quader_poly_document_mesh_internal.cpp`: private triangulation, generated-UV basis, and mesh/UV helper boundary used by document sources.
- `../quader_poly_document_face_api.cpp`: face perimeter helpers and public UV entry points.
- `../quader_poly_document_selection_api.cpp`: selection conversion, containment, edge loops, and selection center.
- `../operations/`: one compiled owner file per public polygon operation; each file defines a concrete `PolyOperation` subclass and the stable public wrapper for that operation.
- `../quader_poly_document_picking_overlay.cpp`: document picking plus legacy document-owned overlay/preview entry points.
- `../quader_poly_document_topology_helpers.cpp`: private geometry conversion, selection, face/edge, perimeter, and validation helpers.
- `../quader_poly_document_hull_plane_helpers.cpp`: private hull reconstruction, plane, and vertex-merge helpers.
- `../quader_poly_document_openmesh_helpers.cpp`: private OpenMesh adapter implementation for backend-neutral topology query and merge behavior.
- `../quader_poly_document_knife_helpers.cpp`: private edge-loop, connect-edge, and knife helper behavior.
- `../quader_poly_document_uv_helpers.cpp`: private UV inheritance and UV operation helpers.
- `../quader_poly_document_picking_helpers.cpp`: private ray/point/segment picking math helpers.
- `../quader_poly_document_bridge_surface_helpers.cpp`: private bridge, bevel, dissolve, copy, and surface-operation helpers.
- `quader_poly_document_constants.h`: private implementation constants shared by current document translation units.
- `quader_poly_document_mesh_internal.h`: private mesh/UV helper declarations shared across document translation units.
- `quader_poly_document_*_types.h`: narrow private type declarations owned by their helper domain, including hull, knife, bridge, bevel, and UV support records.
- `quader_poly_document_topology_backend.h`: private backend-neutral topology query and mutation declarations used by production document sources.
- `quader_poly_document_openmesh_helpers.h`: private OpenMesh-named compatibility shims for focused backend tests.

Future slices should keep adding normal `.cpp` files plus narrow internal headers. Do not add new permanent `.inc` implementation shards.
