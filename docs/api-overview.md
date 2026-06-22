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

if (created.ok) {
    editor.object(created.value).select();
    editor.commands().execute("history.undo");
}
```

Use public API domains for editor intent:

- `editor.project()` for project lifecycle status and future project IO.
- `editor.scene()`, `editor.modeling()`, `editor.selection()`, `editor.assets()`,
  `editor.history()`, and `editor.query()` for document/modeling work.
- `editor.commands()` for stable command IDs shared by menus, shortcuts, tests, and future scripts.
- `editor.viewports()`, `editor.tools()`, `editor.preferences()`, and `editor.ui()` for runtime
  editor state where the app has attached runtime hooks.

Low-level document, SDK, render, UI, and platform services remain owner layers. Public examples
should not start from those internals.
