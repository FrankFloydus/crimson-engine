# Quader SDK

Shared engine-agnostic native libraries for Quader.

This repository is consumed as a Git submodule by engine-specific hosts:

- `qvertexworks/quader-editor` places this repo at `sdk` inside the Godot addon repository.
- `qvertexworks/quader-unity` places this repo at `sdk` inside the Unity plugin repository.
- Future engine adapters should follow the same pattern and keep adapter code outside this repo.

## Packages

- `packages/document`: portable `quader_poly` document model, topology operations, mesh compilation, generated UV0, explicit loop UV mutation, and picking helpers.
- `packages/editor-core`: portable editor/session command and query policy over `quader_poly`, including selection, active targets, hover identity, pick planning, rectangle selection, modal drag routing, viewport navigation math, view-cube policy, transform gizmo policy, and overlay-presentation policy.
- `packages/geometry`: reusable engine-agnostic geometry predicates and projection math with private implementation backends.
- `packages/io`: QDR document/session DTO conversion and serialization helpers.
- `packages/host-contract`: descriptor-only host metadata for tools, operations, shortcuts, gestures, overlays, materials, preferences, UI placement, and interaction targets.
- `packages/foundation`: package contract reserved for future extraction.

Shared default material texture sources live under `assets/materials/default`. Engine packages may copy/import these assets, but material behavior and generated UV0 policy stay shared/native.

Third-party code lives under `thirdparty` with matching license files. Dependencies, including vendored GTE Mathematics headers, must remain compatible with proprietary/non-free engine adapters.

## Build

Configure and build from a checkout root:

```powershell
cmake -S . -B build\sdk
cmake --build build\sdk --config Release
ctest --test-dir build\sdk --output-on-failure
```

The CMake targets are:

- `qdr_geometry`
- `qdr_document`
- `qdr_editor_core`
- `qdr_io_document`
- `qdr_io`
- `qdr_host_contract`
- `qdr_sdk_smoke`

Custom engines should consume the SDK directly with CMake `add_subdirectory`.
The Unity repository includes `samples/custom-engine-minimal` as a fresh
consumer fixture; it links `qdr_host_contract`, `qdr_editor_core`, and `qdr_io`
without the Unity bridge. From the Unity repository root:

```powershell
.\tools\test_custom_engine_consumer.ps1 -OpenMeshSourceDir C:\path\to\openmesh-5f016723f9f183770cffe5787a6f9f628e2914d8
```

`qdr_document` can use OpenMesh-backed topology operations when `QUADER_SDK_USE_OPENMESH_TOPOLOGY=ON` is set, which is the default. Release/package builds should provide a local OpenMesh checkout with `-DQUADER_SDK_OPENMESH_SOURCE_DIR=<path>` or place the pinned source at `thirdparty/openmesh/openmesh-5f016723f9f183770cffe5787a6f9f628e2914d8`. CMake only downloads OpenMesh when `-DQUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH=ON` is explicitly set for one-off development builds.

Third-party dependency versions and license evidence are tracked in `thirdparty/THIRD_PARTY_MANIFEST.md`.

## Boundaries

Shared code must stay engine-clean. Do not include Godot, Unity, Unreal, editor UI, or host engine headers from `packages`.

Adapters convert at the repository boundary into STL values, primitive IDs, and `quader_poly` / `quader_editor_core` value types. Unity's `qdr_unity` C ABI bridge belongs in `qvertexworks/quader-unity`; Godot GDExtension classes belong in `qvertexworks/quader-editor`.

When changing shared code from a consumer checkout, commit and push this submodule first, then commit the updated submodule SHA in each consuming repository.
