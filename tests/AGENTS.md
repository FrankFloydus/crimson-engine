<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tests Guide

This guide owns test placement and selection. Root [AGENTS.md](../AGENTS.md) owns risk-based
verification, Release-only default handoff, and versioning. Read the tested owner's local guide.

## Test Rules

- Tests follow the [source naming conventions](../src/AGENTS.md#naming-from-the-first-edit)
  from the first edit, including local `constexpr` names and fixture/static members. Do not
  leave naming corrections for the commit request.
- Use GoogleTest and deterministic, headless fixtures. Before a manual editor workflow, express
  its inspectable behavior through public APIs or `tests/support/BehaviorScenario` and assert the
  relevant document, mesh, selection, history, overlay, viewport, or render state.
- Public project save/open and scene persistence are implemented; cover those workflows through
  their public APIs. Portable QDR and native QDRB scene persistence have separate owners.
- GPU/window/chrome/material/pixel checks belong to `tools/boot_smoke.ps1` or an existing opt-in
  runtime test when headless state cannot prove the behavior. Do not claim visual verification from
  a source-policy assertion.
- Register each new test file in its owning CMake target immediately.

## Placement Rules

- Root `tests/*_tests.cpp` files belong to `quader_tests` and should cover app/runtime/editor
  behavior: public API workflows, `BehaviorScenario`, commands, selection/history, viewport state,
  render extraction data, UI models, localization, and app services. Register new files in
  `CMakeLists.txt`.
- Restricted executable behavior belongs in `tests/public_release*_tests.cpp`, registered in
  `quader_public_release_tests`. This separate executable links the immutable public profile;
  ordinary tests retain the Full profile, without runtime profile setters.
- Root guardrail files belong to `quader_guardrail_tests` and should cover source-policy,
  architecture, dependency, naming, docs, CMake, and tooling invariants.
- Modeling-domain behavior builds in `quader_modeling_domain_tests`, belongs under
  `tests/modeling_domain/`, and is registered in
  `cmake/QuaderModeling.cmake`, not `CMakeLists.txt`. See its [guide](modeling_domain/AGENTS.md)
  for polygon operation, tool, metadata, and focused loop placement.
- Portable operation semantics, selection remap, preview, history, repeat, and QDR behavior belong
  in SDK consumer/parity tests under `tests/sdk` plus the matching document
  operation suite. `quader_modeling_sdk_consumer_tests` must link only SDK targets; native adapter
  workflows belong in root app tests. `tests/sdk_consumer` is the separate installed-SDK consumer.
- Shared root test helpers belong under `tests/support/`. Run the consumers of changed helpers;
  common fixture infrastructure warrants broad coverage, a command-specific fixture does not
  automatically require every modeling and SDK test.

## Selection Rules

- Choose one verification pass with `tools/affected_check.ps1 -PlanOnly -ChangedPath <path>` or a
  directly selected owner suite (`--gtest_filter=SuiteName.*`). Include all suites declared by
  new/edited test files in that pass. Build their targets first; do not require a separate focused
  run if the selected broader run already includes them. Runner details: [tools/AGENTS.md](../tools/AGENTS.md).
- Use current-task paths plus interacting unverified changes. A dirty worktree can contain already
  verified work from earlier tasks. Omitting `-ChangedPath` intentionally checks the whole diff;
  reserve that scope for integration or when the previous verification is unknown.
- Treat the plan as conservative routing, not permission to skip known affected behavior. If it
  expands to full CTest because of source registration or a command-specific fixture, inspect the
  diff and choose the affected owners explicitly. Unknown impact requires the full gate.
- A successful full `ctest --preset vs2026-debug --output-on-failure` satisfies all included owner
  and command suites. Do not run affected checks and then the full gate on the same inputs just to
  satisfy two checklist items. After a fix, rerun only tests affected by that fix; reuse the rest.
- Documentation and source-comment edits need source/link checks only. Run applicable source-policy
  checks before expensive compilation when their existing test binary is current. Test-only changes
  need no app executable. Do not add source-text tests solely to freeze workflow paragraph wording.
- Keep actual log timings for speed claims. `affected_check.ps1 -MeasureFull` is an optional controlled
  benchmark; it deliberately runs additional work and is not part of normal iteration.
- Allow at least ten minutes for build/test commands, or keep an asynchronous process alive and
  poll it without treating a tool yield as failure.

## Command Behavior Contract

- Each visible registered command needs exactly one explicit record in
  `support/command_behavior_contract.cpp`: canonical fixture, allowed observable domains, outcome
  policy, and undo/repeat/preview policy. New or renamed commands have no prefix-based fallback.
- The contract suite executes the full registry through public editor APIs and fingerprints
  document/scene state, exact geometry, selection, semantic overlay, assets, history, tools,
  viewports, preferences, and revisions. Failure and `changed=false` preserve observable state;
  success changes only declared domains; undo/redo must replay semantic state exactly.
- `baselines/command_behavior.json` is read-only during ordinary testing. For an intentional
  behavior change, run `tools/update_command_behavior_baseline.ps1 -Reason "<behavior change>"`
  and review the complete diff. Never regenerate a baseline merely to silence a failure.
- Run `CommandBehaviorContractTests.*` when command behavior, defaults, or fixtures change, together
  with the feature's affected suites. Adding a command through existing dispatch does not alone
  require the isolated SDK fixture or every unrelated editor test.
- `tools/regression_gate.ps1` remains the complete, read-only test path for broad risks listed in
  root policy. It has no skip-tests or baseline-update path and builds no native app. Use it instead
  of an equivalent affected run. Build Release once when app inputs changed; Debug/Profile is only
  built on explicit request.

## Regression Coverage

- Input/tool changes cover navigation suppression, hover, selection fallback, gizmo consumption,
  preview lifecycle, and undo/redo where state is inspectable.
- UI synchronization changes need a regression proving revision/unchanged-state gates precede
  expensive work and broad catalogs are absent from frame callbacks. A reported performance
  regression also requires a same-build, same-scene before/after frame-time or FPS measurement,
  focused UI tests, and the Release build (Debug/Profile only if requested).
- Keep compatibility fixture bytes stable; [fixtures/qdr/README.md](fixtures/qdr/README.md)
  documents the historical binary needed to verify the current reader.
