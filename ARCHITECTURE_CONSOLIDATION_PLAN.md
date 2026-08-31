<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Six-Point Architecture Consolidation Plan

Approved: 2026-08-29 by the user request to fully address all six audited systemic outliers.

This is the active successor plan permitted by root `AGENTS.md`. It does not reopen the completed
Phase 0-6 SDK migration or change the fixed direction in `architecture-review.md`. The portable
modeling SDK remains modeling truth; native scene, asset, editor, viewport, render, UI, and QDR
owners adapt around it.

## Program Status

Implementation and final repository-wide verification of AC-01 through AC-06 are complete. Every
command in the final checklist passed from the resulting source tree, including the isolated
installed-SDK consumer and both canonical native executables.

## Completion Rule

The program is complete only when all six tasks below are implemented, their parallel production
paths are removed, focused behavior and guardrail coverage passes, `tools/regression_gate.ps1`
passes, full CTest passes, and both canonical `quader` executables build. A compatibility reader or
public deprecated wrapper may remain only when it has an explicit owner, behavior test, and removal
condition; it may not remain a second mutable source of truth.

## AC-01 — One Structural Entity Lifecycle

Status: Completed.

- `SceneStructuralLifecycleService` is the scene-aware owner for duplicate, destroy, group, and
  ungroup intent across modeling objects, Curves, generic entities, nested groups, and mixed
  hierarchies. Public APIs, document commands, repeat, and drag duplication route through the same
  lifecycle instead of choosing competing mutation pipelines.
- Structural capture includes the modeling memento and registered `EditorSubSelectionBaseline`
  providers, so Curve controls, hover, active members, provider revisions, modeling selection,
  scene selection, and history restore together.
- Structural duplicate records repeatable transform state only after a successful commit. A failed
  duplicate transaction uses exact modeling-memento restoration solely for failure compensation;
  ordinary undo/redo retains monotonic modeling revisions.
- Scene-aware delete checkpoints both selected roots and definition roots. Publication or history
  failure restores them verbatim, and redo ordering cannot expose a half-deleted scene.

Acceptance evidence: direct/public/command parity, nested mixed hierarchy coverage, exact
publication-failure rollback, repeat rollback, and selection/history fingerprints.

## AC-02 — One Selection And Scene Spatial Policy

Status: Completed.

- `EditorSelectionService` owns provider selection publication, click and marquee edit algebra,
  sticky hover, hit validation, and mixed scene/component selection. Curve controls participate
  through the registered sub-selection provider instead of a private selection policy.
- A successful SDK selection no-op may still publish a changed host mode or active member; callers
  must not infer that all host selection state is unchanged from the portable result alone.
- `ViewportSceneHitPolicy`, scene-surface queries, occurrence projection, and the canonical depth
  stack share backface, reflected-winding, visibility, occlusion, group promotion, and instance
  rules. Render-visible locked or nonselectable surfaces may occlude without becoming targets.
- Mesh and Curve vertex candidates use the shared aggregation and stable comparator. Curve
  publication enrolls its native binding; hierarchy capture does not invent missing bindings.

Acceptance evidence: provider-independent click/marquee behavior, Curve control visibility through
the universal snapshot, deterministic occurrence/depth tests, reflected-surface tests, and mixed
mesh/Curve snap coverage.

## AC-03 — One Viewport Tool And Pointer Lifecycle

Status: Completed.

- Native Curve, Primitive, Pivot, Linear Array, and Radial Array interaction now lives under
  `src/editor/tools` and is registered through the canonical viewport tool runtime. Qt translates
  input and presents state; `ViewportInteractionController` owns semantic precedence.
- `ViewportPointerGesture` centralizes press identity, reservation, threshold/deadline behavior,
  capture cancellation, and release suppression for selection, gizmos, materials, Curve
  conversion, and context menus.
- The keybinding service and viewport resolver remain the stateful chord/sequence authority. Raw Qt
  events do not create parallel tool or shortcut lifecycles.

Acceptance evidence: catalog lifecycle coverage, focus/navigation/Escape/outside-pointer matrices,
release suppression tests, and source guardrails excluding concrete tool enumeration from Qt/API
lifecycle paths.

## AC-04 — One Modeling Preview, Publication Transaction, And Binding Registry

Status: Completed.

