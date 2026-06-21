# Project Identity

- This repository is the native standalone Windows Quader app.
- It is not the Unity plugin.
- It should not keep old plugin/DLL protection behavior unless a native-app requirement explicitly
  asks for it.
- The Unity plugin checkout at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` is the active
  source of truth for modeling viewport parity.
- The original Unity package lives at
  `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\package\com.qvertexworks.quader`.
- The original SDK lives at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\sdk`.
- This app keeps a copied SDK under `sdk`; preserve the SDK boundary and adapt native-app behavior
  around it instead of rewriting SDK semantics in UI or renderer code.

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
