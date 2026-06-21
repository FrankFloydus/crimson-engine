# Public API And Scripting

`QuaderApi` is the official app-facing front door for editor/user intent. Future whole-editor work
should grow toward `QuaderEditorApi`, with Lua scripting bound only to public API surfaces.

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
api.modeling().createBox(...);
api.selection().selectFace(...);
api.history().undo();
api.assets().addMaterial(...);
editor.viewports().main().setShadingMode(QuaderShadingMode::Wireframe);
```

## Keep Low-Level When

Keep code in owner services when it owns:

- document, SDK, asset, render, or platform invariants;
- per-frame hot loops, mouse-move picking, draw submission, caches, or GPU resources;
- storage/revision internals;
- Unity parity mechanics;
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
- Public examples must start from `QuaderApi`, future `QuaderEditorApi`, or Lua `quader`.
- Lua binding code may bind only public API types and approved value structs.
