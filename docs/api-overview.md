# Quader Public Editor API

Quader editor automation should start from `QuaderEditorApi` in the native app runtime, or from
`QuaderApi` for document/modeling-only behavior tests and examples.

```cpp
quader::ProjectSession session;
quader::QuaderEditorApi editor(session);

auto created = editor.modeling().createBox({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "API Box",
});

if (created.ok()) {
    editor.object(created.value())
            .rename("Renamed API Box")
            .select();
    editor.commands().execute(quader::QuaderCommand::HistoryUndo);
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
editor.selection().selectObject(object.entity);
editor.commands().execute(quader::QuaderCommand::ModelingDuplicateMeshes);
editor.mainViewport().setShadingMode(quader::QuaderShadingMode::Wireframe);

editor.scene().batch("Arrange blockout")
        .rename(entity, "North Wall")
        .setTransform(entity, moved)
        .setVisible(helper, false)
        .commit();
```

Low-level document, SDK, render, UI, and platform services remain owner layers. Public examples
should not start from those internals.
