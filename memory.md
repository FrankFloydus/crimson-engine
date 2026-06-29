# Quader Agent Memory

This file records hard-resolved tasks whose context is easy to lose and costly to rediscover.
Read it after `AGENTS.md` for non-trivial work, but treat current code, tests, AGENTS guidance, and
the user's request as authoritative.

When adding an entry, keep it concise and useful: date, task/context, root cause or invariant,
affected owners/files, resolved approach, and verification. Do not record routine edits, secrets,
private user data, guesses, or unresolved issues. Prefer updating a related existing entry over
adding a duplicate.

## 2026-06-29 - ImGui Viewport Layouts Require MultiPane Public Workspace

Task/context: the ImGui shell layout menu and viewport panel still contained the restored multi-pane
draw path, but choosing any layout other than Single stayed single-pane.

Root cause/invariant:

- The earlier ImGui path owned `ViewportPanel::setViewportLayout()` directly and rendered
  placements from `ViewportLayoutState`.
- After `ViewportWorkspaceService` became the viewport owner, `QuaderViewportApi` was left
  constructing its workspace with `ViewportWorkspaceLayoutPolicy::SinglePaneOnly`, a policy from the
  temporary single-pane host. The current ImGui menu routes through
  `editor.viewports().setLayout(...)`, so the API rejected multi-pane before `ViewportPanel` could
  draw the restored placements.
- Keep `SinglePaneOnly` available only for hosts that actually expose one embedded viewport surface;
  the active native ImGui editor API workspace must be `MultiPane`.

Affected owners/files:

- `src/api/quader_viewport_api.cpp`
- `src/viewport/viewport_workspace_service.hpp`
- `src/ui/main_menu_bar.cpp`
- `src/ui/viewport_panel.cpp`
- `tests/editor_api_tests.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Changed the public/editor viewport API workspace default back to `ViewportWorkspaceLayoutPolicy::MultiPane`.
- Kept layout state in `ViewportWorkspaceService`; `MainMenuBar` continues to call the public API,
  and `ViewportPanel` continues to consume `activePanePlacements()` and `activeSplitHandles()` from
  the workspace.
- Updated the public API viewport test expectation from rejected FourQuad/single-pane to accepted
  FourQuad/four active panes, and added a focused build guardrail for the active ImGui layout route.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace`
- Older affected `quader_tests` verification was previously blocked by stale test sources; the
  native UI cleanup removed those stale backend references.

## 2026-06-29 - Win32 Resize State And Window Placement Persistence

Task/context: nightly window slice B tightened borderless Win32 resize behavior and added persisted
native window placement.

Root cause/invariants:

- Borderless Win32 resize hit testing must use the current DPI-scaled system frame and padded-border
  metrics, not only the fixed fallback hit band.
- Native resize state must clear on normal exit and on cancellation paths such as `WM_CANCELMODE`,
  `WM_CAPTURECHANGED`, focus loss, and destruction; otherwise viewport resize suspension can stay
  active after Windows aborts the modal resize.
