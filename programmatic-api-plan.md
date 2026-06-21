# Quader Whole-Editor Public API And Lua Scripting Plan

Status: architecture plan for the next global editor API refactor.

Last updated: 2026-06-22

## Purpose

Quader should be fully controllable through one clean public editor API. The current `QuaderApi`
work proved the direction: behavior tests, examples, future scripts, and UI commands become easier
to read when they express editor intent instead of low-level service choreography.

This plan expands that idea from modeling/document operations to the whole native editor:

- scene, modeling, selection, assets, history, query, and render sync;
- project/file operations;
- viewport panes, cameras, shading, grid, overlays, screenshots, and framing;
- tools, commands, shortcuts, command palette, and preferences;
- UI panels, notifications, console, and layout;
- Lua scripting as a frontend over the same public API.

The goal is not to delete the engine room. Low-level owner systems remain, but app-facing code should
not start there.

## Current Baseline

Already implemented:

- `src/api/QuaderApi.*` as the app-facing root.
- `QuaderSceneApi`, `QuaderModelingApi`, `QuaderSelectionApi`, `QuaderAssetApi`, `QuaderHistoryApi`,
  `QuaderQueryApi`, `QuaderRenderApi`, `QuaderObjectHandle`, and `QuaderScript`.
- `quader_api` static library for non-render public API code.
- Runtime-only render adapter in `quader_runtime`.
- `ModelingCommandExecutionService` as shared command routing policy.
- SDK-side `quader_poly::DocumentBuilder`.
- `tests/support/BehaviorScenario` and programmatic API guardrails.

Missing whole-editor domains:

- project/file lifecycle API;
- viewport pane/camera/layout/shading/grid API;
- tool activation/settings and command palette API;
- UI panel/layout/preference API;
- editor event stream;
- script host/runtime;
- Lua bindings and script console.

## Core Direction

Use a layered API:

```text
Lua scripts / command palette / UI actions / tests / examples
            |
            v
QuaderEditorApi
            |
            +--> QuaderApi: scene/modeling/selection/assets/history/query
            +--> QuaderViewportApi: panes, cameras, shading, grid, framing
            +--> QuaderToolsApi: tool activation, tool settings, modal status
            +--> QuaderCommandsApi: stable command ids and command palette execution
            +--> QuaderPreferencesApi: persistent editor and viewport settings
            +--> QuaderUIApi: panels, focus, console, notifications, dock layout
            +--> QuaderScriptingApi: Lua runtime, registry, script transactions
            +--> QuaderRenderApi: runtime sync adapter only
            |
            v
Owner services: ProjectSession, DocumentCommandService, ModelingSessionService,
Viewport state services, UIPanelManager, UISettings, SceneRenderBridge, SDK/editor-core
```

`QuaderApi` remains the app-facing root for document/modeling state. `QuaderEditorApi` becomes the
runtime/editor aggregate owned by `App`, because it also needs live editor state such as viewport
panes, UI panel visibility, command palette state, and scripting.

## Public API Inclusion Rules

Add a capability to the public API when it is one or more of these:

- user-visible editor behavior;
- scriptable behavior;
- automation or test behavior;
- command palette behavior;
- undoable or history-visible behavior;
- public onboarding/example behavior;
- UI code expressing user intent rather than per-frame mechanics.

Keep code low-level when it owns one or more of these:

- document, SDK, asset, render, or platform invariants;
- per-frame hot loops, mouse-move picking, draw submission, caches, or GPU resources;
- storage/revision internals;
- Unity parity mechanics;
- private state transitions that must remain behind a service/command owner.

If a new UI action, test, or script needs behavior that the public API does not expose yet, add the
missing owner behavior first, then expose it through a clean public API method. Do not bypass the API
from UI/tests/scripts just because the public method is missing.

## Low-Level Code Retirement Policy

Low-level systems remain. They are not "old bad code"; they are the ownership layer.

What retires over time is casual app-facing use of low-level systems:

- UI panels should stop calling `DocumentCommandService` directly when `api.scene()` can express the
  same intent.
- Shortcut/context-menu code should stop executing command tools directly when `api.commands()` or
  `api.modeling()` can express the same intent.
- Tests should prefer `QuaderApi` or `BehaviorScenario` unless they are testing an owner class.
- Scripts must never receive raw access to `SceneDocument`, `ModelingSessionService`,
  `AssetRegistry`, SDK `EditorSession`, Filament, ImGui, or SDL objects.

