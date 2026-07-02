<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Public Editor API

Quader editor automation should start from `create_standalone_editor()` in small standalone programs,
from `QuaderEditorApi` in the native app runtime, or from `QuaderApi` for document/modeling-only
behavior tests and examples.

```cpp
quader::QuaderStandaloneEditor standalone = quader::create_standalone_editor();
quader::QuaderEditorApi& editor = standalone.editor();

auto created = editor.modeling().create_box({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "API Box",
});

if (created.ok()) {
    auto object = editor.object(created.value());
    object.rename("Renamed API Box");
    object.select();
    editor.commands().execute(quader::quader_command::kHistoryUndo);
}
```

Use public API domains for editor intent:

- `editor.project()` for project lifecycle status and future project IO.
- `editor.scene()`, `editor.modeling()`, `editor.selection()`, `editor.assets()`,
  `editor.history()`, and `editor.query()` for document/modeling work.
- `editor.commands()` for stable command IDs shared by menus, shortcuts, tests, and future scripts.
- `editor.viewports()`, `editor.tools()`, `editor.preferences()`, and `editor.ui()` for runtime
  editor state. Viewport layout, pane view, pane shading, grid preset, focused pane, and camera state
  are backed by the viewport workspace service rather than UI callbacks.

Common ergonomic patterns:

```cpp
editor.scene().select(entity);
editor.selection().select_object(object.entity);
editor.commands().execute(quader::quader_command::kDuplicateSelection);
editor.main_viewport().set_shading_mode(quader::QuaderShadingMode::Wireframe);

editor.scene().batch("Arrange blockout")
        .rename(entity, "North Wall")
        .set_transform(entity, moved)
        .set_visible(helper, false)
        .commit();
```

Low-level document, absorbed modeling internals, render, UI, and platform services remain owner
layers. Public examples should not start from those internals.