- Persist the normal window bounds separately from maximized state. When restoring, recover the
  normal frame into a visible display work area before applying maximize.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_settings.cpp`
- `tests/build_guardrail_tests.cpp`
- `tests/core_tests.cpp`

Resolved approach:

- Added `PlatformWindowPlacement`, native `GetWindowPlacement`/`SetWindowPlacement` use on Win32,
  display-work-area recovery, DPI/system-metric resize borders, duplicate live-resize frame
  coalescing, and forced final redraws on resize end/cancel/maximize/restore.
- Added app-owned `window_settings.ini` serialization for normal bounds and maximized state, while
  moving the shared product config directory lookup into `quaderUserConfigDirectory`.
- Added focused guardrail coverage plus behavior tests for settings round-trip, resize border
  metrics, and offscreen recovery.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- Older `quader_tests` verification was previously blocked by stale test sources; the native UI
  cleanup removed those stale backend references.

## 2026-06-28 - ImGui Borderless Resize Must Redraw During Native Live Resize

Task/context: the `feature/imgui-shell` branch restored custom borderless chrome with ImGui. Window
resize had two regressions: growing from a small window lagged or snapped on mouse-up, while
shrinking later clipped the UI until mouse-up.

Root cause/invariants:

- On Windows, native move/resize runs a modal loop where normal `SDL_PollEvent()` rendering can
  stall. Growing needs redraws for newly exposed pixels; shrinking needs redraws from native size
  messages even when no expose event is generated.
- Do not drive live borderless resize with per-motion `SDL_SetWindowPosition` plus
  `SDL_SetWindowSize` on Windows. Use native hit testing (`WM_NCHITTEST`) so Windows owns resize
  geometry, and keep the SDL size path only as fallback.
- `SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION)` is wrong for this path: it drops live resize input and
  couples resize progress to render cadence.
- During live resize, avoid exact viewport render-target reallocation on every pixel; keep drawing
  the current viewport texture and exact-resize again when the resize loop ends.

Affected owners/files:

- `src/app/app.cpp`
- `src/app/app.hpp`
- `src/platform/platform_window.cpp`
- `src/platform/platform_window.hpp`
- `src/ui/ui_context.hpp`
- `src/ui/ui_system.cpp`
- `src/ui/ui_system.hpp`
- `src/ui/viewport_panel.cpp`

Resolved approach:

- Added one guarded `App::renderFrame()` path used by both the normal loop and live resize redraws.
- Added `SDL_AddEventWatch` handling for `SDL_EVENT_WINDOW_EXPOSED` with `data1 == 1` so growing
  windows repaint during SDL/Windows live resize.
- Enabled Win32 `WM_NCHITTEST` for resize edges and the draggable titlebar (`HT*` and `HTCAPTION`)
  so native Windows resize owns live geometry.
- Added a platform live-resize callback from `WM_SIZE`, `WM_WINDOWPOSCHANGED`, and
  `WM_EXITSIZEMOVE` so shrinking windows repaint during the native modal resize loop.
- Added `PlatformWindow::chromeResizeActive()` and UI render-resize suspension so viewport render
  targets are not reallocated every pixel while the chrome resize is active.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- Manual smoke in `build\vs2026\Debug\quader.exe`: grow from small window and shrink from large
  window both repaint during drag instead of lagging, clipping, or snapping only on mouse-up.

## 2026-06-28 - ImGui Shell Flush Viewport, Visible Checkboxes, And Work-Area Maximize

Task/context: the ImGui shell had visible gutters around docked viewport/panels, clipped viewport
combo labels, invisible unchecked checkboxes, mismatched checked checkbox colors, and a black strip
near the Windows 11 taskbar when maximized.

Root cause/invariants:

- The ImGui dockspace host must push zero `WindowPadding` before `DockSpace`; otherwise the dock node
  inherits the global 8px window padding and leaves viewport/panel gutters, including a bottom strip.
- Viewport chrome combo widths must be measured from localized labels with the active frame padding,
  not fixed constants such as 112/104px.
- Global ImGui checkbox/input frames need a visible 1px border and a frame background distinct from
  the panel background; checked ticks should be neutral/light on a coherent dark frame, not a purple
  tick on a blue/purple active frame.
- Borderless Win32 maximize must handle `WM_GETMINMAXINFO` and `WM_NCCALCSIZE`: the maximize bounds
  come from the monitor `rcWork`, and the maximized client area must be computed during non-client
  sizing so drawable content never extends behind the taskbar.
- Do not fix borderless maximize by calling `SetWindowPos` after `ShowWindow(SW_MAXIMIZE)` or from
  `WM_WINDOWPOSCHANGED`; it fights the native maximize transaction, can flicker, and can fail on
  later maximize/restore cycles.
- A fixed shell bottom bar must be a separate viewport-aligned ImGui window and the dockspace host
  height must reserve its height. Do not draw it as content inside the dockspace host with `Dummy`,
  because that creates whole-app scrolling and gaps between panels and the bar.

Affected owners/files:

- `src/ui/ui_system.cpp`
- `src/ui/viewport_panel.cpp`
- `src/ui/ui_style_tokens.cpp`
- `src/platform/platform_window.cpp`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added local zero-padding style scope around the main ImGui dockspace host.
- Replaced fixed viewport combo widths with measured max widths across localized view/shading labels.
- Set `FrameBorderSize = 1`, raised frame border/background contrast, and changed checkmarks to a
  near-white color.
- Added Win32 `WM_GETMINMAXINFO` handling for borderless chrome maximized work-area sizing.
- Added Win32 `WM_NCCALCSIZE` handling, matching the Electron/Chromium pattern, so the maximized
  frameless client area is the monitor work area instead of a later-corrected outer frame.
- Added a fixed static ImGui bottom bar for visual maximize/taskbar verification, with the dockspace
  host reserving the bar height and disabling host scrolling.
- Added focused guardrails for the active ImGui shell/style invariants and work-area maximize path.

Verification:

- `cmake --build --preset vs2026-debug --target quader`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea`
- Native Win32 smoke: launch `build\vs2026\Debug\quader.exe --borderless-window`, run four
  `ShowWindow(SW_MAXIMIZE)` / `ShowWindow(SW_RESTORE)` cycles, and verify the screen-space client
  rect stays inside `MONITORINFO.rcWork` on every maximize.
