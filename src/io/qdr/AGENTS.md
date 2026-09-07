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
- Keep supported binary schemas round-trip safe. Legacy JSON `.qdr` input is unsupported;
  `qdr_dump --json` is write-only developer inspection output.
- Canonical `Entity` records own serialized object state. Retained `ObjectRecord`/`objects` schema
  fields are reader-only compatibility: an old record may fill a missing editor-object ID, but
  conflicting explicit IDs must fail. Legacy CSR topology also stays reader-only.
- Preserve bounds, overlap, codec, checksum, schema, identity, and asset-reference validation.
  Add QDR fixtures/tests for serialization changes and project-session tests for save/open behavior.