- The portable `ModelingSession` supports atomic multi-object previews. Native publication uses
  `ModelingHostPublicationTransaction` to coordinate SDK state, scene state, assets, bindings,
  native identities, selection, and revision fences with one rollback contract.
- `ModelingObjectBindingRegistry` owns SDK/native/entity mappings while the public legacy integer
  compatibility surface remains an adapter. The production-unused full-scene sync owner was
  removed.
- Preview baselines are captured before the active preview tag changes. Empty participant sets do
  not manufacture bindings, component previews enroll host publication, and asset uniqueness is
  captured before the first write so unique identities remain stable while shared assets use
  copy-on-write.
- Half Cut stages settings until projection succeeds, keeps typed preview-operation ownership,
  preserves every source scene identity across separated-part toggles, and reconstructs the prior
  live preview on structural failure. Curve cancellation uses exact-preferred adaptive restoration;
  operation and transform cancellation preserve their established revision semantics.

Acceptance evidence: single/multi-object parity transcripts, begin/update/no-op/commit/cancel,
failure injection across all publication boundaries, stale-handle identity tests, and exact
preview/selection/history restoration.

## AC-05 — SceneDocument Is The Sole Project Object Truth

Status: Completed.

- Stable editor-object identity and all live component state are owned only by `SceneDocument`.
  `SceneEntityState` is the shared durable capture/apply inventory for QDR, structural history,
  clipboard, bindings, and failure rollback.
- Current QDR writers emit canonical entity records and omit the legacy object vector. Legacy
  `ObjectRecord` data remains a validated reader-only compatibility input and cannot override
  conflicting explicit entity identity.
- Authored topology conversion routes through `MeshProjectionService`; legacy CSR fields are
  reader-only compatibility data. SceneDocument allocation-time scalar baselines permit exact
  recreated-ID transaction rollback without weakening normal monotonic revisions.

Acceptance evidence: real legacy fixture reads, new-writer schema checks, rich component roundtrip,
duplicate/delete/clipboard identity tests, topology equality, and one component inventory owner.

## AC-06 — Render And UI Consume Owner-Derived Snapshots

Status: Completed.

- Typed `RenderSourceStamp` values and dependency masks gate scene, asset, render, overlay, and UI
  work before broad queries, hashing, copies, descriptor construction, or retained-widget setters.
- `ViewportOverlaySubmissionCoordinator` owns overlay composition, retained signatures, rebind
  recovery, and submission. `InstancedWireRenderBackend` provides the shared persistent and
  selected-instance wire resource lifecycle.
- `MaterialPresentationService` owns editor material presentation, while the Standard Material
  render and preview adapters provide one native-to-render projection. Qt retains localization,
  images, widget fanout, and physical presentation only.
- Typed retained UI presenters own their refresh stamps and unchanged-frame gates. Renderer/UI
  consumers use owner-derived snapshots instead of rebuilding parallel scene or material truth.

Acceptance evidence: unchanged-frame source guardrails, targeted invalidation tests, material
draft/persisted parity, overlay lifetime/counter coverage, and focused render/UI suites.

## Final Verification

These results are from the final implementation tree. The post-ledger sanity pass reran the complete
guardrail executable after this documentation was closed.

| Check | Result |
| --- | --- |
| Focused stability tests and full native suite | PASS — 8/8 focused; 1,914 passed and 3 licensed-fixture skips in the 1,917-test native run |
| Command behavior contract and approved baseline | PASS — 190/190 commands covered; deterministic owner/revision changes approved with the consolidation reason |
| Complete `quader_guardrail_tests` executable | PASS — 419/419 |
| `powershell -NoProfile -ExecutionPolicy Bypass -File tools/regression_gate.ps1` | PASS — 6/6 registered CTest targets plus the Debug/Profile app build |
| `ctest --preset vs2026-debug --output-on-failure` | PASS — 6/6 |
| `cmake --build --preset vs2026-debug --target quader` | PASS — `build/vs2026/Debug/quader.exe` |
| `cmake --build --preset vs2026-release --target quader` | PASS — `build/vs2026/Release/quader.exe` |

The C++ style gate is not part of this active final checklist because no commit or push was
requested. Root policy requires it only before commit/push or when the user explicitly requests a
style audit.

No Quader window, GPU smoke, screenshot, or visual test may be launched for this pass, following
the user's request. The application version remains `0.1.66`.