Approved low-level owner areas:

- `src/document/**`
- `src/assets/**`
- `src/modeling/**`
- `src/editor/**`
- `src/viewport/**` for input/tool/runtime mechanics
- `src/ui/**` for presentation and routing only
- `src/render/**`
- `sdk/packages/document/**`
- `sdk/packages/editor-core/**`

## Final API Domain Map

### `editor.project()`

Public project/file operations:

```cpp
editor.project().newProject();
editor.project().open(path);
editor.project().save();
editor.project().saveAs(path);
editor.project().close();
editor.project().dirty();
editor.project().path();
editor.project().metadata();
```

Delegates to future project IO services and `ProjectSession`.

### `editor.scene()`

Continue using `QuaderSceneApi`:

```cpp
editor.scene().createEntity(args);
editor.scene().rename(entity, "Wall");
editor.scene().setTransform(entity, transform);
editor.scene().duplicate(entity);
editor.scene().groupSelection("Group");
```

Delegates to `DocumentCommandService` and document commands.

### `editor.modeling()` and `editor.object(ref)`

Continue using `QuaderModelingApi` and `QuaderObjectHandle`:

```cpp
auto box = editor.modeling().createBox(bounds).value;
editor.object(box).select();
editor.object(box).move({2, 0, 0});
editor.modeling().executeTool("delete_selection");
```

Delegates to `ModelingCommandExecutionService`, `ModelingSessionService`, and SDK/editor-core.

### `editor.selection()`

Object/component selection, hover, picking, named selection sets:

```cpp
editor.selection().selectFace(box, 4);
editor.selection().hoverVertex(box, 1);
editor.selection().clear();
```

Future additions:

```cpp
editor.selection().saveSet("door_verts");
editor.selection().restoreSet("door_verts");
```

### `editor.assets()`

Materials, textures, standalone meshes, assignment, replacement:

```cpp
auto mat = editor.assets().addMaterial({.name = "Panel"});
editor.object(box).assignMaterial(mat.value.id);
```

No raw mutable `AssetRegistry` exposure.

### `editor.history()`

Undo/redo/repeat and script transactions:

```cpp
editor.history().undo();
editor.history().redo();
editor.history().beginTransaction("Build staircase");
editor.history().commitTransaction();
```

Transaction semantics matter for Lua: a script should be one undo item unless it explicitly splits.

### `editor.query()`

Read-only snapshots and diagnostics:

```cpp
auto snapshot = editor.query().snapshot();
auto entity = editor.query().entity(id);
auto bounds = editor.query().compiledObjectMesh(box);
```

Queries return values, not mutable owner pointers.

### `editor.viewports()`

Viewport pane, camera, shading, grid, overlay, and screenshot operations:

```cpp
auto viewport = editor.viewports().main();
viewport.setShadingMode(QuaderShadingMode::Wireframe);
viewport.setGridVisible(true);
viewport.camera().frameSelection();
viewport.camera().orbit({0, 2, 8}, {0, 0, 0});
viewport.captureScreenshot("C:/tmp/quader-view.png");
```

Target types:

```cpp
enum class QuaderShadingMode {
    Material,
    Solid,
    Wireframe,
    XRay,
};

class QuaderViewportApi;
class QuaderViewportHandle;
class QuaderCameraHandle;
```

Do not let this API mutate `ViewportPanel` internals directly long term. First extract a
`ViewportWorkspaceService` or `ViewportStateService` that owns pane layout, camera state, shading,
grid settings, overlay settings, and active viewport identity. UI and API should call that same
owner.

### `editor.tools()`

Tool activation, current tool state, modal state, and settings:

```cpp
editor.tools().activate("select");
editor.tools().activate("edge_bevel");
editor.tools().setSetting("edge_bevel", "amount", 0.1f);
editor.tools().activeTool();
editor.tools().cancelModalTool();
```

Delegates to `ViewportToolRuntime`, `ModelingCommandDispatcher`, tool catalogs, and modeling
services depending on tool family.

### `editor.commands()`

Stable command registry and command palette route:

```cpp
editor.commands().execute("modeling.delete_selection");
editor.commands().execute("viewport.frame_selection");
editor.commands().available();
editor.commands().shortcutFor("history.undo");
```

