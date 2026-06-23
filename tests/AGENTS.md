<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Tests Guide

Read `../agents/testing-build.md` before changing tests.

## Test Rules

- Prefer fast, headless tests.
- Use public APIs and `tests/support/BehaviorScenario` to express real user/editor intent.
- Cover selection, commands, modeling operations, transform previews, tool lifecycles, overlays,
  viewport state, and history with synthetic behavior tests when state can be inspected.
- Keep manual smoke checks for Vulkan/window/dockspace/material/pixel behavior until render readback
  coverage exists.
- Do not add save/load or QDR roundtrip behavior tests for editor workflows until the editor
  save/load surface exists.
- When adding test files, update the owning CMake test target.
