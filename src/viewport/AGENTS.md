<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Viewport Package Guide

- `ViewportWorkspaceService` owns layout, focused pane, camera controllers, projection-specific
  shading, per-pane selected tools and control overrides, shared grid preset, and render
  size/visibility bookkeeping. UI presents this
  state; render owns draw resources. Expose user intent through `QuaderViewportApi`.
  Preference defaults feed one revision-gated projection. Explicit session overrides survive
  layout/tab changes and later default changes; pane-local strip controls do not save preferences.
  Backface Selection retains its existing shared preference and selection policy.
  A pane's remembered tool is separate from the scene's single executing operation. Focus alone
  never acquires or settles it. Actual editing acquires through `QuaderToolsApi`; capture retains
  the initiating pane and its effective snapping through release. Passive gizmos use retained
  presentation frames and must not run interactive tool lifecycle code.
- Read [INPUT_KEYBINDINGS.md](../../INPUT_KEYBINDINGS.md) for input changes. `ViewportInputRouter`
  consumes semantic navigation and resolved shortcuts; raw Qt/Win32 events stay in host adapters.
  Preserve explicit text-focus, navigation-capture, tool, viewport, and global shortcut contexts.
- `ViewportRuntimeBindingToken` fences synchronous dispatch across scene/project replacement.
  Check it after mutations and stop using old owner pointers or remaining sampled input on rebind.
- Preserve the physical modifier chord captured at pointer press through deferred release. Tool
  handling must account for navigation suppression, hover, selection fallback, gizmo consumption,
  preview cancellation/commit, and undo/redo.

Verification pointers: `tests/viewport_interaction_controller_tests.cpp`,
`tests/qt_input_adapter_tests.cpp`, and the viewport tests selected by `tools/affected_check.ps1`.
Use public APIs/`BehaviorScenario` for inspectable state and follow root build/test requirements.
