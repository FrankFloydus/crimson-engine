<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Document Internal Helpers

These files are private implementation helpers for the compiled document sources.

`quader_modeling_polygon` compiles document behavior from normal `.cpp` files. Do not add new
permanent `.inc` implementation shards.

Current helper boundaries:

- constants: private implementation constants shared by current document sources
- mesh internals: triangulation and generated-UV helpers declared in `quader_poly_document_mesh_internal.hpp`
- operation-owned types live in narrow private headers such as `quader_poly_document_knife_types.hpp`, `quader_poly_document_bridge_types.hpp`, `quader_poly_document_bevel_types.hpp`, and `quader_poly_document_hull_types.hpp`
- topology backend: `quader_poly_document_topology_backend.hpp` declares backend-neutral document topology query and mutation helpers for production sources
- OpenMesh helpers: `quader_poly_document_openmesh_helpers.hpp` keeps OpenMesh-named compatibility shims for focused backend tests
- narrow helper headers cover topology/validation, hull/plane, knife, UV, picking, and bridge/surface helpers
- operation helper behavior lives in compiled `quader_poly_document_*_helpers.cpp` sources. Keep OpenMesh implementation types in `quader_poly_document_openmesh_helpers.cpp`.

Document overlay APIs emit primitive geometry only. Overlay styling, layer policy, render state, and
projection belong in `src/editor/projection`, `src/editor/modeling/services`, and `src/render/**`;
do not grow document presentation rules here.
