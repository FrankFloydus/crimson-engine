<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Public Editor API

`QuaderEditorApi` is the default public front door for native-app editor intent. Use `QuaderApi`
for document/modeling-only automation, tests, and examples that do not need runtime editor domains.
Small standalone programs can create an editor through `create_standalone_editor()` and then stay on
the public `QuaderEditorApi` / `QuaderApi` surface.

```cpp
quader::QuaderStandaloneEditor standalone = quader::create_standalone_editor();
quader::QuaderEditorApi& editor = standalone.editor();

const auto created = editor.modeling().create_box({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "API Box",
});

if (!created.ok()) {
    throw std::runtime_error(created.error_message());
}

auto object = editor.object(created.value());
object.rename("Renamed API Box");
object.select();

const auto undo = editor.commands().execute(quader::quader_command::kEditUndo);
if (!undo.ok()) {
    throw std::runtime_error(undo.error_message());
}
```

Use public API domains for editor intent:

- `editor.project()` for project lifecycle status and future project IO.
- `editor.scene()`, `editor.modeling()`, `editor.selection()`, `editor.assets()`,
  `editor.history()`, and `editor.query()` for document/modeling work.
- `editor.commands()` for stable command IDs shared by menus, shortcuts, tests, and future scripts.
- `editor.viewports()`, `editor.tools()`, `editor.preferences()`, and `editor.ui()` for runtime
  editor state such as viewport layout, pane view, pane shading, grid preset, focused pane, camera
  state, active tools, user preferences, and visible panels.

Common ergonomic patterns:

```cpp
quader::QuaderApi& api = editor.api();

api.selection().select_object(object.ref());
const auto duplicated =
        editor.commands().execute(quader::quader_command::kModelingDuplicateMeshes);
if (!duplicated.ok()) {
    throw std::runtime_error(duplicated.error_message());
}
editor.main_viewport().set_shading_mode(quader::QuaderShadingMode::Wireframe);

quader::QuaderSceneBatch batch = editor.scene().batch("Arrange blockout");
const auto arranged = batch.rename(entity, "North Wall")
                              .set_transform(entity, moved)
                              .set_visible(helper, false)
                              .commit();
if (!arranged.ok()) {
    throw std::runtime_error(arranged.error_message());
}
```

The `object` variable above is a `QuaderObjectHandle`, the preferred fluent surface for object
workflows. `QuaderSceneBatch` groups scene edits into one undoable operation. Check API results
through the callable helpers: `result.ok()`, `result.changed()`, `result.value()`, and
`result.error_message()`.

`QuaderScript` is the current C++ automation DSL. Build scripts from a public API facade and inspect
the same result helpers after `run()`:

```cpp
const quader::QuaderBounds wall_bounds{
        .min = {-2.0f, 0.0f, -0.1f},
        .max = {2.0f, 3.0f, 0.1f},
};
quader::QuaderScript script = api.script("Blockout");
const auto script_result = script.create_box("wall", wall_bounds, "Wall")
        .select("wall")
        .move_selection({0.0f, 0.0f, 2.0f})
        .run();

if (!script_result.ok()) {
    throw std::runtime_error(script_result.error_message());
}
```

Future Lua examples should start from the public `quader` scripting object once native Lua bindings
exist. Do not expose or depend on raw editor services from public scripting docs.

**Internal Architecture Note:** Low-level document, absorbed modeling, render, UI, and platform
services remain owner layers. Internal architecture docs may discuss those systems, but public
examples should not start from them.
