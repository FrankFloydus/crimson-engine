<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Docs Guide

Read `../agents/docs-public-examples.md` before changing public documentation.

## Docs Rules

- Public docs and examples should start from `QuaderEditorApi`, `QuaderApi`,
  `QuaderObjectHandle`, `QuaderSceneBatch`, `QuaderScript`, or future Lua `quader` bindings.
- Label internal architecture docs clearly when they discuss services such as `SceneDocument`,
  `AssetRegistry`, `ModelingSessionService`, or render internals.
- Do not add executable Lua examples until Lua exists in the native app.
- Keep docs aligned with public API behavior and tests.
