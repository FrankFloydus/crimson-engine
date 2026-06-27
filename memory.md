# Quader Agent Memory

This file records hard-resolved tasks whose context is easy to lose and costly to rediscover.
Read it after `AGENTS.md` for non-trivial work, but treat current code, tests, AGENTS guidance, and
the user's request as authoritative.

When adding an entry, keep it concise and useful: date, task/context, root cause or invariant,
affected owners/files, resolved approach, and verification. Do not record routine edits, secrets,
private user data, guesses, or unresolved issues. Prefer updating a related existing entry over
adding a duplicate.

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
- Restored the external RmlUi vendor include to `RmlUi/Core/Types.h`.

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
  headers, Filament/SDL/RmlUi SDK headers, or GoogleTest without a separate measured change.

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
