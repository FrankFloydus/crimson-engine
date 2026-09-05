<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Agent Guide

Quader is a native Windows polygon modeling editor: C++20, Qt Widgets, Filament,
CMake and vcpkg. This file owns repository-wide agent policy. Read it first, then
only the guidance relevant to the change.

## Find the right guidance

| Work | Read next |
| --- | --- |
| Source changes | [src/AGENTS.md](src/AGENTS.md), then the nearest package `AGENTS.md` |
| Ownership or cross-package changes | [architecture-review.md](architecture-review.md) |
| Input, shortcuts, navigation or command lifecycle | [INPUT_KEYBINDINGS.md](INPUT_KEYBINDINGS.md) |
| Non-trivial or regression-prone behavior | Relevant sections of [memory.md](memory.md) |
| Test placement, selection and regression contracts | [tests/AGENTS.md](tests/AGENTS.md) |
| Build/test tooling | [tools/AGENTS.md](tools/AGENTS.md), [cmake/AGENTS.md](cmake/AGENTS.md) |
| Serious known defects | [tasklist.md](tasklist.md) |
| Resources, examples, dependencies or benchmarks | Nearest local `AGENTS.md` |

Current code, tests and the user request override historical memory. There is no active
architecture migration program. Completed plans are in Git history; do not restart them.
[FEATURE_IDEAS.md](FEATURE_IDEAS.md) contains optional product ideas, not authorized work.

## Ownership and implementation

- The app-owned portable modeling core at `src/sdk/core/modeling` is modeling truth.
  Its public namespace is `quader::modeling`, umbrella include
  `quader/modeling/modeling.hpp`, and consumer target `quader::modeling_sdk`.
  Keep Qt, Filament, Win32, native scene/assets/editor types and command IDs outside it.
- Native `QuaderEditorApi` is the whole-editor facade; `QuaderApi` covers document/modeling
  workflows. UI, examples, scripts and automation express user intent through these APIs
  or their command/service owners. Add missing behavior to its owner before exposing it.
- Keep topology, storage, render resources, platform mechanics and hot-path internals low-level.
  Lua bindings must not expose internal scene, asset, session, render or platform owners.
- Prefer typed `quader::quader_command` IDs in C++; stable strings serve scripts and the
  command palette. Use `QuaderObjectHandle` and `QuaderSceneBatch` for object workflows.
  Native results use `ok()`, `changed()`, `value()` and `error_message()`.
- Treat each first directory under `src/` as a package boundary unless a narrower owner
  exists. Search existing helpers and call paths before adding another implementation.
  Never create parallel mutable modeling, scene, selection, history, asset or viewport truth.
- Native code consumes source-only `quader/modeling/neutral/...` declarations, not SDK
  `internal` headers. Do not restore old copied SDK packages, checkout wiring, presets,
  `QUADER_MODELING_SDK_DIR`, or compile-time SDK presence macro checks.
- Preserve behavior when restoring a regression: ordering, state transitions, side effects,
  cache invalidation and errors. Find the defining code/tests/history before changing it.
- Gate expensive queries, snapshot/topology work and retained Qt setters by owner revisions
  before allocation or formatting. Never enumerate broad command catalogs every frame.
  UI/render details and verification live in their package guides.
- Keep raw Qt input at the UI adapter; lower owners consume canonical/semantic input.
  Command changes must reconcile IDs, defaults or deliberate unassignment, context/conflicts,
  Settings, menus/tooltips, localization, persistence compatibility and tests together.
  Changed defaults preserve existing user overrides.
- Keep network dependency fetches opt-in and backed by license evidence. Dependency changes
  include manifest, license/provenance, CMake and guardrails together; avoid new dependencies
  for small utilities already covered by C++ or existing libraries.

## Worktree and version

- Preserve unrelated and pre-existing edits. Never use destructive reset/checkout unless
  the user explicitly requests that operation.
- "Commit and push" without a scope means stage, commit and push the entire current worktree,
  including deletions and untracked files. Partial commits require explicit scope.
- Preserve the current app version unless the current request explicitly asks for a bump.
  `VERSION` is authoritative; `vcpkg.json` `version-string` must match, in unpadded `x.y.z`
  form (displayed as `vX.Y.Z`). For a requested bump, keep `0.1` and increment patch once
  before the final app builds unless the user specifies otherwise. At patch 9999, ask before
  changing major/minor. Builds, commits and handoffs do not imply a bump.

## Canonical native builds

Use one native multi-config Visual Studio tree: `build/vs2026`. Do not create alternative
native build trees or a traditional checked Debug configuration without an explicit request.
The isolated SDK verification fixture is separate from the native handoff.

