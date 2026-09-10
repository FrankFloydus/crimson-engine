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
- [qdr_project_write_snapshot.*](qdr_project_write_snapshot.hpp) captures/pins export data before
  serialization. `QdrDocumentReader`/`QdrDocumentWriter` adapt files; `qdr_atomic_write.*` owns durable
  replacement. Project sessions pass their persistent scene UUID on write and consume
  `QdrProjectReadResult::scene_uuid` on open.
- Dependency records contain stable `ProjectAssetId`, kind, and project-relative path. Project
  sessions populate them from referenced materials and validate them against the project catalog.
  `ProjectAssetResolver`/`ProjectFileSystem` own resolution and containment.

## Compatibility that must survive

Current scene metadata schema is 14; schema versions and defaults are enforced in the codec.

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

The FlatBuffers defaults remain intensity 1 and ACES tone mapping, even when new native
objects use different creation defaults. Saved camera values also remain unchanged. Obsolete
ACES Legacy and Display Range settings retain their stored identities; their current viewport
presentation falls back to ACES. Readers predating schema 13 reject new scene files rather
than interpreting the appended AgX mode incorrectly. Schema 14 appends the material diffuse
model with Burley as the wire default; readers predating schema 14 reject new files rather
than silently dropping an authored Lambert selection. Unknown diffuse-model values are rejected.

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
