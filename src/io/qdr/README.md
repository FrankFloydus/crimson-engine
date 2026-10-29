<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native QDR Scene Format

A native `.qdr` is a binary `QDRB` container for one scene: canonical entities, embedded geometry and
scene-local assets, scene layers, project-material references, and a persisted perspective camera
pose. `.qproj` stores project workspace metadata; reusable project materials are external `.qmat`
files. Portable SDK modeling-document QDR is a separate interchange contract.

## Owners and layout

- [qdr_binary_container.*](qdr_binary_container.hpp): little-endian header with persistent 128-bit
  scene UUID and 64-bit chunk directory. Entries carry offsets/sizes, schema/codec/critical flags,
  CRC32, and content hash. Validation covers arithmetic, bounds, overlap, limits, checksums, and hashes.
- [qdr_scene_binary_codec.*](qdr_scene_binary_codec.hpp) and [schema/qdr_scene.fbs](schema/qdr_scene.fbs):
  `QDRS` metadata plus one `QDRG` per unique geometry payload. FlatBuffers and domain references are
  validated before publication. Identical geometry is emitted once; large chunks use Zstandard only
  when worthwhile. A shared immutable backing buffer supports deferred geometry decode/compile.
- Authored polygon data uses SDK-owned packed/CSR `QGEO` inside the geometry wrapper; compiled mesh
  fallback data also lives in `QDRG`. Native code consumes the projection owner instead of duplicating
  topology conversion.
- Opt-in circular UV Peel fields persist in authored `QGEO` minor 4. Compiled-only payloads
  carry an optional sparse field table and per-vertex 1-based indices in `QDRG`; missing data
  means ordinary interpolation. The field is renderer-neutral and adds no mesh vertices.
- Save and Auto Save retain raw authored geometry independently of modeling diagnostics. Invalid
  loops, references, UV arrays and edge attributes round-trip without automatic cleanup. On load,
  safe display geometry is derived separately; it never replaces the stored authored records.
- [qdr_project_write_snapshot.*](qdr_project_write_snapshot.hpp) captures/pins export data before
  serialization. `QdrDocumentReader`/`QdrDocumentWriter` adapt files; `qdr_atomic_write.*` owns durable
  replacement. Project sessions pass their persistent scene UUID on write and consume
  `QdrProjectReadResult::scene_uuid` on open.
- Dependency records contain stable `ProjectAssetId`, kind, and project-relative path. Project
  sessions populate them from referenced materials and validate them against the project catalog.
  `ProjectAssetResolver`/`ProjectFileSystem` own resolution and containment.
- Material usage is a separate read-only projection: actual authored face slots, nonempty
  compiled primitives, and solid curves. It validates topology through the SDK and reuses
  slots for shared geometry chunks without compiling render geometry. Its isolated
  [schema-10 usage dialect](schema/qdr_scene_usage_schema10.fbs) is preserved from commit
  `42d4b430` and accepts only the retired metadata-10/geometry-2 wire format for this purpose.
  Usage completeness never establishes that a scene can be opened, rewritten, or safely
  inspected by the conservative deletion dependency reader.

## Compatibility that must survive

Current scene metadata schema is 21; schema versions and defaults are enforced in the codec.
Schema 21 stores optional host component/settings envelopes and declared native entity UUID
references in the same authored scene. Portable SDK geometry and container versions are unchanged.

Native Location uses the existing `TransformComponent.position` wire slot. Native
`SelectableComponent.selectable` is stored as the inverse of the existing
`LockComponent.locked` field. The separate `VisibilityComponent.selectable` eligibility
gate is preserved independently: all four historical flag combinations keep their
selection behavior. These native vocabulary changes do not change schema versions or
serialized field meanings. The write-only JSON inspection dump uses `location` and the
positive top-level `selectable` name.

