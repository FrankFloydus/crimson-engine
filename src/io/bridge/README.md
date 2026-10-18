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

ABI version 5 appends engine-ready light calibration to the direct-light views
introduced in version 4. `light_view` retains stable scene-local entity identity,
name, type, affine world transform, effective saved visibility, linear RGB,
authored intensity, range, distance attenuation and cast-shadow state. Published
light type IDs remain Directional = 0, Point = 1 and Spot = 2.

The appended fields are `preview_energy`, `illuminance_lux`, and
`luminous_intensity_candela`, resolved by the scene-owned
[`light_engine_values`](../../scene/light_properties.hpp) helper. For authored
intensity I, nonnegative preview energy G is I / 16. Directional lights provide
illuminance `pi * 38400 * G` lux; Point and Spot provide the same numerical value
in candela. The unused physical field is zero. The helper computes in double
precision and saturates exported values to finite float range. I = 1, 16, and 32
therefore produce preview energy 0.0625, 1, and 2; I = 16 produces approximately
120637.158 lux or candela. Existing saved intensity numbers remain unchanged;
legacy display-unit metadata does not select a different calibration.

`scene_view` still exposes the light count and three camera inputs, now always
the fixed preview reference: aperture 16, shutter 1/125 second, ISO 100. Its
exposure multiplier is 1/38400. Editing the source camera or disabling authored
camera exposure never changes these fields or the resolved light values.
Consumers use `preview_energy` directly in a nonphysical engine, or the physical
field appropriate to the light shape in a physical engine. They must not add
another camera/exposure conversion to these resolved values.

Light snapshots include hidden ordinary hierarchy members and exclude definition
storage, definition members and occurrence leaves, matching native direct-light
ownership. Directional and spot emission follows transformed local -Y. Consumers
still apply their engine's color-space and axis conversions. Light views validate
finite authored light values and transforms only when requested; consumers with
light importing disabled do not need to consume these views. Canonical QDR
validation remains unchanged.

The `scene_source_fingerprint` and `material_source_fingerprint` functions
introduced in ABI version 2 remain unchanged.
Their byte count and FNV-1a64 content hash describe the exact QDR/QMAT bytes
decoded by the canonical reader, not a later reread of a mutable pathname.
Embedded material fingerprints refer to their containing QDR. The corresponding
source path is borrowed from the handle and remains project-relative. A bridge
can compare these fingerprints with the post-commit records under
`.quader/bridge/` before publishing an automatic update. Ship the v6 reader and
its consumer together; requesting the earlier ABI returns null.

ABI version 6 appends `diffuse_model` to `QuaderBridgeMaterialView`. Both project
and embedded materials publish their authored choice: Burley = 0, Lambert = 1.
New materials and older files without a saved choice resolve to Burley. Other
values are rejected. The field is part of the immutable, lazily loaded material
descriptor and requires no texture reads. Consumers map it to their equivalent
diffuse model when importing or explicitly refreshing a material; their existing
material-edit preservation policy continues to decide when to request it.

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

The reader uses the canonical persisted-geometry decoder. Nonmanifold topology,
including an edge shared by more than two faces, does not reject the scene or
modify its authored records. Ship a rebuilt native reader for this behavior;
replacing the editor executable alone does not update a staged reader DLL.

The scene-owned occurrence projection enumerates ordinary meshes and concrete
instance placements, including hidden placements, without also emitting definition
storage geometry. Each mesh retains effective saved visibility, an affine
column-major world transform and local positions, normals and UVs. Singular
instance definitions that would omit placements fail explicitly.

QDR schema 17 stores affine parenting compensation separately from editable TRS. The reader's
existing mesh and light affine matrices include the complete compensated hierarchy, including
shear and reflections, without baking geometry or changing the ABI. Rebuild the reader with the
native codec when shipping schema 17 support; older reader builds reject the newer scene schema.

The mesh projection owner applies authored face and occurrence texture mapping on
a detached payload. Source triangles retain counter-clockwise winding; consumers
adapt winding and tangent generation to their target engine. Surfaces are grouped
by effective source material identity. Manifest deletion tombstones resolve to the
reserved Default Material UUID before material indexing and surface grouping.
Unassigned surfaces use `QUADER_BRIDGE_NO_MATERIAL`.
The material descriptor carries the remaining shared UV transformation; consumers
can use the previously imported transformation when reusing an edited material.

Material identities require no QMAT or texture file reads. Project materials use
persistent project asset UUIDs; embedded materials use scene-local numeric IDs.
Consumers must namespace project resources by project UUID, and embedded resources
additionally by scene UUID. Only request descriptors and texture bytes for resources
that need importing. This permits material reuse when original source assets are
unavailable.

Descriptors use the canonical QMAT reader and validate recorded material identity.
Legacy scene references without a dependency path and moved materials whose saved
path is missing or now contains a different UUID use the existing material catalog
as a lazy fallback, matching the exact effective UUID. Malformed sources and unsafe
paths still fail. Descriptors and fingerprints report the validated current QMAT
path and decoded bytes, including Default for deleted references; scene source
fingerprints continue to identify the unchanged decoded QDR. Standard material
color factors are linear RGB; project albedo
textures are sRGB and PBR data textures are linear. Embedded material texture color
spaces are exposed as authored and may need explicit target-engine handling.

All texture file resolution goes through `ProjectFileSystem`, including absolute
embedded paths, which must resolve inside the project. Missing sources, unbacked
generated textures, empty files and files larger than 64 MiB fail when requested.
The reader returns encoded bytes and the file extension; image decoding remains
the importing engine's responsibility. It never writes source projects or assets.

Public behavior and deterministic export fixtures are covered by
[`BridgeReaderTests`](../../../tests/bridge_reader_tests.cpp).

The maintained export fixture adds `Scenes/lighting.qdr` (three white I=16 lights,
range 12, falloff 2) and `Scenes/lighting_calibration.qdr` (all three shapes at
I=1/16/32, range 12, falloff 1). Their identities and resolved values are recorded
in `expected.json` under `light_entries` and `calibration_lights`. The existing
`SaveGodotAcceptanceFixture` path supports `QUADER_BRIDGE_MUTATION=lights_half`
to save I=8 through the native document owner, and `lights_camera` to change only
authored camera fields and disable camera exposure. These fixtures are disposable
verification projects, not installed plugin payload.
