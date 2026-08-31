<!--
//////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
//////////////////////////////////////////////////////////////////////////////
-->
# QDR Compatibility Fixtures

`legacy_object_record_schema3.qdr` is an immutable binary compatibility fixture for the retired
`ScenePayload.objects` / `ObjectRecord` layout. Its schema and writer contract are frozen from
repository commit `daf64c85` (`feat: add project workspaces and binary scene storage`), where the
native QDR writer emitted that redundant object projection. The container holds a schema-3 scene
metadata chunk and deliberately gives the canonical entity no editor object ID; the legacy record
supplies ID `77` while its stale name and visibility must not override entity-owned state.

SHA-256: `A3230041C0CD196A4A4E22E202BCD8109DC2E19484AD1E9DDFC5BD1D465CEA8F`

Keep this file byte-for-byte stable. New writer tests must create their own documents and must not
regenerate this fixture with the current FlatBuffers schema.
