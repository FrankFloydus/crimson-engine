<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# App Package Guide

- `App` composes services and owns startup/shutdown, persisted top-level placement, and frame flow.
  Domain behavior remains in its owner; `UISystem` constructs/composes the retained `QMainWindow`.
- `QApplication` owns the only event loop. The app `QTimer` schedules UI refresh, semantic input,
  scene synchronization, viewport preparation, and visible native-surface submission.
- Autosave uses a separate coarse timer and `ProjectAutosaveService`; never serialize scenes in
  frame callbacks. Defer while modal/popup/pointer interactions or owner previews are active.
- `AppFramePresentationGate` yields submission until newly shown modal/tool/popup windows receive
  their first paint. Nested Qt event loops then retain live rendering; avoid starving first paint
  or suppressing rendering for an entire dialog lifetime.
- `AppFrameSchedule` retains display cadence with VSync, tests GPU readiness before expensive
  uncapped frame work, backs off rejected submissions, and uses an idle interval when appropriate.
  Actual VSync and surface lifetimes belong to `RenderSystem`; timer changes cannot implement it.
- Scene/project replacement must install the new UI binding before publishing lifecycle events.
  `render_frame()` reacquires the active scene after synchronous input dispatch. Do not retain
  scene/session pointers across commands or nested event loops.
- Shutdown stops scheduling, clears UI runtime hooks while callback owners live, destroys native
  surfaces before the renderer, and keeps render material resolvers alive through bridge teardown.

Verification pointers: `tests/app_frame_clock_tests.cpp`, `tests/qt_project_workspace_tests.cpp`,
`tests/qt_startup_presentation_tests.cpp`, and `tests/render_presentation_tests.cpp`. Window/backend
changes also need `tools/boot_smoke.ps1` when headless tests cannot inspect them; follow root builds.
