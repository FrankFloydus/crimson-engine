<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tools Guide

`tools` is for developer tooling and repository maintenance scripts.

## Tool Rules

- Keep tools separate from product runtime code.
- Prefer deterministic, documented inputs and outputs.
- Avoid destructive filesystem behavior; require explicit user intent for cleanup, migration, or
  overwrite actions.
- If a tool encodes build, dependency, documentation, or test policy, keep it aligned with root
  `AGENTS.md` and the nearest local package guide.

## C++ Style Tool

`tools/check_cpp_style.py` is the clang-tidy style gate. It validates project-owned C++ path naming,
generates its disposable compile database in `.build/cmake-tidy`, and enforces the root
`.clang-tidy` identifier-naming policy.

## Regression Gate

- `regression_gate.ps1` is the read-only, complete command/behavior verification path; do not add
  skip-tests or baseline-update switches.
- `update_command_behavior_baseline.ps1` is the separate intentional-change path. It must require a
  non-empty reason and leave the resulting baseline diff for review.