Container 1.2 stores a nonzero minimum product release in the 64-bit header word at byte 56.
Container 1.0/1.1 retain a zero reserved word and are treated as Internal Full releases.
Low-level container inspection is unrestricted. Authoring callers pass their executable release
to `QdrDocumentReader`, which checks the verified requirement before metadata decoding or scene
hydration. `read_project_release_requirement` provides the same container validation without
hydration for workspace preflight. Write snapshots preserve their explicit release requirement;
the session stamps the writer's release without lowering an existing requirement.

| Version | Durable data/defaults |
| --- | --- |
| 1-3 | Legacy CSR topology is reader-only; current writes use SDK topology |
| 4 | Scene layers |
| 5 | Optional persisted viewport camera pose |
| 6 | Authored modeling local-frame version; earlier schemas use the legacy upgrade route |
| 7 | Optional entity affine texture sampling; earlier schemas use identity |
| 8 | Curve assets/components with stable authored controls and allocation cursors |
| 9 | Curve solidification; schema 8 defaults to disabled, resolution 8, radius 0.1 |
| 10 | Light distance attenuation; schemas 1-9 default to inverse-square attenuation (2) |
| 11 | Camera exposure toggle; schemas 1-10 default to enabled |
| 12 | Light unit, color mode, remembered Kelvin, and stable source preset; schemas 1-11 default to Simple/Color/6500 K/Custom |
| 13 | AgX tone mapping, appended as value 5; historical tone-mapping IDs 0-4 remain readable and round-trip unchanged |
| 14 | Embedded material diffuse model (Burley or Lambert); missing fields use Burley |
| 15 | Scene fog settings; older schemas and an absent fog table default to disabled fog |
| 16 | Document-owned scene settings and an explicit ordered forest of parentless entities |
| 17 | Affine parenting offset before editable TRS; older or absent fields use identity |
| 18 | Per-mesh Wireframe on Shaded participation; older or absent fields default to enabled |
| 19 | Optional per-mesh shaded-wire RGB override; older or absent fields inherit the global viewport color |
| 20 | Optional compiled circular UV interpolation descriptors and one-based per-vertex references; absent fields use ordinary interpolation |

Schema 18 appends a mesh display flag. The pane-local master remains transient viewport state;
only the object's participation is saved. Readers predating schema 18 reject new files so a later
save cannot silently re-enable an object's disabled wireframe display.

Schema 19 adds optional normalized sRGB color data to each mesh. Absence means inherit;
explicit black remains a distinct authored override. All RGB channels must be finite and within
[0, 1]. Full and dependency-only readers reject invalid overrides before publication. Override
alpha is deliberately not stored; the shared viewport color owns opacity.

Schema 20 protects compiled-only circular texture flow from older readers that would discard
the added fields on save. Geometry chunks retain schema 1. Full hydration, dependency reads,
and material-usage reads reject nonempty circular descriptor/index vectors labeled with an
earlier scene schema, nonfinite or degenerate frames, mismatched index counts, and missing descriptors.
These checks inspect compiled attributes without decoding authored topology or compiling a mesh.

The FlatBuffers defaults remain intensity 1 and ACES tone mapping, even when new native
objects use different creation defaults. Saved camera values also remain unchanged. Obsolete
ACES Legacy and Display Range settings retain their stored identities; their current viewport
presentation falls back to ACES. Readers predating schema 13 reject new scene files rather
than interpreting the appended AgX mode incorrectly. Schema 14 appends the material diffuse
model with Burley as the wire default; readers predating schema 14 reject new files rather
than silently dropping an authored Lambert selection. Unknown diffuse-model values are rejected.

Schema 15 appends an optional fog table to the historical scene root settings. It preserves all authored
distance, height, density, opacity, linear color, environment-color and sunlight-scattering
settings. A cutoff distance of zero means unlimited; nonpositive scattering size disables
sunlight scattering. Numeric fog values must be finite, with nonnegative distances, density,
height falloff and color, and opacity in [0, 1]. Fog height may be negative. Readers predating
schema 15 reject new files instead of silently losing fog on a later save.

