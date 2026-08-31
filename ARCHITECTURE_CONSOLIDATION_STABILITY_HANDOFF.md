<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Architecture Consolidation Stability Handoff

Date: 2026-08-30

## Scope And State

The approved AC-01 through AC-06 implementation and repository-wide verification are complete in
the working tree. This handoff records the resulting ownership, regression-sensitive invariants,
and exact final checks.

The worktree contains the user's broader in-progress feature work. Do not discard, reset, or stage
only a guessed subset of it. No commit or push is part of this handoff.

## Implemented Owners

### AC-01 — Structural lifecycle

`SceneStructuralLifecycleService` now routes duplicate, destroy, group, and ungroup across scene,
modeling, Curve, selection, and history state. Grouping captures both modeling and registered
sub-selection baselines. Delete and duplicate failure compensation restores complete roots,
definition roots, identities, provider state, scene selection, modeling selection, and history.
Repeat state is recorded only after successful structural commit. Exact modeling revision restore
is deliberately limited to failed-transaction compensation; normal undo/redo remains monotonic.

### AC-02 — Selection and spatial policy

`EditorSelectionService` is the common provider-selection authority for Curve controls and ordinary
selection, including click/marquee edits, sticky hover, validation, and mixed selections. Typed
scene-hit, surface-occlusion, occurrence, depth-stack, and vertex-snap policy is shared across
meshes, Curves, groups, instances, placement, and snapping. Backface evaluation honors reflected
winding, while render-visible nonselectable surfaces can still occlude.

### AC-03 — Viewport tool and pointer lifecycle

Native tool interaction lives in `src/editor/tools` and is registered through the viewport tool
runtime. `ViewportInteractionController` owns semantic ordering. `ViewportPointerGesture` provides
the shared press identity, reservation, threshold/deadline, cancellation, capture, and release
suppression policy; Qt remains an input adapter and presenter. The keybinding service remains the
single stateful semantic sequence dispatcher.

### AC-04 — Modeling preview and publication

Portable previews support atomic multi-object participation. `ModelingHostPublicationTransaction`
coordinates SDK, scene, asset, binding, identity, selection, and revision publication with one
rollback boundary. `ModelingObjectBindingRegistry` is the mapping owner, and the unused full-scene
modeling sync path is removed. Preview baselines precede active tags, empty participants do not
create bindings, and asset uniqueness is captured before mutation. Component previews publish the
host fence. Half Cut and Curve cancellation retain exact source identity and prior live state across
failure paths.

### AC-05 — Project object truth

`SceneDocument` is the sole live project-object owner. `SceneEntityState` supplies the canonical
component inventory to persistence, history, clipboard, bindings, and rollback. New QDR files omit
the legacy object vector; the old record remains validated reader-only input. Authored and legacy
topology both converge through `MeshProjectionService`. Allocation-time scalar baselines allow
exact transaction compensation for recreated IDs.

### AC-06 — Render and UI snapshots

Typed render source stamps and dependency masks gate retained work. Overlay composition and
submission moved into `ViewportOverlaySubmissionCoordinator`, with shared instanced-wire resources
in `InstancedWireRenderBackend`. `MaterialPresentationService` and the Standard Material adapters
provide owner-derived editor/render snapshots. Retained UI presenters gate updates by typed stamps,
keeping broad queries and unchanged Qt setters out of frame callbacks.

## Regression-Sensitive Invariants

- Curve publication registers the native binding; hierarchy capture returns no invented binding.
- A portable selection no-op can still change host selection mode or active member.
- Capture a preview baseline before changing its active-operation tag.
- Empty preview participant sets do not capture or publish bindings.
- Publish component-preview host state before acknowledging the host revision fence.
- Capture asset uniqueness before the first write; preserve a unique asset identity and use
  copy-on-write only for shared assets.
- Record structural repeat state only after commit succeeds.
- Failed structural duplicate uses exact modeling-memento compensation; normal structural
  undo/redo keeps monotonic revisions.
- Delete redo and compensated failure preserve both selected roots and definition roots exactly.
- Backface policy uses authored winding adjusted for reflected transforms; depth ordering uses the
  canonical occurrence stack.
- Curve cancel prefers exact restoration and adapts only when external state invalidates the exact
  baseline. Existing operation and transform cancel behavior remains preserving.
- Group/ungroup history captures modeling state and every registered editor sub-selection provider.
- SceneDocument allocation-time mutation baselines are required for exact recreated-ID rollback.
- The installed SDK external-consumer fixture must match the canonical Debug/Profile MSVC ABI:
  dynamic Release CRT, `NDEBUG`, and unchecked STL iterators. Keep this fixture target scoped so
  exported SDK targets do not mutate downstream global compiler policy.

## Verification Ledger

All rows below passed from the completed implementation tree. The full native executable reports
three intentional skips for licensed local stress fixtures and one disabled test; neither is a
failure.

| Required check | Current state | Final result |
| --- | --- | --- |
| Focused stability regressions | Final focused cluster plus structural/scalar compensation | PASS — 8/8 focused; allocation rollback 1/1, repeat rollback 1/1, scalar compensation 6/6 |
| Full `quader_tests` | 1,917 tests executed | PASS — 1,914 passed, 3 licensed-fixture skips |
| Intentional command baseline review | All 190 command IDs retained; contracts and lifecycle outcomes reviewed | PASS — regenerated with the approved six-owner consolidation reason |
| Full `quader_guardrail_tests` | Complete source/architecture guardrail executable | PASS — 419/419 |
| `tools/regression_gate.ps1` | Builds every test owner, runs the six-target CTest graph, and builds Debug/Profile Quader | PASS — 6/6 CTest targets |
| `ctest --preset vs2026-debug --output-on-failure` | Separate required full invocation | PASS — 6/6 |
| Debug/Profile `quader` build | Canonical first artifact | PASS — `build/vs2026/Debug/quader.exe` |
| Release `quader` build | Canonical second artifact | PASS — `build/vs2026/Release/quader.exe` |

Run the final canonical app builds in this order:

1. `cmake --build --preset vs2026-debug --target quader`
2. `cmake --build --preset vs2026-release --target quader`

Expected handoff artifacts:

- `C:\Users\Drako\Desktop\quader-windows\quader-app\build\vs2026\Debug\quader.exe`
- `C:\Users\Drako\Desktop\quader-windows\quader-app\build\vs2026\Release\quader.exe`

Do not run `tools/boot_smoke.ps1` or launch either executable. No Quader window, GPU smoke,
screenshot, or visual test was launched during this stability pass, following the user's request.

The C++ style gate is omitted because the user did not request commit/push or a style audit. The
application version remains `0.1.66`; neither `VERSION` nor `vcpkg.json` should be bumped.
