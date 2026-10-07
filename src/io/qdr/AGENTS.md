<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Binary QDR Scene IO

See [README.md](README.md) for the format/owner map.

- Native extraction/hydration wraps the SDK-owned authored-topology codec. Serialize from
  `QdrProjectWriteSnapshot`; preserve the persistent scene UUID supplied by the project session.
- Successful native read/write results carry the exact source byte count and FNV-1a fingerprint
  for save synchronization. Compute these from the bytes already being decoded/serialized.
- Current scene metadata is schema 19; optional per-mesh wire colors inherit the viewport
  default when absent or read from older schemas. Colors contain finite normalized sRGB.
  Schema 18 mesh wireframe-on-shaded defaults to enabled
  for older schemas or absent fields. Schema 17 affine parenting offsets preserve editable TRS.
  Earlier schemas default offsets to identity. Validate finite affine matrices without requiring
  invertibility, and keep full and dependency-only readers consistent.
  Scene settings and ordered top-level IDs belong to the schema-16-and-later scene payload.
  Historical Scene wrapper fields are reader-only; migrate and validate them before publication.
- Keep supported binary schemas round-trip safe. Legacy JSON `.qdr` input is unsupported;
  `qdr_dump --json` is write-only developer inspection output.
- Canonical `Entity` records own serialized object state. Retained `ObjectRecord`/`objects` schema
  fields are reader-only compatibility: an old record may fill a missing editor-object ID, but
  conflicting explicit IDs must fail. Legacy CSR topology also stays reader-only.
- Preserve bounds, overlap, codec, checksum, schema, identity, and asset-reference validation.
  Add QDR fixtures/tests for serialization changes and project-session tests for save/open behavior.
