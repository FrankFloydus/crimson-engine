<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Agent Guide

This file is the self-contained authority for agents working in this repository. Do not look for
separate top-level instruction or documentation folders; those folders are not part of the current
source-of-truth model. Read this file first, then read the nearest nested `AGENTS.md` under the
specific subtree being edited.

## Project Overview

- Quader is a native standalone Windows polygon modeling editor written in C++ with CMake and vcpkg.
- The app shell uses Qt Widgets, Filament, native Windows integration, QDR project/document IO, a
  modeling runtime, a scene/asset document model, viewport tools, public editor APIs, examples,
  and GoogleTest-based headless verification.
- The current product remains the native app. Its app-owned portable modeling core under
  `src/sdk/core/modeling` is the modeling source of truth, with native host systems adapting
  around it.
- Do not preserve the old top-level copied SDK boundary. Do not restore old SDK package names,
  old SDK CMake presets, `QUADER_MODELING_SDK_DIR`, old compile-time SDK presence macro checks, or
  conditional app code that routes around missing first-party SDK source.
- Former SDK geometry, polygon, editor-core, host-contract, and QDR behavior is app-owned source
  today. The SDK boundary is normal first-party source inside this repo, not a resurrected
  external checkout.

## Architecture Direction

Quader follows one deterministic direction:

1. The native Windows app remains the shipping editor.
2. The portable modeling core is the modeling source of truth.
3. The portable core lives at `src/sdk/core/modeling`.
4. The public SDK namespace is `quader::modeling`.
5. The native editor APIs remain native host APIs.
6. Host systems adapt input, UI, render, scene, asset, platform, and project persistence around
   SDK-owned modeling state.
7. Old copied SDK structures stay banned.

The portable modeling core owns mesh documents, polygon operations, operation results, typed IDs,
selection remapping, validation, portable QDR document IO, modeling sessions, preview transactions,
undo/redo for modeling workflows, dirty flags, and renderer-neutral snapshots. Host adapters own
Qt Widgets, Filament, Win32, native file dialogs, project sessions, app scene entities, material
assets, command palette routing, viewport panels, and platform windows.

Use `architecture-review.md` for the fixed migration direction and completed ownership decisions.
The linked root `REFACTORING_PLAN.md` is the only executable refactoring tracker and sole execution
record for the completed Phase 0-6 refactor; new implementation work requires an explicitly
approved follow-up task or successor plan. Do not treat historical phase numbers or closed audit
ledgers as current task authority.

## Domain Ownership

- `src/api`: Native public editor API. `QuaderEditorApi` is the default whole-editor API for user
  and editor intent. `QuaderApi` covers document/modeling workflows that do not need runtime
  editor domains. These APIs stay host/editor facades and delegate to lower owners.
- `src/sdk/core/modeling`: Portable modeling core and modeling source of truth. It has no SDL,
  ImGui, Filament, Win32, Qt,
  `SceneDocument`, `ProjectDocument`, `ProjectSession`, `AssetRegistry`, `SceneRenderBridge`,
  `ViewportOverlayFrame`, `QuaderEditorApi`, native command IDs, or app UI dependencies.
- `src/sdk/core/modeling/neutral`: Source-only neutral geometry and polygon declarations shared by
  native owner targets and the SDK implementation. Native code includes
  `quader/modeling/neutral/...`, never SDK `internal` paths; this surface is not installed for
  external SDK consumers.
- `src/editor`: Native editor services, command services, modeling adapters, history integration,
  selection services, metadata catalogs, pivot tools, and runtime-to-scene wiring.
- `src/scene`: Scene document, entities, hierarchy, transforms, components, and scene invariants.
- `src/assets`: Asset registry, asset identities, default material catalogs, and native asset
  ownership.
- `src/io`: Native project persistence and file format adapters. Portable QDR document IO lives at
  the SDK boundary; project hydration, file dialogs, durable native writes, scene wiring, and
  asset registry integration remain native.
- `src/render`: Filament resources, renderer caches, render sync, render snapshots, material
  binding, overlays, viewport rendering, and renderer-owned performance stats.
- `src/viewport`: Toolkit-neutral viewport state, semantic input routing, shortcut resolution,
  viewport camera/navigation, and host viewport contract adapters.
- `src/ui`: Qt Widgets presentation, panels, menus, settings surfaces, localization use, and UI
  input translation. UI does not own modeling, scene, asset, render, or command truth.
- `src/platform`: Windows platform integration, native dialogs, support utilities, and fatal error
  presentation. Qt application and window lifecycle stay in `src/app` and `src/ui`.
- `resources`: Runtime resources, localization, icons, materials, generated Filament assets, and
  packaged UI assets.
