<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Public API And Scripting

`QuaderEditorApi` is the official app-facing front door for whole-editor user intent in the native
app. Use `QuaderApi` for document/modeling-only automation, tests, and examples that do not need
runtime editor domains. Lua scripting, when implemented, must bind only to public API surfaces.

Read `programmatic-api-plan.md` for the current global plan.

## Add To Public API When

Add a capability to the public API when it is:

- user-visible editor behavior;
- scriptable behavior;
- test or automation behavior;
- command palette behavior;
- undoable/history-visible behavior;
- public onboarding/example behavior;
- UI code expressing user intent rather than per-frame mechanics.

Examples:

```cpp
editor.modeling().createBox(...);
editor.object(object).rename("Wall").select();
editor.selection().selectFace(object, face);
editor.scene().batch("Arrange").setTransform(entity, moved).commit();
editor.commands().execute(QuaderCommand::HistoryUndo);
editor.mainViewport().setShadingMode(QuaderShadingMode::Wireframe);
```

Prefer these public API ergonomics for C++ callers:

- `QuaderCommand::*` typed command IDs instead of raw string literals.
- `QuaderObjectHandle` for fluent object actions such as `rename`, `select`, `setVisible`,
  `setLocked`, `duplicate`, `destroy`, and material assignment.
- `QuaderSceneBatch` for grouped scene edits that should commit as one undoable operation.
- Convenience selection overloads before manually assembling `std::span` or component requests.
- `QuaderApiResult` helpers such as `ok()`, `changed()`, `value()`, and `errorMessage()` in public
  examples. Legacy field access can remain in older internal code.

## Keep Low-Level When

Keep code in owner services when it owns:

- document, SDK, asset, render, or platform invariants;
- per-frame hot loops, mouse-move picking, draw submission, caches, or GPU resources;
- storage/revision internals;
- private modeling, viewport, and render mechanics;
- private state transitions that must remain behind service/command owners.

If a new UI/test/script needs behavior that the public API does not expose yet, add the missing
owner behavior first, then expose it through a public API method. Do not bypass the API from
UI/tests/scripts because the method is missing.

## Scripting Rules

Lua must call public API bindings, not raw C++ internals.

Lua bindings must not expose:

- `SceneDocument`;
- `AssetRegistry`;
- SDK `EditorSession`;
- `ModelingSessionService`;
- Filament, ImGui, SDL, EnTT, or raw platform state;
- mutable internal pointers.

Lua handles store stable refs/IDs and validate liveness on every call.

Scripts should:

- run on the editor/main thread or enqueue work onto it;
- support undo transaction grouping;
- report structured errors with line/stack/status;
- write output to the in-app console;
- have filesystem/process/network access disabled by default;
- keep startup scripts disabled until sandbox policy exists.

Target Lua style:

```lua
quader.transaction("Create box", function()
    local box = quader.modeling:create_box({
        min = {-1, -1, -1},
        max = {1, 1, 1},
        name = "Lua Box"
    })

    box:select()
    box:move({2, 0, 0})

    local viewport = quader.viewports:main()
    viewport:set_shading_mode("wireframe")
    viewport:frame_selection()
end)
```

## Guardrails

- Public headers under `src/api` must avoid backend/internal owner headers.
- `src/api` must not call raw SDK execution shortcuts outside approved execution services.
- Public examples must start from `QuaderEditorApi`, `QuaderApi`, `QuaderObjectHandle`,
  `QuaderScript`, or future Lua `quader`.
- Lua binding code may bind only public API types and approved value structs.
