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
- Home starts centered at 1200 by 740 Qt logical client pixels, clamped to the current display's
  work area and native frame margins independently of interface scale. Its resized placement is
  never persisted. Project placement uses `AppWindowSettings::resolve_project_path()` in the
  per-user `project_windows` directory, keyed by canonical manifest path; the legacy global file
  is a read-only fallback for projects without their own file. At a committed project binding
  change, save the outgoing project's normal/maximized placement before hiding its root. Queue
  replacement until the initiating action, loading operation, composition and modal/popup stacks
  have returned. Keep the project session and editor API, but create a fresh UI/render generation;
  never apply the destination placement to the outgoing window. Apply final placement to the new
  root before its first show and logo submission. Scene switches within one project, cancelled
  actions and failed project loads keep their root. Accepted quit saves the outgoing project and
  must not create or reveal Home.
- `QApplication` owns the only event loop. The app `QTimer` schedules UI refresh, semantic input,
  scene synchronization and viewport preparation. Vulkan scene panes submit from Qt's composition
  callbacks; native preview and standalone surfaces retain their existing submission path.
- Vulkan startup first reveals the retained Qt shell and its transparent composition anchor, then
  lends Qt's initialized device/queue to `RenderSystem`. The anchor survives scene changes within
  that shell generation. A project/Home replacement retires its render borrowers before destroying
  the old Qt root/device. Prepare the new shell with current scene bindings and final placement,
  then let normal native exposure present its logo before synchronous renderer initialization.
  Matching geometry and a returned `show()` do not prove exposure or submission. The queued
  replacement waits for that submission without pumping events or using a timeout fallback; a
  newer project commit invalidates preparation, and an accepted Close must not resume renderer
  startup. Bind dirty-close preflight before this wait. Never destroy Qt's device while Filament
  still borrows it.
- A changed pane geometry revision prepares the current scene/camera before that Qt paint, even
  when layout runs before the next timer. `prepare_composed_frame()` must not consume input twice
  or recursively refresh the UI. Wrap Filament producers in `SharedRenderCommandBatch`; nested
  paints may drain earlier work while the enclosing producer still has later commands to enqueue.
- Autosave uses a separate coarse timer and `ProjectAutosaveService`; never serialize scenes in
  frame callbacks. Defer while modal/popup/pointer interactions or owner previews are active.
- `AppFramePresentationGate` yields submission until newly shown modal/tool/popup windows receive
  their first paint. Nested Qt event loops then retain live rendering; avoid starving first paint
  or suppressing rendering for an entire dialog lifetime.
- `AppFrameSchedule` retains display cadence with VSync, tests GPU readiness before expensive
  uncapped frame work, backs off rejected submissions, and uses an idle interval when appropriate.
  Qt owns the composed window's actual VSync; `RenderSystem` owns native-surface modes and records
  the compositor's resolved mode. Count composed presentations after the common Qt flush, not when
  a timer merely requests pane updates. Timer changes cannot implement VSync.
- Scene/project replacement must install the new UI binding before publishing lifecycle events.
  `render_frame()` reacquires the active scene after synchronous input dispatch. Do not retain
  scene/session pointers across commands or nested event loops.
- Shutdown stops scheduling and detaches UI/render callbacks while their owners live. Release pane
  and preview render resources, then the scene bridge and Filament engine, and only then destroy
  the Qt shell/device. Keep render material resolvers alive through bridge teardown. Partial
  startup failures must preserve the same borrowed-device lifetime order.

Verification pointers: `tests/app_frame_clock_tests.cpp`, `tests/qt_project_workspace_tests.cpp`,
`tests/qt_startup_presentation_tests.cpp`, and `tests/render_presentation_tests.cpp`. Window/backend
changes also need `tools/boot_smoke.ps1` when headless tests cannot inspect them; follow root builds.