Command IDs should be stable and documented. The command API is the bridge between menus, command
palette, shortcuts, scripts, and public API methods.

### `editor.preferences()`

Persistent editor settings:

```cpp
editor.preferences().setViewportGridSize(10.0f);
editor.preferences().setLocale("en");
editor.preferences().setShortcut("history.undo", "Ctrl+Z");
editor.preferences().save();
```

Delegates to `UISettings`, `ViewportSettings`, localization, and future keybind services.

### `editor.ui()`

Panel visibility, dock layout, console, notifications:

```cpp
editor.ui().showPanel("Inspector");
editor.ui().focusPanel("Viewport");
editor.ui().notify({.level = Info, .message = "Script finished"});
editor.ui().console().append("Hello from script");
```

UI API owns presentation routing, not document/modeling semantics.

### `editor.render()`

Runtime adapter only:

```cpp
editor.render().syncScene(bridge);
editor.render().syncToolPreviews(bridge);
editor.render().setSelectionOverlay(bridge, viewportIndex, layerMask, camera);
```

No raw Filament exposure from public API headers.

### `editor.events()`

Future event stream for scripts and automation:

```cpp
editor.events().onSelectionChanged(callback);
editor.events().onProjectSaved(callback);
editor.events().onActiveToolChanged(callback);
```

Events should carry stable snapshots or IDs, not mutable internals.

### `editor.scripting()`

Script registry, Lua runtime, console, startup scripts:

```cpp
editor.scripting().runLua("quader.history:undo()");
editor.scripting().runFile(path);
editor.scripting().registerCommand("make_wall", path);
editor.scripting().setStartupScriptsEnabled(true);
```

## C++ Target Style

```cpp
QuaderEditorApi editor(appContext);

auto viewport = editor.viewports().main();
viewport.setShadingMode(QuaderShadingMode::Wireframe);
viewport.camera().frameSelection();

auto box = editor.modeling().createBox({
    .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
    .name = "Scriptable Box",
}).value;

editor.object(box).select();
editor.object(box).move({2, 0, 0});
editor.tools().activate("edge_bevel");
editor.history().undo();
```

## Lua Scripting Direction

Lua should be a frontend over the public API, not a separate control plane.

Target architecture:

```text
Lua script
   |
   v
QuaderLuaHost / QuaderLuaBindings
   |
   v
QuaderEditorApi / QuaderApi
   |
   v
normal command/session/render/UI owners
```

Candidate files:

```text
src/scripting/QuaderLuaHost.h
src/scripting/QuaderLuaHost.cpp
src/scripting/QuaderLuaBindings.h
src/scripting/QuaderLuaBindings.cpp
src/scripting/QuaderScriptHost.h
src/scripting/QuaderScriptHost.cpp
src/scripting/QuaderScriptRegistry.h
src/scripting/QuaderScriptRegistry.cpp
src/ui/ScriptConsolePanel.h
src/ui/ScriptConsolePanel.cpp
tests/lua_scripting_tests.cpp
```

Dependency direction:

- Prefer Lua 5.4 or LuaJIT only after license/build evaluation.
- A binding helper such as `sol2` may be acceptable only after confirming permissive license and
  vcpkg availability.
- Avoid GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, or source-available scripting
  dependencies unless explicitly approved.

Runtime rules:

- Lua bindings expose only public API types and approved value structs.
- Lua object handles store stable refs/IDs and validate liveness on every call.
- Scripts run on the editor/main thread, or enqueue commands onto it.
- Long-running scripts need cancellation.
- Script output goes to the in-app console.
- Script failures return structured errors with source, line, stack, step, and API status.
- Filesystem/process/network APIs are disabled by default.
- Startup scripts are disabled by default until the sandbox policy exists.

Target Lua style:

```lua
quader.transaction("Build test box", function()
    local box = quader.modeling:create_box({
        min = {-1, -1, -1},
        max = {1, 1, 1},
        name = "Lua Box"
    })

    box:select()
    box:move({2, 0, 0})
    box:assign_material_slot(1)

    local viewport = quader.viewports:main()
    viewport:set_shading_mode("wireframe")
    viewport:frame_selection()
end)
```

## Migration Plan

### Phase 0: Contract Baseline

