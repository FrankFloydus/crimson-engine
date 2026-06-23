<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Scene Package Guide

`src/scene` owns scene and project document truth.

## Scene Rules

- `SceneDocument`, `ProjectDocument`, `ProjectSession`, scene components, and scene bounds own scene
  state and invariants.
- User-triggered create, destroy, parent, select, rename, transform, visibility, lock, and material
  changes should route through document commands, services, or public API batches.
- Keep naming policy in scene-owned helpers such as `SceneEntityNaming`.
- Do not let UI, render, or modeling runtime caches become scene truth.
- Add behavior or document-command tests for scene state transitions and invariants.
