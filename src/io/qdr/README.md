<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# QDR IO Module

Portable Quader persistence owned by the native app.

Owns:

- canonical `.qdr` or `.quader` format
- versioned read/write
- migrations
- fixture-backed golden, migration, and malformed-file tests
- optional debug JSON format
- stable file DTOs that are converted into editor runtime state instead of
  serializing live `EditorSession` internals directly

The stable DTO boundary is `<io/qdr/qdr_document.h>` plus the
`quader_qdr_document_io` target. It depends on portable document types only.
The root schema is versioned with `format: "quader.qdr"`, `version: 1`, and a
canonical `document` payload. Readers still accept the old v1 `session` payload
key as a migration alias, but writers must not emit it.

The legacy `<io/qdr/qdr.h>` API remains as a modeling-runtime compatibility
adapter for callers that still save or load `SessionState`. It converts through
`QdrDocument` and must not add canonical schema fields or serialize live editor
runtime state such as dirty flags, undo history, redo history, active tools, or
preview/transient data.

Real fixture-backed golden, migration, and malformed-file coverage lives in
`tests/modeling_domain/qdr_io_document_tests.cpp` with fixtures under
`tests/modeling_domain/fixtures/io`.
Compatibility coverage for the session-state adapter lives in
`tests/modeling_domain/qdr_io_session_tests.cpp` and is registered as the
`qdr_runtime_io_compat_tests` CTest target. Live editor runtime fields remain
excluded from the durable schema.

Engine asset formats wrap or import/export this portable data.