- `tests`: GoogleTest coverage and guardrails. Test ownership follows the behavior being protected.
- `examples`: Public examples. Examples start from public APIs or script surfaces, not internal
  owners.
- `cmake`, `tools`, `thirdparty`, `benchmarks`: Build logic, automation, dependencies, and
  performance harnesses.

Treat the first directory under `src/` as a package boundary unless a narrower owner already exists.
Do not cross owner boundaries because a caller lacks a convenience method. Add owner behavior first,
then expose it through the right API.

## Public API Rule

Use the public API when code expresses user/editor intent, such as
`editor.modeling().create_box(...)`, `editor.scene().batch(...)`,
`editor.commands().execute(quader::quader_command::kEditUndo)`, or
`editor.main_viewport().set_shading_mode(...)`.

Prefer typed command IDs from `quader::quader_command` in C++ call sites. Stable string command IDs
remain the script and command-palette representation. Prefer `QuaderObjectHandle` and
`QuaderSceneBatch` for fluent object workflows and grouped scene edits. `QuaderApiResult` and
`QuaderApiVoidResult` use callable helpers such as `ok()`, `changed()`, `value()`, and
`error_message()`.

Add To Public API When:

- a workflow represents real user/editor intent;
- tests, examples, automation, scripts, or UI need a stable user-level action;
- a caller would otherwise bypass command, scene, modeling, selection, history, viewport, or asset
  owners.

Keep Low-Level When:

- code owns hot-path mechanics, topology, render resources, SDK semantics, persistence internals,
  platform state, or native backend details;
- the behavior is not a stable editor/user operation;
- exposing it would leak owner internals into API consumers.

Lua remains an optional scripting surface. Lua bindings must not expose internal owners such as
`SceneDocument`, `AssetRegistry`, `ModelingSessionService`, `SceneRenderBridge`, render caches,
platform windows, or SDK internals.

## SDK Boundary Rules

The portable modeling SDK is first-party source inside this repo. It is not a separate checkout.

The SDK core must:

- build without the native app executable;
- build without Qt, SDL3, ImGui, Filament, Win32 app shell, native dialogs, app resources, and UI
  code;
- expose a single umbrella include rooted at `quader/modeling/modeling.hpp`;
- use `quader::modeling` names;
- expose structured `Result<T>`, structured errors, typed generational IDs, dirty flags,
  operation diagnostics, selection remap data, and renderer-neutral snapshot payloads;
- own portable QDR document IO and neutral mesh views;
- provide SDK consumer tests that link only SDK targets.

The native app must:

- keep `QuaderEditorApi` and `QuaderApi` as native host/editor facades;
- adapt scene entities, assets, materials, command IDs, viewport panels, render bridge calls,
  file dialogs, and platform behaviors around SDK-owned modeling state;
- keep old copied SDK guardrails active for old package paths, old presets, old macros, and old
  checkout wiring.

## Source-Of-Truth And Duplication

Before adding behavior, search for existing implementations, helpers, services, command paths,
projection code, cached state, and tests that already solve the same problem. Prefer routing call
sites to the owner implementation over copying logic into another layer.

Treat these as architecture defects:

- repeated logic that belongs in one helper, service, command, or SDK operation;
- overlapping behavior with different names, ordering, or side effects;
- parallel sources of truth for scene, modeling, viewport, UI, history, asset, render, QDR, or SDK
  state;
- copied cache, projection, conversion, validation, or selection-remap helpers;
- command, selection, transform, hierarchy, asset-validation, or overlay behavior implemented
  differently across public API, UI, services, and low-level owners.

When no clear owner exists, create the narrowest owner in the domain that owns the data and
invariants, then route call sites through it. Add or update guardrail tests when the change prevents
future drift.

## Restore Rule

When the user asks to restore previous working behavior, do not redesign or reinterpret it. Find
the current app behavior, tests, memory entries, or project history that defines it and integrate
that same behavior into the current architecture while preserving ordering, state transitions,
side effects, cache invalidation, and error handling.

## Hard-Resolved Task Memory

Read root `memory.md` for non-trivial or regression-prone work. When resolving a hard-to-rediscover
task, update `memory.md` with the invariant, owners, fix, and verification. Current code, tests,
this file, nested `AGENTS.md` files, and the user request override memory.

## Nested Package Routing

This repo uses nested `AGENTS.md` files for local package guidance. Root policy stays here; nested
files add local ownership and verification notes only.

- For source changes, read `src/AGENTS.md`, then the nearest `src/<package>/AGENTS.md` when present.
- For tests, examples, resources, CMake, tools, benchmarks, third-party, or specialized subtrees,
  read the nearest nested `AGENTS.md`.
