<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tests Guide

This is the detailed testing contract. Root [AGENTS.md](../AGENTS.md) owns mandatory final builds,
versioning, and the explicit fast-iteration exception. Read the nearest local guide for the owner
being tested.

## Test Rules

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
- Shared root test helpers belong under `tests/support/`. Keep helper changes small because they
  intentionally force broader affected test runs.

## Selection Rules

- Use `tools/affected_check.ps1 -PlanOnly -ChangedPath <path>` before choosing a broad test set.
  Run the selected affected workflow for public API, commands, scene, selection, history, viewport,
  UI, render, resources, and app services. Instructions for the runner are in
  [tools/AGENTS.md](../tools/AGENTS.md).
- For a new or edited test file, first build the owning target and run its declared GoogleTest suite
  with `--gtest_filter=SuiteName.*`.
- Run full `ctest --preset vs2026-debug --output-on-failure` before pushing changes that touch
  CMake, shared test support, dependency policy, affected-check routing, or multiple owners.
- A compile/test speed claim requires `affected_check.ps1 -MeasureFull` and a recorded concrete
  before -> after measurement. Do not substitute an estimate.
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
- `tools/regression_gate.ps1` is mandatory final verification for command, selection, history,
  instance, overlay, viewport, preference, project, scene-document, or cross-owner behavior work.
  It has no skip-tests or baseline-update path. It builds the test owners, runs full CTest, and
  builds Debug/Profile `quader`; the separate canonical Release build is still required.

## Regression Coverage

- Input/tool changes cover navigation suppression, hover, selection fallback, gizmo consumption,
  preview lifecycle, and undo/redo where state is inspectable.
- UI synchronization changes need a regression proving revision/unchanged-state gates precede
  expensive work and broad catalogs are absent from frame callbacks. A reported performance
  regression also requires a same-build, same-scene before/after frame-time or FPS measurement,
  focused UI tests, and both native builds.
- Keep compatibility fixture bytes stable; [fixtures/qdr/README.md](fixtures/qdr/README.md)
  documents the historical binary needed to verify the current reader.
