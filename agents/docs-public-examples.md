# Docs And Public Examples

Public-facing docs and examples should begin from the public API, not internal owners.

Preferred starting points:

- `QuaderEditorApi` for future whole-editor runtime examples;
- `QuaderApi` for document/modeling examples;
- `QuaderObjectHandle` for fluent object examples;
- `QuaderScript` for C++ automation examples;
- Lua `quader` bindings for user scripting examples;
- `BehaviorScenario` for behavior-test examples.

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
examples/lua/create_box.lua
```
