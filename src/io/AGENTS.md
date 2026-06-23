<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# IO Package Guide

`src/io` owns storage format readers, writers, and compatibility adapters.

## IO Rules

- Keep format semantics deterministic and version-aware.
- IO code should preserve document/model invariants instead of repairing unrelated editor state.
- Do not introduce editor save/load user workflows until the editor save/load surface exists.
- When a format behavior changes, update focused IO tests and fixtures together.
