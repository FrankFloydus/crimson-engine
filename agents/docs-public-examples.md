<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Docs And Public Examples

Public-facing docs and examples should begin from the public API, not internal owners.

Preferred starting points:

- `QuaderEditorApi` for whole-editor runtime examples;
- `QuaderApi` for document/modeling examples;
- `QuaderObjectHandle` for fluent object examples;
- `QuaderScript` for C++ automation examples;
- future Lua `quader` bindings for user scripting examples once Lua is implemented;
- `BehaviorScenario` for behavior-test examples.

Public examples should prefer the polished API surface:

- callable result helpers: `result.ok()`, `result.changed()`, `result.value()`,
  `result.errorMessage()`;
- typed command IDs: `QuaderCommand::HistoryUndo`, `QuaderCommand::ModelingDuplicateMeshes`,
  and other `QuaderCommand::*` constants;
- `QuaderObjectHandle` for object workflows;
- `QuaderSceneBatch` for grouped scene edits;
- `editor.viewports()` / `editor.mainViewport()` for viewport layout, camera, and pane intent.

Avoid starting public examples with:

- `SceneDocument`;
- `AssetRegistry`;
- `ModelingSessionService`;
- internal `quader_editor_core::EditorSession` or absorbed modeling runtime internals;
- `SceneRenderBridge`;
- UI, render, platform, and backend internals.

Internal docs may explain those systems, but must be clearly labeled as internals.

Current public docs/examples:

```text
docs/api-overview.md
docs/editor-scripting-lua.md
examples/create_box.cpp
examples/script_modeling_workflow.cpp
examples/lua/README.md
```

Treat additional public docs/examples as backlog unless the files are created in the same change.

Do not add executable Lua examples until the native app actually ships a Lua runtime and binding
layer.