- Screenshot/pixel smoke: maximized client rect equals `MONITORINFO.rcWork`, the bottom bar accent
  is visible at the client bottom, and no whole-app scrollbar or gap appears above the bar.

## 2026-06-28 - Undo Cancels Active Tool Previews Before Document History

Task/context: Ctrl+Z during an active modeling preview could consume document history first, making
the object disappear; finalized preview operations such as Slice Quad could undo only to the
post-preview selected faces instead of the pre-operation mesh and selection.

Root cause/invariants:

- Public undo is a command/history intent, but the API mutation boundary must first cancel active
  runtime ownership: operation previews, transform previews/drags, scene transform previews, and
  modal viewport tools. Command registration should still ask the history API for availability.
- Preview commit history must capture the preview transaction base state as the undo "before"
  memento. Capturing after the preview has already mutated the live session records the wrong
  boundary and makes undo restore the committed preview state.
- Texture/material identity must survive preview cancel, finalized topology undo/redo, and
  delete/restore stacks.

Affected owners/files:

- `src/api/quader_api_mutation_runner.*`
- `src/api/quader_history_api.cpp`
- `src/api/quader_commands_api.cpp`
- `src/editor/history/modeling_memento_command.hpp`
- `src/editor/history/modeling_preview_command_service.hpp`
- `src/editor/modeling/runtime/session.*`
- `src/editor/modeling/services/modeling_session_service.*`
- `tests/editor_api_tests.cpp`

Resolved approach:

- Added active-preview/tool cancellation to `QuaderApiMutationRunner::undo()` and surfaced the same
  decision through `QuaderApiMutationRunner::canUndo()` so Ctrl+Z is enabled while a runtime cancel
  is possible without adding split modeling-history routes in command registration.
- Exposed the active preview transaction history state through the modeling session service and let
  preview/transform commit commands override their before-memento capture with that base state.
- Added public query accessors for material and texture assets so behavior tests can assert texture
  preservation through history stacks.
- Added focused editor API regressions for bevel-preview undo cancel, Slice Quad finalized undo, and
  a mixed undo/redo stack covering texture assignment, preview cancel, slice undo/redo, delete,
  redo, and restore.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests quader_guardrail_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=EditorApiTests.HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory:EditorApiTests.HistoryUndoAfterSliceQuadFinalizeRestoresPreOperationMeshAndSelection:EditorApiTests.MixedUndoRedoPreviewStackPreservesTexturedMaterial`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=*:ProgrammaticApiGuardrailTests.*`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=*:EditorApiTests.*:EditorApiTests.*History*:EditorApiTests.*Modeling*:EditorApiTests.*Selection*:GroupingPublicApiTests.*:ModelingSessionService*:ModelingToolCatalogTests.*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:SelectionModeAdapterTests.*`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no --gtest_filter=*`
