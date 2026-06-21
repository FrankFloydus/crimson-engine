# Document Internal Helpers

These files are private implementation helpers for the compiled document sources.

`qdr_document` now compiles all document behavior from normal `.cpp` files. Do not add new permanent `.inc` implementation shards.

Current helper boundaries:

- constants: private implementation constants shared by current document sources
- mesh internals: triangulation and generated-UV helpers declared in `quader_poly_document_mesh_internal.h`
- operation-owned types live in narrow private headers such as `quader_poly_document_knife_types.h`, `quader_poly_document_bridge_types.h`, `quader_poly_document_bevel_types.h`, `quader_poly_document_hull_types.h`, and `quader_poly_document_uv_types.h`
- topology backend: `quader_poly_document_topology_backend.h` declares backend-neutral document topology query and mutation helpers for production sources
- OpenMesh helpers: `quader_poly_document_openmesh_helpers.h` keeps OpenMesh-named compatibility shims for focused backend tests
- narrow helper headers cover topology/validation, hull/plane, knife, UV, picking, and bridge/surface helpers
- operation helper behavior lives in compiled `quader_poly_document_*_helpers.cpp` sources. Keep OpenMesh implementation types in `quader_poly_document_openmesh_helpers.cpp`.

The overlay APIs remain legacy document-owned entry points because overlay styling belongs in editor-core. Keep any future overlay policy movement in editor-core rather than growing document presentation rules.
