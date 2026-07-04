<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Lua Examples

Lua examples are intentionally placeholders for now. The native app does not ship a Lua runtime or
Lua bindings yet.

When Lua is enabled, examples in this folder should start from the public `quader` scripting object.
That object should map to public editor and document behavior only: whole-editor intent belongs to
the future Lua equivalent of `QuaderEditorApi`, document/modeling intent belongs to the future Lua
equivalent of `QuaderApi`, and command execution should be backed by canonical typed C++ command IDs
such as `quader::quader_command::kEditUndo`.

Lua examples should describe result status, changed state, returned values, and error messages in a
way that matches the C++ `ok()`, `changed()`, `value()`, and `error_message()` helpers. They must
not expose document, absorbed modeling, UI, render, platform, ECS, backend internals, or raw mutable
service pointers.