Whenever a task changes any project file, build both native executables in this order.
The same two-artifact handoff is required after any native app build:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader
cmake --build --preset vs2026-release --target quader
```

Configure when needed; do not clean-rebuild routinely. Allow build/test processes at least
10 minutes (`600000` ms); asynchronous polling must not terminate them at tool yield timeouts.

| Configuration | Required contract |
| --- | --- |
| Debug/Profile | `build/vs2026/Debug/quader.exe`: optimized `/O2`, embedded debug information, Tracy on-demand; Release CRT/dependencies, `NDEBUG`, unchecked STL, Release Qt/vcpkg and Filament `md`. Never ship it. |
| Release | `build/vs2026/Release/quader.exe`: `/O2`, aggressive inlining, COMDAT and whole-program/link-time optimization; no debug info, Tracy definitions or Tracy client. This is the shipping artifact. |

Canonical presets disable Sentry for both artifacts. Deployment uses vcpkg's recursive
Release-bin app-local resolver and `windeployqt --force --release`; direct-DLL copying
misses Qt private dependencies. `tools/boot_tracy.ps1` is Debug/Profile-only.

After builds, report success/failure and the absolute executable path for **both** configurations.

## Verification

- Run builds and verification locally.
- Choose focused owner tests using [tests/AGENTS.md](tests/AGENTS.md). New/edited GoogleTests
  must build their target and run the declared suite before broader verification.
- Use `tools/affected_check.ps1` for public API, command, scene, selection, history, viewport,
  UI, render, resources and app-service changes. Inspect `-PlanOnly` when choosing scope.
- `tools/regression_gate.ps1` is mandatory for command, selection, history, instance, overlay,
  viewport, preference, project, scene-document or cross-owner behavior changes. Its normal
  runs and command baselines are read-only; never regenerate a baseline to hide a failure.
- Every visible command has one explicit contract in
  `tests/support/command_behavior_contract.cpp`. Intentional behavior updates use
  `tools/update_command_behavior_baseline.ps1 -Reason "<why the behavior changed>"`;
  review the complete `tests/baselines/command_behavior.json` diff.
- Express inspectable editor workflows as headless synthetic tests through public APIs or
  `tests/support/BehaviorScenario`. Reserve `tools/boot_smoke.ps1` for native window/GPU,
  driver or pixel-output checks.
- UI frame-loop changes require a regression test gating expensive work; reported FPS
  regressions also require a same-build, same-scene before/after measurement.
  Compile/test speed claims require affected-check `-MeasureFull` evidence.
- Run full `ctest --preset vs2026-debug` before pushing risky or cross-cutting changes.
  Run `python tools/check_cpp_style.py` only before commit/push or an explicit C++ style audit.
- Only a current request explicitly naming **fast iteration mode** skips automated tests
  and affected checks. Still build both native configurations and report untested behavior.

## Keep documentation useful

Keep global policy here, package contracts in local `AGENTS.md`, technical explanations beside
their owner, and hard-to-rediscover regression invariants in topical `memory.md` entries.
Link the owner/test instead of copying a catalog, source listing or build log. Update an existing
entry before adding one. Remove superseded guidance; Git preserves completed task history.
Keep `tasklist.md` limited to verified serious open defects, with evidence and acceptance criteria.

## Large-repository workflow

- Establish the requested outcome, relevant owners, constraints and observable acceptance criteria.
  For a substantial change, keep a short live plan with remaining work and verification; retire it
  when complete. Preserve only durable decisions in owner docs or memory.
- Use `rg --files` and scoped symbol searches, then trace the public entry point, state owner,
  publication path and protecting test. Read relevant sections instead of dumping whole catalogs,
  memory or logs into context. Summarize evidence with paths and test results.
- Delegate independent investigations or edits with explicit file ownership and a bounded result.
  Integrate findings centrally; one coordinator runs builds against a shared build tree.
  Independent editing tasks can use separate Git worktrees; never share generated build artifacts
  between checkouts or discard the current task's uncommitted starting state.
- Keep always-loaded guidance small. Codex's documented project-instruction budget defaults to
  32 KiB across the discovered chain. A root-started task must explicitly read the relevant nested
  guide; do not assume every package guide was automatically loaded.
- Repeated specialized workflows may become narrowly scoped skills; package ownership remains in
  `AGENTS.md`. Prefer existing executable tools and behavior/link checks over more prose rules.

These repository practices adapt OpenAI's [Codex best practices](https://learn.chatgpt.com/guides/best-practices),
[instruction discovery](https://learn.chatgpt.com/docs/agent-configuration/agents-md),
[subagent guidance](https://learn.chatgpt.com/docs/agent-configuration/subagents),
[worktree guidance](https://learn.chatgpt.com/docs/environments/git-worktrees) and
[skill loading](https://learn.chatgpt.com/docs/build-skills), checked 2026-09-05.
