<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# API Package Guide

Read `../../agents/public-api-and-scripting.md` before changing this package.

## API Rules

- `QuaderEditorApi` is the whole-editor API for runtime/editor intent.
- `QuaderApi` is for document/modeling workflows that do not need runtime editor domains.
- Public API code delegates to owner services; it must not become the owner of document, render,
  asset, UI, viewport, or platform invariants.
- Public headers must avoid backend/internal owner headers such as UI backend, Filament, SDL, EnTT
  internals, and mutable service pointers.
- Prefer `QuaderCommand::*`, `QuaderObjectHandle`, `QuaderSceneBatch`, and
  `QuaderApiResult`/`QuaderApiVoidResult` helpers in public-facing surfaces and examples.
- Command execution should use typed `QuaderCommand` values or canonical command IDs returned by
  `QuaderCommandsApi` helpers. Do not reintroduce legacy command aliases or registry
  canonicalization for `history.*`, `modeling.action.*`, or `modeling.*` routes.
- When changing public behavior, update API/behavior guardrail tests and public docs/examples when
  the user-facing contract changes.
