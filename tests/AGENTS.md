<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tests Guide

Follow root `AGENTS.md` testing, build, versioning, and guardrail rules before changing tests.

## Test Rules

- Prefer fast, headless tests.
- Use public APIs and `tests/support/BehaviorScenario` to express real user/editor intent.
- Cover selection, commands, modeling operations, transform previews, tool lifecycles, overlays,
  viewport state, and history with synthetic behavior tests when state can be inspected.
- Keep manual smoke checks for Vulkan/window/native chrome/material/pixel behavior until
  render readback coverage exists.
- Do not add save/load or QDR roundtrip behavior tests for editor workflows until the editor
  save/load surface exists.
- When adding test files, update the owning CMake test target.

## Placement Rules

- Root `tests/*_tests.cpp` files belong to `quader_tests` and should cover app/runtime/editor
  behavior: public API workflows, `BehaviorScenario`, commands, selection/history, viewport state,
  render extraction data, UI models, localization, and app services. Register new files in
  `CMakeLists.txt`.
- Root guardrail files belong to `quader_guardrail_tests` and should cover source-policy,
  architecture, dependency, naming, docs, CMake, and tooling invariants.
- Modeling-domain behavior belongs under `tests/modeling_domain/` and is registered in
  `cmake/QuaderModeling.cmake`, not `CMakeLists.txt`.
- Shared root test helpers belong under `tests/support/`. Keep helper changes small because they
  intentionally force broader affected test runs.

## Selection Rules

- Use `tools/affected_check.ps1 -PlanOnly -ChangedPath <path>` before choosing a broad test set.
- For a new or edited test file, first build the owning target and run its declared GoogleTest suite
  with `--gtest_filter=SuiteName.*`.
- Run full `ctest --preset vs2026-debug --output-on-failure` before pushing changes that touch
  CMake, shared test support, dependency policy, affected-check routing, or multiple owners.