- For input, keybindings, shortcuts, shortcut persistence, keybinding contexts, mouse gestures, or
  viewport navigation input, also read `INPUT_KEYBINDINGS.md`.
- For architecture migration work, read `architecture-review.md` for direction and
  `REFACTORING_PLAN.md` for executable task IDs, prerequisites, and progress.
- Do not create new top-level instruction folders. Add concise local guidance in the nearest
  nested `AGENTS.md` only when a subtree needs local rules.

## UI And Frontend Rules

- UI panels present state and dispatch intent; they do not own scene, modeling, render, asset,
  history, or command truth.
- Use existing UI components, localization helpers, settings descriptors, and command routes before
  adding new UI-specific logic.
- Do not put editor mutations directly in panels when a public API, command service, editor
  service, or SDK owner exists.
- Keep Qt Widgets and raw Qt events at the UI boundary. Convert raw events through the Qt input
  adapter into canonical Quader input and semantic viewport frames before lower layers see them.
- The native shell currently initializes Qt `Fusion` with `fusion_style->standardPalette()` as its
  default baseline. This baseline is not a prohibition on styling. Apply style, palette, font,
  icon, or window-chrome changes when the user's current request explicitly directs them; keep
  scoped changes local where practical and verify accessibility plus default, hover, selected,
  pressed, focused, and disabled states in proportion to the change.
- The viewport workspace is one central Qt panel with one to four embedded panes. Each pane hosts a
  native child `QWindow` Filament surface; `ViewportWorkspaceService` remains the state owner.
- Text, buttons, icons, and settings controls must remain scannable, localized, and consistent with
  the existing native editor shell.

## UI Frame-Loop Performance Rule

UI additions must not turn catalogs, command predicates, snapshots, or widget presentation into
per-frame work. Native Qt controls and toolbars are cheap to paint; a large FPS collapse after UI
work almost always means synchronization code was placed in `UISystem::refresh`, a viewport frame
callback, or another continuously called path without an unchanged-state gate.

The recurring failure pattern is:

- calling broad APIs such as `commands().available()`, catalog enumeration, full snapshots, or
  repeated selection summaries every rendered frame;
- allocating descriptor vectors and strings, resolving every shortcut, and executing every
  command availability predicate just to update a few retained controls;
- calling `setText`, `setToolTip`, `setEnabled`, `setChecked`, visibility, geometry, or model-reset
  methods with unchanged values, which can schedule Qt change, layout, accessibility, and paint
  work;
- duplicating an expensive query in several widgets instead of computing one owner snapshot and
  sharing or caching the result.

Required resolution:

- Build static controls once. Synchronize retained controls only on the narrow event or owner
  revision that can change them: project/scene replacement, selection revision, active-tool change,
  command execution, keybinding change, localization change, or explicit settings commit.
- Put the unchanged-state/revision check before descriptor allocation, command availability
  predicates, shortcut lookup, string formatting, and Qt setters.
- When a widget needs one or a small fixed set of commands, use narrow lookup such as
  `commands().descriptor(id)`; never enumerate `commands().available()` from a frame callback.
- Cache the values currently presented and do not call Qt setters when the effective value is
  unchanged. Invalidate the cache explicitly for localization, keybinding, project, and other
  non-revision transitions.
- If polling is unavoidable, query the cheapest owner state once per frame and share it. Do not
  compensate for UI polling by lowering the render cadence, skipping renderer work, or weakening
  input responsiveness.

Verification for UI work that touches a frame callback or runtime synchronization must include:

- a source or behavior regression test proving the expensive work is behind an unchanged-state or
  revision gate and broad catalog enumeration is absent from the hot path;
- a same-build, same-scene before/after frame-time or FPS measurement when a regression is reported;
- the normal focused UI tests and native app build. A visually correct toolbar is not complete if it
  reduces steady-state viewport performance.

## Viewport And Input Rules

- `QuaderKeybindingService`, command IDs, command registry, viewport shortcut descriptors,
  `src/ui/qt_input_adapter.*`, and `ViewportInputRouter` are the source-of-truth map for Quader
  input.
- Treat command lifecycle and default-keybinding lifecycle as one change. Whenever a command is
  added, renamed, or removed, or the user requests a default keybinding change, reconcile the
  command registry and IDs, compiled default mapping, context/scope and conflict behavior,
  Settings catalog and labels, menus/tooltips, persistence compatibility, localization, and tests
  in the same pass. Every new command must deliberately receive a default or be explicitly tested
  and documented as unassigned; a removed or renamed command must leave no stale default, alias,
  catalog row, localization entry, menu shortcut, or user-override target; a changed default must
  preserve existing user overrides unless the request explicitly requires migration.
