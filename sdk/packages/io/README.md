# IO Package

Portable Quader persistence.

Owns:

- canonical `.qdr` or `.quader` format
- versioned read/write
- migrations
- fixture-backed golden, migration, and malformed-file tests
- optional debug JSON format
- stable file DTOs that are converted into editor runtime state instead of
  serializing live `EditorSession` internals directly

The stable DTO boundary is `<quader_io/qdr_document.h>` plus the
`qdr_io_document` target. It depends on portable document types only. The root
schema is versioned with `format: "quader.qdr"`, `version: 1`, and a canonical
`document` payload. Readers still accept the old v1 `session` payload key as a
migration alias, but writers must not emit it.

The legacy `<quader_io/qdr.h>` API remains as an editor-core compatibility
adapter for callers that still save or load `SessionState`. It converts through
`QdrDocument` and must not add canonical schema fields or serialize live editor
runtime state such as dirty flags, undo history, redo history, active tools, or
preview/transient data.

Real fixture-backed golden, migration, and malformed-file coverage lives in
`tests/qdr_io_document_tests.cpp` with fixtures under `sdk/tests/fixtures/io`.
Compatibility coverage for the session-state adapter lives in
`tests/qdr_io_session_tests.cpp`. Live editor runtime fields remain excluded
from the durable schema.

Engine asset formats wrap or import/export this portable data.
