<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Source Package Guide

Read the nearest package `AGENTS.md` before editing source. The
[architecture map](../architecture-review.md) links every package owner; root
[AGENTS.md](../AGENTS.md) owns shared policy, build requirements and verification.

## Source Rules

- Trace the public entry point through its state owner, transaction/publication and consumer before
  changing behavior. Reuse the existing owner; do not add cross-package mutation shortcuts.
- When adding or moving `.cpp` files, update the owning CMake target in `CMakeLists.txt` or
  `cmake/QuaderModeling.cmake`.
- Update the nearest owner guide only when its contracts or navigation change; do not duplicate
  root policy or copy the source's method catalog.