- Keep current `QuaderApi` as the document/modeling baseline.
- Add this plan and modular agent instructions.
- Replace retired OOP source-of-truth checks with instruction-routing guardrails.

### Phase 1: Editor Aggregate

- Add `QuaderEditorApi` as an app/runtime aggregate.
- Compose it in `App`.
- Pass `QuaderEditorApi&` through `UIContext` while keeping old fields temporarily.
- Do not migrate all UI in one pass.

### Phase 2: Commands And Menus

- Add `QuaderCommandsApi`.
- Route command palette, menus, shortcuts, and context menu through stable command IDs.
- Migrate `MainMenuBar`, `ViewportShortcutController`, and `ViewportContextMenuPresenter` first.

### Phase 3: Scene/UI Panel Migration

- Migrate document mutations in `OutlinerPanel` and `InspectorPanel` to `editor.scene()`.
- Keep panel rendering and selection presentation in UI owners.

### Phase 4: Viewport State Service

- Extract viewport pane/camera/layout/shading/grid state from `ViewportPanel` into a service.
- Add `QuaderViewportApi`, `QuaderViewportHandle`, and `QuaderCameraHandle`.
- Keep per-frame picking and draw mechanics low-level.

### Phase 5: Tool And Preferences API

- Add `QuaderToolsApi` and `QuaderPreferencesApi`.
- Route tool activation/settings and persistent user settings through public APIs.
- Add guardrails for new UI bypasses.

### Phase 6: Events And Transactions

- Add editor event stream.
- Add transaction grouping for scripts and command batches.
- Ensure undo/redo labels remain comprehensible.

### Phase 7: Lua Runtime

- Add Lua dependency after license/build check.
- Add `QuaderLuaHost` with minimal scene/modeling/history bindings.
- Add script error reporting and tests.

### Phase 8: Script Console And User Scripts

- Add in-app script console or console input mode.
- Add user script folder and registry.
- Keep startup scripts disabled until sandbox rules are complete.

### Phase 9: Public Examples And Docs

- Add examples for C++ editor API and Lua.
- Public examples must start from `QuaderEditorApi`, `QuaderApi`, or Lua `quader`.
- Internal-owner examples must be clearly labeled as internals.

### Phase 10: Retire Casual Low-Level App Calls

- Tighten guardrails so new UI/app/test code cannot add direct document/modeling/command bypasses.
- Leave low-level owner files alone.
- Remove transitional `UIContext` owner fields after panels migrate.

## First Migration Targets

1. `App` composes `QuaderEditorApi`.
2. `UIContext` receives `QuaderEditorApi&`.
3. `ViewportShortcutController` uses `editor.commands()` and `editor.history()`.
4. `ViewportContextMenuPresenter` uses `editor.commands()`.
5. `OutlinerPanel` uses `editor.scene()` for rename/delete/select/reparent.
6. `InspectorPanel` uses `editor.scene()` and `editor.assets()`.
7. `ViewportPanel` user-intent actions move behind `editor.viewports()` and `editor.selection()` as
   viewport state extraction makes that safe.

## Guardrails

Add/update tests to enforce:

- public API headers do not expose `SceneDocument`, `AssetRegistry`, SDK `EditorSession`, Filament,
  ImGui, SDL, or EnTT internals;
- Lua bindings include only `src/api`, `src/scripting`, and approved value types;
- public docs/examples start from `QuaderEditorApi`, `QuaderApi`, or Lua `quader`;
- new UI/app/viewport files do not add direct document/modeling mutations when public API exists;
- retired OOP source-of-truth references do not return;
- root `AGENTS.md` stays a thin router and all required `agents/*.md` instruction files exist.

## Risks

- A global API can become a dumping ground. Keep domain APIs focused.
- Viewport APIs can accidentally expose `ViewportPanel`; extract viewport state first.
- Lua handle lifetime can become unsafe unless every handle validates refs before use.
- Script undo grouping can confuse users if partial failures commit too much.
- Script sandboxing needs an explicit policy before startup scripts or filesystem access.
- API stability matters once user scripts exist; breaking script APIs should require migration notes.

## Final Decision

Yes, Quader can grow into a fully programmatically controllable editor. The right model is:

- public API for intent;
- low-level services for mechanics and ownership;
- Lua as a safe language binding over the public API;
- gradual migration backed by guardrails, not a single rewrite.
