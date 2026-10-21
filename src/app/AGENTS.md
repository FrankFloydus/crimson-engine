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
- App/UI use the neutral `RenderHost`, `RenderSceneSink` and `RenderView` facades. Each final
  executable links one `create_app_render_host()` implementation. On `main`,
  `quader_render_full_host` supplies Filament for both Full and Public 0.1. On
  `quader-crimson`, Full keeps that host and Public uses `quader_render_crimson_host`.
  Preserve shared application changes in both branches without carrying over the other
  branch's renderer composition. The Crimson dependency and probes remain available on
  `main`; they are outside its Public executable's link closure.
  Do not select a backend in the shared runtime or duplicate renderer/resource owners per pane.
  Current activation and dependency requirements live in the
  [Crimson integration contract](../../thirdparty/crimson/README.md).
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
  callbacks; Full's native preview and standalone surfaces retain their existing submission path.
- Vulkan startup first reveals the retained Qt shell and its transparent composition anchor, then
  lends Qt's initialized device/queue to the selected `RenderHost`. `QRhiWidget` pane textures and
  ordinary Qt widgets share one window composition. One render host coordinates all panes and
  their independent offscreen targets. The anchor survives scene changes within
  that shell generation. A project/Home replacement retires its render borrowers before destroying
  the old Qt root/device. Prepare the new shell with current scene bindings and final placement,
  then let normal native exposure present its logo before synchronous renderer initialization.
  Matching geometry and a returned `show()` do not prove exposure or submission. The queued
  replacement waits for that submission without pumping events or using a timeout fallback; a
  newer project commit invalidates preparation, and an accepted Close must not resume renderer
  startup. Bind dirty-close preflight before this wait. Never destroy Qt's device while a render
  backend still borrows it.
- A changed pane geometry revision prepares the current scene/camera before that Qt paint, even
  when layout runs before the next timer. `prepare_composed_frame()` must not consume input twice
  or recursively refresh the UI. Wrap backend producers through `SharedRenderCommandBatch`; nested
  paints may drain earlier work while the enclosing producer still has later commands to enqueue.
- Complete the pane-preparation epoch before its first shared submission. Crimson must preflight
  every active pane's scene/editor candidate before recording any pane, then advance one shared
  frame. Accept scene targets, preview suppression and editor coverage together; rejected candidates
  retain the previous display until valid retry. App scheduling must not publish pending coverage
  or advance accepted revisions. Exact selection remains with native query owners, including their
  CPU fallback while Crimson's optional native selection accelerator is unimplemented. See the
  [render contract](../render/AGENTS.md) and [GPU fixture](../../tests/crimson_scene_composition_probe.cpp).
- Autosave uses a separate coarse timer and `ProjectAutosaveService`; never serialize scenes in
  frame callbacks. Defer while modal/popup/pointer interactions or owner previews are active.
- `AppFramePresentationGate` yields submission until newly shown modal/tool/popup windows receive
  their first paint. Nested Qt event loops then retain live rendering; avoid starving first paint
  or suppressing rendering for an entire dialog lifetime.
- `AppFrameSchedule` retains display cadence with VSync, tests GPU readiness before expensive
  uncapped frame work, backs off rejected submissions, and uses an idle interval when appropriate.
  Qt owns the composed window's actual VSync; the host owns supported native-surface modes and records
  the compositor's resolved mode. Count composed presentations after the common Qt flush, not when
  a timer merely requests pane updates. Continuous synchronized composition requests its next App
  frame from the root anchor's completed submission, through the coalesced queued request; the
  rounded display timer remains a fallback after two display periods. Do not introduce a second
  independent pacing clock.
  Timer changes cannot implement VSync. The GPU-free root-callback regression and the opt-in
  `QtFlyNavigationPacingTests` real-App fixture protect the gates and delivery cadence.
- Scene/project replacement must install the new UI binding before publishing lifecycle events.
  `render_frame()` reacquires the active scene after synchronous input dispatch. Do not retain
  scene/session pointers across commands or nested event loops.
- Shutdown stops scheduling and detaches UI/render callbacks while their owners live. Release pane
  and preview render resources, then the scene sink and render host, and only then destroy
  the Qt shell/device. Keep render material resolvers alive through bridge teardown. Partial
  startup failures must preserve the same borrowed-device lifetime order.
  Inspect Crimson completion failure before returning from Qt's device-release callback; unknown
  GPU completion cannot be made safe by destroying the borrowed device after native quarantine.

Verification pointers: `tests/app_frame_clock_tests.cpp`, `tests/qt_project_workspace_tests.cpp`,
`tests/qt_startup_presentation_tests.cpp`, and `tests/render_presentation_tests.cpp`. Window/backend
changes also need `tools/boot_smoke.ps1` when headless tests cannot inspect them; follow root builds.
