<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Scene Ownership

- `SceneDocument` owns entity identity, components, hierarchy, selection, layers, and scene revisions.
  `SceneContentDocument` aggregates scene and assets and validates their references. Runtime session
  and project workspace lifetime belong to `src/editor/session`.
- `document/scene_entity_state.*` is the shared component inventory for persistence, history,
  clipboard, and rollback. Do not add a second live object-record store or allocator.
- Keep naming in `SceneEntityNaming`; retain scene-owned hierarchy/occurrence projections and their
  revision gates. Render and UI caches derive from this state.
- User mutations route through editor services/document commands/public API batches. Protect owner
  invariants with scene/document tests and user workflows with public API behavior tests.
