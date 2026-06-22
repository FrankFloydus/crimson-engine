# Lua Scripting Status

Lua scripting is planned but not implemented in this pass.

There is currently no Lua runtime, binding layer, script console, startup script loader, or Lua
dependency in the native app. Future Lua work should bind only public API surfaces such as
`QuaderEditorApi`, `QuaderApi`, and stable value types.

Before enabling Lua, the project must add an explicit dependency and license decision, a sandbox
policy, structured script error reporting, cancellation behavior, and undo transaction grouping.
Startup scripts should remain disabled until that policy exists.
