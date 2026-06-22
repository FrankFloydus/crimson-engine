<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Project Identity

- This repository is the native standalone Windows Quader app.
- `app-refactor.md` is the accepted native-app architecture direction as of 2026-06-22.
- This app owns former SDK code in app modules. Do not preserve the old top-level copied SDK boundary,
  modeling SDK directory override, or compile-time SDK presence macro as native-app authority.
- Preserve established native behavior while moving code, but keep editor document, command,
  selection, history, and projection ownership app-native instead of split across an SDK facade.

## Restore Rule

When the user tells you to restore previous working logic, functioning, or behavior:

- do not redesign it;
- do not improve it while restoring;
- do not reinterpret it;
- do not invent a new policy;
- find the previous working source and integrate the same logic into the current architecture.

Refactors may move restored code behind cleaner classes only if observable behavior, ordering, state
transitions, cache invalidation, and side effects remain identical.

If exact behavior cannot be preserved, stop and explain the conflict before changing code.
