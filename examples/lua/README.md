<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Lua Examples

The native app does not currently ship a Lua runtime or bindings; this directory is a placeholder,
not a runnable scripting API. Runnable public examples live under
[examples/sdk](../sdk).

If Lua support is requested, route scripts through `QuaderEditorApi` / `QuaderApi` and canonical
commands. Preserve the public result semantics (`ok()`, `changed()`, `value()`, `error_message()`)
and keep mutable owner/backend pointers private. See [the examples guide](../AGENTS.md).
