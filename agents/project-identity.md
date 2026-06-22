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
- It is not the Unity plugin.
- It should not keep old plugin/DLL protection behavior unless a native-app requirement explicitly
  asks for it.
- The Unity plugin checkout at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` is the active
  source of truth for modeling viewport parity.
- The original Unity package lives at
  `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\package\com.qvertexworks.quader`.
- The original Unity SDK lives at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\sdk` and is a
  parity reference only.
- `app-refactor.md` is the accepted native-app architecture direction as of 2026-06-22.
- This app owns former SDK code in app modules. Do not preserve the old top-level copied SDK boundary,
  modeling SDK directory override, or compile-time SDK presence macro as native-app authority.
- Preserve behavior while moving code, but make editor document, command, selection, history, and
  projection ownership app-native instead of split across an SDK facade.

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