Schema 16 appends scene settings and ordered root entity IDs directly to `ScenePayload`.
Both fields are required by domain validation, including an explicit empty root vector for
empty scenes. Every parentless entity appears exactly once; nested or missing IDs are rejected.
New writers omit the historical entity root/settings fields. Schemas 1-15 extract settings
from the sole legacy wrapper and remove it, promoting children in order while preserving world
placement and inherited visibility/selectability/locking. Roots carrying other authored components, malformed
hierarchies, and nonfinite transforms are rejected before publication. Affine compensation
preserves inherited shear when a legacy wrapper is removed. Older scenes without a wrapper
retain their existing forest and receive defaults.
Full and dependency-only readers share this conversion and root-order validation.

Schema 17 appends `TransformComponent.parenting_offset` as 16 column-major floats. Placement is
`parent_world * parenting_offset * editable_TRS`; the offset remains valid on parentless entities.
The matrix must be finite and affine with homogeneous row `(0, 0, 0, 1)`. Singular affine offsets
are valid stored placements; inversion is an editing-operation concern. Missing offsets and all
schemas before 17 use identity. Both full hydration and dependency/material-usage projections
validate the field before publishing results. Earlier readers reject schema 17 before hydration,
so they cannot silently load compensated children in the wrong location or discard the offset.

The native `qdr_migrate` tool validates a complete batch without writing by default:
`qdr_migrate <scene.qdr> [...]`. Add `--apply --backup-directory <new-directory>`
to retain verified originals and publish validated schema-20 files with per-file atomic writes
and rollback on ordinary failures. Its isolated converter also handles the retired
metadata-10/geometry-2 dialect; that dialect is not accepted by the normal scene reader.
Persistent backups provide recovery after interruption; the batch is not crash-atomic.
Migration preserves the decoded minimum product release, including Internal Full for legacy
containers; a format-only conversion never promotes or lowers the release requirement.

Light metadata does not replace authored RGB or canonical intensity. Reading historical scenes
preserves those values and their falloff, including values outside the current Inspector slider
spans. The FlatBuffers distance-attenuation default stays 2 for compatibility; new native lights
use falloff 1 and serialize it explicitly. Kelvin must be finite and within 1500-15000 K; unknown
units, color modes, and source presets are rejected. Source presets are editable RGB approximations,
not spectral simulations, and retain their stable enum identities when saved.

Curves need at least two anchors and cannot be instance-definition members. Curve samples and tool
drafts are derived/transient. QGEO 1.3 stores explicit disjoint Hard and Soft edge-normal rows,
face texture scale/shift/rotation, and distinct automatic (`Authored`) and explicit Flat face output.
Pre-1.3 encoding used Flat for both ordinary constructor defaults and explicit Shade Flat, so those
indistinguishable values migrate to automatic edge normals. This also applies to legacy native CSR;
explicit Flat survives unchanged once written as QGEO 1.3. QGEO 1.0 reads identity texture transforms.
QGEO 1.0 and 1.1 first migrate every live non-Hard edge shared by at least two Smooth faces to
explicit Soft, before migrating implicit face defaults. Deferred pre-1.3 geometry is materialized
and canonicalized on save; subsequent reads may preserve the current encoded payload lazily.
Current writers omit legacy `ScenePayload.objects`: old records may
fill missing entity editor-object IDs, but explicit conflicting IDs fail.

Selection, hover, history, tools, and previews are not persisted. The stored perspective camera is
an explicit exception to otherwise transient viewport state. JSON `.qdr` input is unsupported;
`qdr_dump --json` produces developer inspection output only.

Protect format changes with QDR round-trip/malformed-input fixtures and project-session/API
save/open tests. See [AGENTS.md](AGENTS.md) and the root verification contract.