- `build\vs2026\Debug\quader_modeling_domain_metadata_loop_tests.exe --gtest_color=no --gtest_filter=QdrEditorMetadata*`
- `build\vs2026\Debug\quader_modeling_domain_runtime_loop_tests.exe --gtest_color=no --gtest_filter=QdrModelingRuntime*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Modeling Tests Are GoogleTest-Only

Task/context: the modeling-domain test harness was migrated permanently to GoogleTest and the old
test framework was removed as a CMake and third-party dependency.

Root cause/invariant:

- Native tests must use GoogleTest only. Do not add parallel test frameworks or fallback source-fetch
  policy for modeling-domain tests.
- App/runtime behavior tests live in `quader_tests`, source-policy scans live in
  `quader_guardrail_tests`, and modeling-domain tests are one aggregate executable,
  `quader_modeling_domain_tests`, registered through explicit categorized source lists in
  `cmake/QuaderModeling.cmake`.
- Project-owned files must not reintroduce retired test-framework include paths, macros, dependency
  options, source-policy docs, or license evidence.

Affected owners/files:

- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `tests/modeling_domain/**`
- `tests/build_guardrail_tests.cpp`
- `agents/testing-build.md`
- `agents/dependencies.md`
- `docs/absorbed_modeling/README.md`
- `thirdparty/THIRD_PARTY_MANIFEST.md`

Resolved approach:

- Removed the old dependency option, commit pin, source-dir cache variable, FetchContent path, and
  test-support compile definition.
- Rewired `quader_modeling_domain_tests` to `GTest::gtest` and `GTest::gtest_main`.
- Converted modeling-domain test sources to GoogleTest syntax and moved shared approximate
  comparison support into `tests/modeling_domain/test_support.hpp`.
- Removed old third-party manifest/license entries and added guardrails that scan project-owned
  files for retired test-framework tokens.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `build\vs2026\Debug\quader_modeling_domain_tests.exe --gtest_color=no`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:DependencyLicenseGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Source-Wire Overlays, Flipped Mesh Picking, And Inverted Face Fill

Task/context: source-wire and selection overlays had a long-running set of regressions: large wire
gaps, fading or missing edges at distance/angles, hidden internal source-wire edges on inverted
meshes, hover/deselect colors failing for inverted internal edges, occluded internal handles being
pickable from outside, and face-fill hover/selection not appearing on inverted faces.

Root cause/invariants:

- Source-wire topology is editor interaction geometry and should render as crisp draw-on-top lines.
  `SourceWireDepthStamp` is metadata only and must not render or delete source-wire line topology.
- Picking and rendering are separate concerns. Render extraction should not CPU-filter source-wire
  edges to solve pickability; source-wire interaction is accepted or rejected by picking/hover
  occlusion policy.
- Editor picking needs nearest visible two-sided surface behavior for flipped/backfacing meshes:
  backfaces are pickable, but authored front-facing faces must not beat nearer visible backfaces.
- Selected/hover face-fill overlays need two-sided triangle render state so inverted faces show fill;
  duplicating reversed geometry is not the owner fix.

Affected owners/files:

- `src/editor/modeling/runtime/session_picking.cpp`
- `src/mesh/polygon/document.hpp`
- `src/mesh/polygon/quader_poly_document_picking_overlay.cpp`
- `src/editor/projection/picking_cache_projection.cpp`
- `src/editor/modeling/runtime/session_selection_overlay.cpp`
- `src/render/viewport_overlay_policy.cpp`
- `src/render/overlay/overlay_renderable_builder.cpp`
- `src/render/overlay/overlay_renderable_builder.hpp`
- `src/render/overlay/overlay_face_fill_builder.cpp`
- `tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp`
- `tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp`
- `tests/render_extraction_tests.cpp`
- `tests/core_tests.cpp`
- `docs/overlay-handling.md`
- `agents/rendering-overlays.md`

Resolved approach:

- Kept `SourceWire` draw-on-top and kept `SourceWireDepthStamp` non-rendered.
- Added `PickOptions::prefer_front_facing_faces`, preserving the old low-level default while opting
  editor picking into nearest-surface behavior.
- Included the new pick option in face-geometry cache signatures.
- Routed editor hover/pick paths through one shared editor surface pick policy.
- Added modeling-domain tests for outside-vs-inside picking on flipped geometry and source-wire
  occlusion.
- Made selected and hover face-fill overlay layers two-sided in `ViewportOverlayPolicy`, preserving
  the existing depth-stamped two-pass fill pipeline.
- Added render extraction and policy tests asserting selected/hover face fills use no triangle
  culling and two-sided transparency.
- Added `docs/overlay-handling.md` and explicit overlay status tests for the final object-mode
  x-ray, component-mode source-wire, source-wire depth-stamp, inside-out visibility, and
  outside-vs-inside picking contracts.

Verification:

- `cmake --build --preset vs2026-debug --target quader_tests`
- `ctest --preset vs2026-debug --output-on-failure`
- `cmake --build --preset vs2026-debug --target quader`

## 2026-06-27 - Project-Owned C++ File Naming Invariant

Task/context: the native app source tree had mixed PascalCase and snake_case C++ filenames, plus
project headers using mixed extensions. The repository now standardizes project-owned C++ filenames
so future refactors, CMake entries, includes, and tests have one naming convention.

Invariant:

- Project-owned C++ files under `src`, `tests`, `examples`, `benchmarks`, and `tools` use
  snake_case basenames only.
- Project-owned C++ source/header extensions are only `.cpp` and `.hpp`.
- Third-party/vendor headers keep their upstream names and extensions.

Affected guardrails:

- `tests/build_guardrail_tests.cpp` enforces snake_case `.cpp`/`.hpp` project filenames, rejects
  stale PascalCase or old-extension project path references, rejects broken extension rewrite
  artifacts, and rejects temporary rename files.
- `tests/support/guardrail_file_scan.cpp`, `tests/programmatic_api_guardrail_tests.cpp`, and
  `tests/oop_architecture_guardrail_tests.cpp` scan `.hpp` project headers instead of old header
  extensions.

Resolved approach:

- Renamed project-owned C++ files to snake_case and changed project headers to `.hpp`.
- Updated CMake, docs, examples, source includes, and tests to the new paths.
- Repaired mechanical rewrite fallout in member names such as `history_status`, `has_selection`,
  `has_projection`, and `hard_normal_edges`.
- Restored a mechanically rewritten external vendor include path during the header-extension rename.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_tests`
- `cmake --build --preset vs2026-debug --target quader_modeling_domain_tests`
- `cmake --build --preset vs2026-debug --target quader`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.*:OopArchitectureGuardrailTests.*:ProgrammaticApiGuardrailTests.*`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Affected Build/Test Workflow

Task/context: prototype loops were paying the full app/test cost for narrow source changes. The
fast path now uses an affected workflow before full verification, while still printing a concrete
before -> after timing when requested.

Invariant:

- Use `tools/affected_check.ps1` for scoped build/test checks during prototyping and agent loops.
- Pass explicit comma-separated `-ChangedPath` values in dirty worktrees so the plan reflects the
  intended edit, not unrelated local changes.
- Route pure source-policy scans through `quader_guardrail_tests`; do not force `quader_tests` to
  compile/link for docs, tooling, CMake policy, or API/source-boundary guardrails.
- Modeling-domain loop targets are developer-only `EXCLUDE_FROM_ALL` executables used by the
  affected workflow. Do not register them with CTest; full verification stays on the aggregate
  `quader_modeling_domain_tests` entry.
- Use `-MeasureFull` when a task needs a concrete before -> after comparison.
- Use `-IncludeApp` when manual in-app verification or runtime resource copying is part of the
  change.

Affected owners/files:

- `tools/affected_check.ps1`
- `CMakeLists.txt`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added path-based target and GoogleTest filter planning for render, UI, viewport, modeling,
  polygon/geometry, QDR, resources, docs, tools, CMake, and direct test-source edits.
- Split pure source-policy scans into one lightweight `quader_guardrail_tests` executable so docs
  and tooling changes do not pay the app/runtime test link cost.
- Kept the workflow in the existing `build/vs2026` tree and added one lightweight guardrail
  executable plus three modeling-domain loop executables instead of per-test artifacts or extra
  artifact folders.
- Split modeling-domain test sources into shared category object buckets and assembled developer
  loops for polygon/document, runtime/QDR runtime, and editor metadata checks while preserving the
  aggregate `quader_modeling_domain_tests` CTest target.
- Added a guardrail so the tool keeps printing the full baseline, affected workflow, filtered
  GoogleTest commands, and before -> after comparison support.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath src/render/overlay/overlay_renderable_builder.cpp -MeasureFull`
- Render-overlay sample timing: full baseline 51.36s -> affected workflow 4.22s, 47.14s saved.
- Tooling-only sample after the guardrail split and filter tightening: full baseline 54.03s ->
  affected workflow 2.43s, 51.60s saved.
- Runtime modeling test-source sample after loop targets:
  `tests/modeling_domain/operation/qdr_modeling_runtime_operation_command_tests.cpp`, full baseline
  52.13s -> affected workflow 3.24s, 48.89s saved.
- Polygon operation source sample after operation-family filters:
  `src/mesh/polygon/operations/bevel_edges_operation.cpp`, full baseline 46.86s -> affected
  workflow 3.08s, 43.78s saved.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\affected_check.ps1 -NoUntracked -ChangedPath tools/affected_check.ps1,agents/testing-build.md,tests/build_guardrail_tests.cpp`
- `ctest --preset vs2026-debug --output-on-failure`

## 2026-06-27 - Target-Local Standard-Library PCH

Task/context: manual in-app prototype builds still paid repeated C++ standard-library parse cost in
large runtime/modeling targets after affected checks selected the right target.

Invariant:

- `QUADER_ENABLE_PCH` owns the opt-out switch in `cmake/QuaderBuildOptions.cmake`.
- Common PCH is private and target-local. Do not publish PCH through target interfaces.
- Keep the common PCH list to stable C++ standard-library headers. Do not add project owner
  headers, Filament/SDL SDK headers, or GoogleTest without a separate measured change.

Affected owners/files:

- `CMakeLists.txt`
- `cmake/QuaderBuildOptions.cmake`
- `cmake/QuaderModeling.cmake`
- `agents/testing-build.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added `quader_enable_common_pch(target)` and applied it to app/runtime/modeling libraries, app
  tests, guardrail tests, and modeling-domain test object libraries.
- Kept PCH behind `QUADER_ENABLE_PCH=ON` so a developer can disable it for local diagnosis.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader`
- Runtime app incremental sample after PCH warm-up: touching
  `src/render/viewport_renderer.cpp`, `quader` build 7.87s -> 5.72s, 2.15s saved.

## 2026-06-27 - Optional Ninja Multi-Config Prototype Build

Task/context: after target-local PCH, Visual Studio/MSBuild still spent avoidable time in warm
incremental app rebuilds used for manual in-app prototyping.

Invariant:

- Keep `vs2026` and `vs2026-debug` as the default expected presets.
- Optional Ninja/MSVC acceleration must use a single contained tree under `build/ninja-msvc`.
- Do not hardcode local Visual Studio or compiler paths in presets. Ninja/MSVC runs from a Visual
  Studio Developer PowerShell/CMD, or an equivalent environment where `cl.exe` is already on `PATH`.
- `tools/affected_check.ps1` derives focused test executable paths from the selected configure
  preset `binaryDir`; it must not assume `build/vs2026`.

Affected owners/files:

- `CMakePresets.json`
- `tools/affected_check.ps1`
- `agents/testing-build.md`
- `cmake/AGENTS.md`
- `tests/build_guardrail_tests.cpp`

Resolved approach:

- Added optional `ninja-msvc`, `ninja-msvc-debug`, and `ninja-msvc-release` presets.
- Updated affected-check preset resolution so passing `-BuildPreset ninja-msvc-debug` also derives
  `ninja-msvc` and `Debug` when those parameters are not supplied explicitly.
- Added guardrails for Ninja preset presence, no local compiler paths, and no hardcoded focused
  test executable dir.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug --target quader_guardrail_tests`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.BuildOptionsAndDependencyPinsHaveSingleCMakeOwner:BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Through `VsDevCmd.bat`: `cmake --preset ninja-msvc`
- Through `VsDevCmd.bat`: `cmake --build --preset ninja-msvc-debug --target quader_guardrail_tests`
- Through `VsDevCmd.bat`: `build\ninja-msvc\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.FormerSdkBoundaryIsAbsorbedIntoAppBuild:BuildGuardrailTests.LocalMachinePathsUseEnvironmentBackedDefaults:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns`
- Runtime app warm incremental sample after the Ninja tree was populated: touching
  `src/render/viewport_renderer.cpp`, `vs2026-debug` `quader` build 7.90s ->
  `ninja-msvc-debug` `quader` build 2.98s, 4.92s saved. The first-time Ninja tree build is not
  included in this warm prototyping comparison.

## 2026-06-29 - Native Window Shell, Layout Restore, And Stable Entity Identity

Task/context: native standalone app needed Windows 11 borderless maximize fixes, persisted window
placement, restored multi-pane viewport layouts, UI chrome/sidebar polish, RMB context selection
preservation, literal default group naming, stable entity UUIDs, and removal of the retired retained
UI stack references.

Invariant:

- Borderless maximize is owned by `PlatformWindow` on Windows. Keep `WM_GETMINMAXINFO` clamped to
  the monitor work area and `WM_NCCALCSIZE` constrained for zoomed client bounds so the app does not
  cover the Windows taskbar.
- Persisted window placement is owned by `AppWindowSettings`; recover saved normal bounds to a
  visible display work area before applying them.
- The native editor uses the ImGui shell only. Do not reintroduce generated UI controller/resource
  paths or stale retained-UI documentation.
- Viewport layout restoration belongs to `ViewportWorkspaceService` with the standalone app using
  `ViewportWorkspaceLayoutPolicy::MultiPane`.
- RMB context picking with a valid component selection must preserve the existing component
  selection when the hit is on another unselected component, then open the context menu against that
  preserved selection.
- New groups default to the literal name `Group`.
- Every scene entity has a stable UUID component; QDR I/O and public snapshots preserve and expose
  it, while duplicated entities receive distinct UUIDs and undo/redo restores created identities.

Affected owners/files:

- `src/platform/platform_window.*`
- `src/app/app_window_settings.*`
- `src/core/user_config_paths.*`
- `src/ui/ui_system.*`, `src/ui/main_menu_bar.*`, `src/ui/viewport_panel.cpp`,
  `src/ui/inspector_panel.*`, `src/ui/outliner_panel.*`
- `src/ui/viewport_selection_controller.*`, `src/ui/viewport_shortcut_controller.cpp`
- `src/viewport/viewport_workspace_service.hpp`, `src/api/quader_viewport_api.cpp`
- `src/scene/scene_document.*`, `src/scene/components.hpp`, `src/io/qdr/qdr_document_*`
- `src/editor/commands/document/*`
- UI/docs guardrails and AGENTS/docs files.

Resolved approach:

- Matched Electron's Windows frameless-window lesson locally: work-area max bounds plus client-area
  insets/constraints for maximized borderless windows, without relying on one-time resize hacks.
- Added placement persistence under the user config directory.
- Restored multi-pane workspace policy for the standalone app.
- Made the custom chrome 35 px, moved the menu beside the logo, removed the old menu strip, kept a
  static bottom bar for maximize verification, and tightened sidebar/dropdown/checkbox sizing and
  colors.
- Added `stduuid` through vcpkg/CMake/license manifest and stored UUIDs on scene entities and QDR.
- Replaced stale docs with current native ImGui/app guidance and removed explicit retired UI stack
  references.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `build\vs2026\Debug\quader_tests.exe --gtest_color=no --gtest_filter=UISettingsTests.*:AppWindowSettingsTests.*:PlatformWindowChromeTests.*:SceneDocumentTests.CreatesStableEntitiesAndComponents:DocumentCommandTests.TypedLifecycleCommandsRestoreSceneSnapshots:DocumentCommandTests.DuplicateMintsEditorObjectIdsOnceAndRedoReusesThem:DocumentCommandTests.DestroyUndoRestoresEditorObjectIdsForSubtrees:DocumentCommandServiceTests.DefaultGroupNameStaysLiteralGroup:DocumentCommandServiceTests.CoversEntityHierarchySelectionAndLifecycleCommands:QdrDocumentIoTests.WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings:EditorApiTests.ViewportHandlesUseWorkspaceWithoutRuntimeHooks:EditorApiTests.ViewportSelectionModeShortcutsApplyThroughPublicSelectionApi:EditorApiTests.RightClickPickWithExistingFaceSelectionPreservesComponentSelectionBeforeContextDelete:ModelingSessionServiceTests.RmbMeshPickWithExistingFaceSelectionPreservesSelectionAndAllowsContextMenu`
- `build\vs2026\Debug\quader_guardrail_tests.exe --gtest_color=no --gtest_filter=BuildGuardrailTests.ProjectOwnedTextDoesNotReferenceOldCppPathNames:BuildGuardrailTests.SourceBuildAndTestsDoNotUseOldSdkIncludeOrPackagePaths:BuildGuardrailTests.TestingDocsExplainPlacementAndSelectionRules:BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiRuntimeResourcesArePackagedWithRuntime:BuildGuardrailTests.ImGuiShellViewportAndControlStylingStayVisible:BuildGuardrailTests.ImGuiViewportApiKeepsMultiPaneLayoutWorkspace:BuildGuardrailTests.RuntimeUiUsesNativeImGuiStackOnly:WindowGuardrailTests.BorderlessWindowModeIsDefault:WindowGuardrailTests.BorderlessMaximizeUsesMonitorWorkArea:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned:DependencyLicenseGuardrailTests.*:ProgrammaticApiGuardrailTests.PublicBehaviorTestsUseApiOrScenarioDsl:ProgrammaticApiGuardrailTests.PublicDocsExamplesManifestClassifiesEveryDocsAndExamplesFile:SelectionModeAdapterGuardrailTests.*:ViewportToolGuardrailTests.ViewportPanelProvidesPointToolContextToModalTools`
- Explicit retired UI stack string search over source/docs returned no matches outside ignored build
  and third-party/license folders.

## 2026-06-29 - Daily DRY/OOP Drift Audit Completion

Task/context: exhaustive daily DRY/OOP drift audit across API/commands, modeling/projection,
viewport/input/tools, UI/layout/localization, render/assets/scene, QDR I/O, docs/build/tests, with
six parallel audit subagents and main repo-wide inspection.

Invariant:

- `refactor.md` is the current-run source of truth and must not contain unchecked actionable items
  before completion.
- Inspector one-shot transform edits must route through `SceneTransformDragService`; `InspectorPanel`
  may stage UI field values but must not build `QuaderSceneTransformEdit` batches, compute selected
  transform pivots, filter locked/selected-descendant targets, or pin local pivots itself.
- Viewport shading conversion helpers that include viewport owners live under `src/viewport`, not
  public API headers.
- Modeling selection, drawn-plane lifecycle, overlay/projection, scene render extraction, material
  texture mutation, QDR root/header/project codecs, scene-batch reparenting, and docs/build
  guardrail policy each have one owner recorded in `refactor.md`.

Resolved approach:

- Completed all actionable `refactor.md` items A01-A09, U01-U07, M01-M05, R01-R05, and T01-T07.
- Final U07 fix moved inspector-style move/rotate/scale commits into `SceneTransformDragService`,
  including selected-target filtering, selected-bounds pivot resolution, scale ratio/clamp policy,
  parent-aware pivot pinning, and public API batch commit. `InspectorPanel` now only calls the
  transform owner.
- Moved `quaderApiToViewportShadingMode`/`quaderApiFromViewportShadingMode` to
  `src/viewport/viewport_shading_mode_conversions.hpp` after the public API guardrail caught the
  internal viewport include under `src/api`.

Verification:

- `cmake --preset vs2026`
- `cmake --build --preset vs2026-debug`
- `ctest --preset vs2026-debug`
- `powershell -ExecutionPolicy Bypass -File tools\boot_smoke.ps1 -Exe build\vs2026\Debug\quader.exe -ScreenshotPath build\vs2026\Debug\daily-dry-oop-boot-smoke.png -LogCopyPath build\vs2026\Debug\daily-dry-oop-boot-smoke.log -TimeoutSeconds 30`