- Do not bypass these owners with ad hoc key checks in UI panels or tools.
- Viewport navigation and tools consume semantic input frames, not raw Qt events.
- Shortcut contexts remain explicit. Global commands, viewport commands, tool activation, text
  input focus, and navigation capture must not overlap silently.
- For viewport tool changes, cover navigation suppression, hover, selection fallback, gizmo
  consumption, preview lifecycle, and undo/redo when state is inspectable.

## Rendering And Overlay Rules

- Filament resources stay in `src/render`.
- Render code consumes renderer-neutral snapshots and dirty state. It must not mutate scene,
  assets, modeling documents, or SDK state.
- Overlay policy belongs in render-owned overlay policy and builder code. UI and modeling services
  produce semantic overlay records; render decides draw policy and resource allocation.
- Snapshot payloads must have explicit lifetime semantics: owned immutable data, pinned arena data,
  or borrowed same-frame data with documented lifetime.
- Put dirty/revision gates before expensive mesh compilation, topology hashing, snapshot building,
  preview generation, and render cache updates.

## Diagnostics And Fatal Errors

Diagnostics are explicit shared-core diagnostics exceptions.

- `src/core/logger.*` owns Logger setup, logger macros, memory sink access, and shared logging
  policy.
- `src/platform/fatal_error_handler.*` owns fatal runtime hooks and platform fatal presentation.
- `src/main.cpp` installs startup logging and fatal-error handling.
- Fatal-error handling is platform-owned. UI and app services do not install runtime hooks,
  create global loggers, or own crash presentation.

## Dependencies

- Use vcpkg manifest mode and first-party vendored third-party evidence under `thirdparty/`.
- Keep network dependency fetches opt-in and backed by license evidence.
- Do not add a dependency for small parsing, formatting, math, or utility work already covered by
  the standard library or existing project dependencies.
- Before changing dependencies, update manifests, license files, third-party manifest evidence,
  CMake wiring, guardrails, and verification commands together.
- SDK targets must not inherit native app dependencies by broad include directories or transitive
  app targets.

## Worktree Rule

The worktree may already be dirty. Do not revert changes you did not make unless the user
explicitly asks. Work with existing edits when they affect the task and ignore unrelated edits.

Never use destructive git commands such as `git reset --hard` or `git checkout --` unless the user
clearly requests that exact operation.

## Commit And Push Scope Rule

When the user says "commit and push" without explicitly limiting the scope, stage, commit, and push
everything in the current worktree, including modified files, deleted files, and untracked files.
Do not default to committing only task-owned changes. A partial commit is appropriate only when the
user explicitly names the files, changes, or scope to include or exclude.

## Versioning Rule

Root `VERSION` is the app version source of truth, and `vcpkg.json` `version-string` must mirror it.
Use unpadded `x.y.z` numeric format; the app displays it as `vX.Y.Z`.

Preserve the current app version by default. Do not infer a version bump from project-file changes,
final handoff, a build, commit, or push. Change the version only when the user's current request
explicitly asks for a version bump; otherwise leave both `VERSION` and `vcpkg.json` unchanged.

When a bump is explicitly requested, update both version sources once before the final handoff
`quader` build. Starting from the explicit `0.1.0` baseline, keep major/minor fixed at `0.1` and
bump only the patch/build component unless the user requests a different version. Patch must stay
within `0..9999`; if it is already `9999`, stop and ask before rolling minor or major.

Do not bump for planning, review, inspection-only, failed/no-change, or explanation-only turns.

## Fast Iteration Mode

Only when the user's current request explicitly asks for `fast iteration mode`, skip automated
tests and affected-check runs, implement the requested changes, and build the native `quader`
executable as quickly as possible for manual testing. Still report build failures and do not claim
untested behavior as verified. This exception applies only to the request that names the mode; when
the user does not explicitly request it, follow the normal testing and verification workflow in
this guide.

## Expected Commands

