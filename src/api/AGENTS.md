<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native API

- Start with `QuaderEditorApi` for whole-editor intent and `QuaderApi` for document/modeling
  workflows. Facades delegate to owner services; public headers do not expose Qt, Filament,
  Win32, EnTT, or mutable internal service pointers.
- Use typed `quader_command::*` IDs, `QuaderObjectHandle`, `QuaderSceneBatch`, and result helpers
  (`ok()`, `changed()`, `value()`, `error_message()`). Do not restore `history.*`,
  `modeling.action.*`, or legacy `modeling.*` command aliases.
- Session-bound references must reject stale scene generations. Preserve those checks when adding
  handles, batches, previews, or rebinding API facades across scene activation.
- Scene settings belong to the document: read `query().scene_settings()` and write
  `scene().set_scene_settings(settings, options)`. Environment and fog color preview references
  carry session, document-binding, and edit generations; object selection does not cancel them.
- Add public behavior coverage in `tests/editor_api_tests.cpp` or the matching focused API suite;
  update examples when the public contract changes. Root verification rules apply.
