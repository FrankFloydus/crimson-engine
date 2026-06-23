<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Source Package Guide

This file adds source-wide routing for code under `src/`. Also read the nearest package
`AGENTS.md` under the specific source directory you are editing.

## Source Rules

- Treat the first directory under `src/` as a package boundary unless a narrower owner already
  exists.
- Keep user/editor intent behind public APIs, command services, or editor services.
- Keep low-level math, mesh, render, asset, storage, and platform invariants in their owner packages.
- Do not add cross-package shortcuts because a caller is missing a convenience method; add the owner
  behavior and expose it cleanly.
- When adding or moving `.cpp` files, update the owning CMake target in `CMakeLists.txt` or
  `cmake/QuaderModeling.cmake`.
- Prefer focused headless tests for source behavior. Use manual smoke checks only for window, GPU,
  driver, and pixel-output behavior that cannot be inspected programmatically yet.
