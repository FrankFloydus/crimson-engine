<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Asset Ownership

- `AssetRegistry`, `TypedAssetStore`, and asset documents own payloads, validation, IDs, and revisions.
  Renderer resources are derived caches.
- Embedded and Standard project materials share `MaterialDiffuseModel`; Burley is the
  default, Lambert is an explicit authored choice, and invalid enum values are rejected.
- `mesh_asset_payload.hpp` and `modeling_mesh_payload_adapter.*` are the mesh payload boundary;
  preserve immutable/shared payload and clone semantics when editing assets.
- `mesh_collision_geometry_payload.*` derives and caches local collision triangles from rendered
  primitive spans. Viewport and engine bridges share this owner; preserve Godot-compatible snapping
  and triangle multiplicity. Collision payloads are derived, never a second editable mesh.
- Scene-local `AssetId` and persistent `ProjectAssetId` are different identities. Project material
  files and catalog resolution live in `src/io/project`, with project lifetime in
  `src/editor/session/project_session.*`.
- User edits route through asset command services/public APIs. Validate references before publication
  and cover revision, undo/redo, and mesh/material contracts in the matching asset/API tests.
