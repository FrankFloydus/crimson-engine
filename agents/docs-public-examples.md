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
- `editor.viewports()` / `editor.mainViewport()` for workspace-backed viewport intent.

Avoid starting public examples with:

- `SceneDocument`;
- `AssetRegistry`;
- `ModelingSessionService`;
- SDK `EditorSession`;
- `SceneRenderBridge`;
- ImGui/Filament/SDL internals.

Internal docs may explain those systems, but must be clearly labeled as internals.

Recommended public docs/examples:

```text
README.md
docs/api-overview.md
docs/editor-scripting-lua.md
docs/internal-architecture.md
examples/create_box.cpp
examples/script_modeling_workflow.cpp
examples/custom_document_builder.cpp
examples/lua/README.md
```

Do not add executable Lua examples until the native app actually ships a Lua runtime and binding
layer.
