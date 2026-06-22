<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Absorbed Modeling Code

The former copied SDK code now belongs to the native Quader app. These modules are app-owned
source, not a separate package boundary.

## App Modules

- `src/modeling/polygon`: app-owned polygon document model, topology operations, mesh compilation, generated UV0, explicit loop UV mutation, and picking helpers.
- `src/modeling`: app-owned modeling session/runtime policy over polygon documents, including active targets, hover identity, pick planning, rectangle selection, modal drag routing, viewport navigation math, view-cube policy, transform gizmo policy, and overlay-presentation policy.
- `src/modeling/operations`: modeling runtime operations such as bridge, thicken, slice quad, edge bevel, material assignment, duplicate, and selection edits.
- `src/modeling/interactions`: low-level modeling tool state/math and tool runtime helpers.
- `src/geometry`: reusable geometry predicates and projection math with private implementation backends.
- `src/io/qdr`: QDR document/session DTO conversion and serialization helpers.
- `src/editor/metadata`: descriptor-only host metadata for tools, operations, shortcuts, gestures, overlays, materials, preferences, UI placement, and interaction targets.
Shared default material texture sources live under `resources/modeling_assets`. Material behavior and
generated UV0 policy stay native/app-owned.

Third-party code lives under `thirdparty` with matching license files. Dependencies, including vendored GTE Mathematics headers, must remain compatible with proprietary/non-free engine adapters.

## Build

Configure and build from the app root:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug --output-on-failure
```

The modeling-domain CMake targets are:

- `quader_geometry`
- `quader_modeling_polygon`
- `quader_modeling_runtime`
- `quader_qdr_document_io`
- `quader_qdr_runtime_io`
- `quader_editor_metadata`
- `qdr_modeling_domain_smoke`

`quader_modeling_polygon` can use OpenMesh-backed topology operations when
`QUADER_USE_OPENMESH_TOPOLOGY=ON` is set, which is the default. Builds should provide a local
OpenMesh checkout with `-DQUADER_OPENMESH_SOURCE_DIR=<path>` or place the pinned source at
`thirdparty/openmesh/openmesh-5f016723f9f183770cffe5787a6f9f628e2914d8`. CMake only downloads
OpenMesh when `-DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=ON` is explicitly set.

Third-party dependency versions and license evidence are tracked in `thirdparty/THIRD_PARTY_MANIFEST.md`.

## Boundaries

Low-level modeling code must stay engine-clean and UI-clean. UI/editor intent routes through app
services and command APIs; polygon/runtime internals remain implementation details inside `src/`.
