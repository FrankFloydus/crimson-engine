<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native QDR Scene IO

The native `.qdr` scene format is a binary `QDRB` container. It stores one
scene's graph, embedded assets, and object records; project workspace metadata
belongs in `.qproj` and reusable project assets will live in separate files.

The fixed little-endian container header carries the persistent 128-bit scene
UUID and a 64-bit chunk-directory location/count. Every directory record has
64-bit offsets and sizes, schema/codec/critical flags, CRC32, and a deterministic
content hash. Readers validate bounds, arithmetic, overlaps, codec limits,
checksums, hashes, FlatBuffers, and domain references before returning a scene.

Chunk schema v1 contains:

- one uncompressed `QDRS` FlatBuffer with scene metadata;
- one `QDRG` FlatBuffer per unique embedded geometry payload;
- compiled-mesh fallback arrays stored directly in `QDRG`;
- SDK-owned deterministic `QGEO` packed/CSR authored topology embedded in the
  `QDRG` wrapper, so the native container delegates durable polygon semantics
  to the portable modeling core.

Current scene metadata uses schema 9. It adds durable per-entity curve
solidification state: enabled, radial resolution, and radius. Schema 8 appended
curve assets with SDK-owned binary authored controls and an explicit curve
component on each curve entity; schema-8 curves load with solidification
disabled, resolution 8, and radius 0.1.
Curve controls, open/closed state, stable point/segment IDs and allocation
cursors survive a round trip; display samples and edit-tool drafts do not.
Curve payloads and bindings are validated before a scene is returned. Curves
require at least two anchors and are not allowed inside instance definitions.
Schemas 1–7 remain readable as scenes without curves.

Schema 7 appended an optional affine texture
sampling component to mesh and instance-occurrence entities; schemas 1–6 use
identity sampling. Face scale, normalized shift, and rotation are owned by
SDK QGEO 1.1, with identity defaults when reading QGEO 1.0. Compiled fallback
geometry has an optional parallel UV carrier/geometric-normal array; the
original packed vertex layout and QDRG wrapper version remain unchanged.

Identical geometry is emitted once and referenced by logical mesh assets.
Geometry chunks of at least 64 KiB use Zstandard level 1 only when it saves at
least ten percent. Metadata remains directly readable. Selection, hover,
viewport, history, tools, previews, and other transient editor state are never
serialized.

The container keeps one immutable backing byte buffer. Compressed chunks are
decompressed and checksum-verified on first access, so unreferenced geometry or
future derived-cache chunks do not add hydration work. Codec results expose
phase timings, logical/unique geometry counts, compression byte counts, and
unique decode/compile counts for developer benchmarks.

`QdrDocumentReader` and `QdrDocumentWriter` remain the native adapter surface.
Outer project sessions must pass their persistent `std::array<std::byte, 16>`
scene UUID to the explicit atomic-write overload and consume
`QdrProjectReadResult::scene_uuid` on open. The overload without a UUID exists
only for standalone compatibility.

Scene metadata also has a versioned project-asset dependency catalog carrying
stable ID, asset kind, and validated project-relative path. It is empty in this
slice. `ProjectAssetResolver` is the minimal resolution seam and delegates all
containment, case-collision, and link-escape enforcement to `ProjectFileSystem`;
it intentionally does not implement an asset registry or importer.

Legacy JSON project QDR files are intentionally unsupported and are rejected
before JSON parsing. Portable modeling-document QDR under the SDK boundary is a
separate interchange contract and is not owned by this native scene container.
