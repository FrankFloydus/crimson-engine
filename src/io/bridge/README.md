<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native bridge reader

`quader_bridge_reader` is the native, read-only import boundary for external editor
bridges. Its [C-compatible header](quader_bridge_reader.hpp) is self-contained and has no Qt,
Filament, Godot or native object dependencies. The target links the existing project
and QDR readers and the native modeling projection owners; it does not define a
second project format or modeling implementation.

Load `quader_bridge_get_api` and request `QUADER_BRIDGE_ABI_VERSION`. Check the
returned pointer, `abi_version` and `struct_size` before calling its table. Different
ABIs return null. Every fallible table function returns 1 for success or 0 for
failure and optionally writes a bounded UTF-8 error. Native exceptions stay in the
reader DLL.

ABI version 3 adds mesh collision eligibility and borrowed local triangle points.
Collision data uses the shared immutable asset generator, before detached
material/UV projection, and remains valid for the scene handle lifetime. Points
retain CCW winding, rendered primitive-span multiplicity and Godot's 0.0001-unit
coordinate snapping. Empty geometry has no points. Generation requires the saved
Generate Collisions flag, Mesh shape and Static Body; unsupported enabled
combinations are distinguished from disabled generation for a bounded consumer
diagnostic. Editor collision display, fill color and mesh visibility do not
control physics export.

ABI version 4 adds direct authored lights and effective camera exposure inputs.
`scene_view` exposes the light count, aperture, shutter duration in seconds and
ISO sensitivity. Scenes without camera settings use the native defaults of 16,
0.008 seconds and ISO 100. When camera exposure is disabled, these inputs are
1, 1.2 seconds and ISO 100, equivalent to an exposure multiplier of 1. Inactive
authored camera values remain stored in the native scene. `light_view` exposes stable scene-local entity identity,
name, type, affine world transform, effective saved visibility, linear RGB,
authored intensity, range, distance attenuation and cast-shadow state. Published
light type IDs are Directional = 0, Point = 1 and Spot = 2.

Light snapshots include hidden ordinary hierarchy members and exclude definition
storage, definition members and occurrence leaves, matching native direct-light
ownership. Directional and spot emission follows transformed local -Y. Light
values remain unconverted; consumers apply renderer intensity units, effective
camera exposure, color-space and axis conversions.
Light views validate finite light values and transforms only when requested;
consumers with light importing disabled do not need to consume these views or
interpret the camera settings. Canonical QDR validation remains unchanged.

The `scene_source_fingerprint` and `material_source_fingerprint` functions
introduced in ABI version 2 remain unchanged.
Their byte count and FNV-1a64 content hash describe the exact QDR/QMAT bytes
decoded by the canonical reader, not a later reread of a mutable pathname.
Embedded material fingerprints refer to their containing QDR. The corresponding
source path is borrowed from the handle and remains project-relative. A bridge
can compare these fingerprints with the post-commit records under
`.quader/bridge/` before publishing an automatic update. Ship the v4 reader and
its consumer together; requesting the earlier ABI returns null.

## Lifetime and threading

Project handles own the validated manifest and scene catalog. Opening a scene
reopens the manifest, verifies the project identity and selected scene UUID, reads
the binary QDR through its canonical reader, and builds detached immutable mesh
and light snapshots. Scene handles own their project filesystem and native document and remain
valid after the project handle is released. Texture blob handles are independent
of both.

Strings and arrays returned by views are borrowed until their owning handle is
released. Material views are loaded once per scene and remain stable across later
view or texture reads. Release allocations only with the matching table function;
null releases are supported. Keep the DLL loaded until all its handles are gone.
Serialize operations on a handle. Independent handles may run on separate threads.

## Geometry and material identity

The scene-owned occurrence projection enumerates ordinary meshes and concrete
instance placements, including hidden placements, without also emitting definition
storage geometry. Each mesh retains effective saved visibility, an affine
column-major world transform and local positions, normals and UVs. Singular
instance definitions that would omit placements fail explicitly.

The mesh projection owner applies authored face and occurrence texture mapping on
a detached payload. Source triangles retain counter-clockwise winding; consumers
adapt winding and tangent generation to their target engine. Surfaces are grouped
by source material identity. Unassigned surfaces use `QUADER_BRIDGE_NO_MATERIAL`.
The material descriptor carries the remaining shared UV transformation; consumers
can use the previously imported transformation when reusing an edited material.

Material identities require no QMAT or texture file reads. Project materials use
persistent project asset UUIDs; embedded materials use scene-local numeric IDs.
Consumers must namespace project resources by project UUID, and embedded resources
additionally by scene UUID. Only request descriptors and texture bytes for resources
that need importing. This permits material reuse when original source assets are
unavailable.

Descriptors use the canonical QMAT reader and validate recorded material identity.
Legacy scene references without a dependency path use the existing material catalog
as a lazy fallback. Standard material color factors are linear RGB; project albedo
textures are sRGB and PBR data textures are linear. Embedded material texture color
spaces are exposed as authored and may need explicit target-engine handling.

All texture file resolution goes through `ProjectFileSystem`, including absolute
embedded paths, which must resolve inside the project. Missing sources, unbacked
generated textures, empty files and files larger than 64 MiB fail when requested.
The reader returns encoded bytes and the file extension; image decoding remains
the importing engine's responsibility. It never writes source projects or assets.

Public behavior and deterministic export fixtures are covered by
[`BridgeReaderTests`](../../../tests/bridge_reader_tests.cpp).