When launching build or test commands through Codex tools, set timeouts to at least 10 minutes
(`600000` ms). Short defaults are unreliable for this project.

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
cmake --build --preset vs2026-debug --target quader
ctest --preset vs2026-debug
```

Whenever a task changes any project file, final verification must include building the native app
executable:

```powershell
cmake --build --preset vs2026-debug --target quader
```

Run the C++ style gate only before commit/push, or when the user explicitly asks for clang-tidy or
a C++ style audit:

```powershell
python tools/check_cpp_style.py
```

For local prototyping, the optional Ninja Multi-Config path keeps artifacts in `build/ninja-msvc`.
Run it from a Visual Studio Developer PowerShell/CMD so `cl.exe` is on `PATH`.

```powershell
cmake --preset ninja-msvc
cmake --build --preset ninja-msvc-debug --target quader
ctest --preset ninja-msvc-debug
```

## Test Placement

Choose the test owner from the behavior being protected, not from the file that happened to change.

- Put source-policy, architecture, dependency, naming, CMake, tooling, and instruction invariants
  in `tests/*guardrail_tests.cpp`, usually `tests/build_guardrail_tests.cpp`,
  `tests/programmatic_api_guardrail_tests.cpp`, or `tests/oop_architecture_guardrail_tests.cpp`.
  These build in `quader_guardrail_tests`.
- Put user/editor behavior that goes through `QuaderEditorApi`, `QuaderApi`, commands,
  `BehaviorScenario`, runtime UI models, render extraction data, localization, viewport state, or
  app services in root `tests/*_tests.cpp`. These build in `quader_tests`; register new files in
  `CMakeLists.txt`.
- Put polygon/document internals, geometry, polygon operations, QDR document IO, and low-level
  modeling invariants under `tests/modeling_domain/`. Operation tests use
  `tests/modeling_domain/operation/qdr_document_operation_<feature>_tests.cpp` and build in
  `quader_modeling_domain_tests`.
- Put portable operation semantics, remap, preview, history, repeat, and QDR behavior in the SDK
  consumer/parity suites plus the matching `qdr_document_operation_<feature>_tests.cpp`. Keep the
  single native descriptor-projection contract in
  `qdr_modeling_runtime_operation_command_tests.cpp`; host tool/input lifecycles remain under
  `tests/modeling_domain/tool/`, while production adapter behavior belongs in root app tests.
- Put metadata catalog/schema/placement checks under `tests/modeling_domain/editor_metadata/`.
- Put shared app-test helpers under `tests/support/`; put modeling-domain-only helpers and fixtures
  under `tests/modeling_domain/` or `tests/modeling_domain/fixtures/`.
- Register every new test file in the owning CMake target immediately.

## Test Selection

Use the smallest test set that covers the changed owner, then run full `ctest` before pushing risky
or cross-cutting changes.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/affected_check.ps1 -PlanOnly -ChangedPath path/to/changed_file.cpp
```

- For a new or edited GoogleTest file, build its owning target and run the declared suite with
  `--gtest_filter=SuiteName.*` before broadening.
- For direct polygon operation source edits, prefer `quader_modeling_domain_polygon_loop_tests`
  plus the matching `QdrDocumentOperation...` suite.
- For native operation descriptor/projection edits, prefer
  `quader_modeling_domain_runtime_loop_tests` with
  `QdrModelingRuntimeOperationCommandTests.*`, the metadata loop, and the SDK consumer/parity
  targets when portable facts can change.
- For public API, command, scene, selection, history, viewport, UI, render, resource, or app service
  changes, use `tools/affected_check.ps1`.
- Use `-MeasureFull` whenever the task claims a compile/test speed improvement and record the
  concrete before -> after result in the task summary or `memory.md`.

The native test suite uses GoogleTest only. It is split into `quader_guardrail_tests`,
`quader_tests`, and `quader_modeling_domain_tests`. Modeling-domain prototyping loops may build
developer-only `EXCLUDE_FROM_ALL` targets:
`quader_modeling_domain_polygon_loop_tests`, `quader_modeling_domain_runtime_loop_tests`, and
`quader_modeling_domain_metadata_loop_tests`. These loop targets are not CTest entries.

## Synthetic Behavior Tests

When a change would normally need manual verification of editor behavior, first express the workflow
as a fast headless synthetic behavior test. Use the public API or `tests/support/BehaviorScenario`
to express real user/editor intent, then inspect document, selection, mesh, history, overlay,
viewport, SDK, or render state.

Manual smoke checks are reserved for real GPU/window/driver behavior: Vulkan swapchain creation,
native window chrome/layout appearance, material rendering, screenshot or log verification, and
pixel output until a render readback harness exists.

## Manual Smoke Checklist

- Vulkan backend initializes.
- The Qt `QMainWindow` opens without a separate console window and uses the stock `Fusion` style.
- The native Qt shell shows its menu bar, docked panels, central viewport workspace, and status bar
  with standard window chrome.
- Every visible native-child Filament viewport surface renders app-created polygon meshes with the
  default material.
- Window resize keeps the viewport texture visible without stale or blank UI content.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.

Use `tools/boot_smoke.ps1` for GPU/window smoke checks with screenshot and log-copy output when a
visual runtime check is required.
