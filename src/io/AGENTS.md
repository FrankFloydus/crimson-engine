<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native IO

| Format or contract | Owner |
| --- | --- |
| Binary scene container and atomic replacement | [qdr guide](qdr/AGENTS.md), [format overview](qdr/README.md) |
| `.qproj` workspace manifest and contained paths | `project/project_manifest.*`, `project/project_file_system.*` |
| External `.qmat` materials and dependency resolution | `project/qmat_codec.*`, `project/project_material_catalog.*`, `project/project_asset_resolver.*` |
| OBJ export | `mesh/obj_writer.*` |
| Read-only engine bridge C ABI | `bridge/quader_bridge_reader.hpp`, [bridge contract](bridge/README.md) |
| User save/open/close lifecycle | `src/editor/session/project_session.*`, `src/api/quader_scene_documents_api.*` |
| Advisory engine synchronization after committed saves | `project/project_bridge_notifications.*` |

Keep format validation deterministic and version-aware. Parse/validate candidates before replacing
live documents; failed writes must not silently replace valid on-disk state. Use `ProjectFileSystem`
for project-relative containment, case-collision, and link-escape checks. Portable modeling document
IO belongs to `src/sdk/core/modeling`.

QMAT reads legacy v1, Standard v2, and the experimental v3 named-image schema. Map supported v3
sRGB images to Standard albedo with their identities and UV transform intact; opening never rewrites
them. Authoring still writes v2. Reject v3 data that cannot be represented without loss.
Standard v1/v2 parameters may specify `diffuseModel` as `burley` or `lambert`;
missing values use Burley. Writers preserve the authored selection in v2, and unknown
strings or non-string values are rejected. Strict named-image v3 remains Burley.

Project manifest v5 stores persistent texture UUIDs, paths, legacy aliases and permanent
`deletedTextureIds`, together with the v4 `minimumProductRelease` and `deletedMaterialIds`.
Older v1/v2/v3 manifests remain readable and require Internal Full; v4 retains its authored release.
Texture discovery is an owner-level read-only candidate operation, never a per-slot filesystem scan.
Initial legacy project opening defers format migration until an authored save or workspace mutation.
Serialization writes v5 while preserving release and deletion metadata; only authoring raises release requirements.
Texture resolution consults canonical and alias identities before QMAT path hints, and retired IDs never
fall back to paths. Image identity and decoded bytes remain independent of slot color-space interpretation.
Materials and supported source images may occupy any visible project folder; dot-prefixed bookkeeping
and project manifests are excluded from authored asset discovery.
Session owners enforce release compatibility before replacing live state or provisioning assets.
Deleted UUIDs resolve only to the reserved Default Material through `ProjectMaterialReferenceResolver`.
Catalog lookup remains exact: deleted assets cannot be edited or reassigned. Never discard release
requirements or deletion metadata during manifest hydration.
Material folder deletion coordinates saved scenes, live bindings and recovery before publication;
see the [material ownership contract](../editor/material/README.md).

Update focused IO tests and compatibility fixtures with format changes; cover user save/load and
project identity through the existing project-session/API tests. Root verification rules apply.

Engine bridge completion records live below `.quader/bridge/` and never alter authored formats.
Only scene/material save owners publish them after their complete transaction commits; recovery
autosaves do not. Fingerprints identify the exact serialized or decoded bytes, not a later file
read. Scene publication establishes missing referenced material baselines before its scene record.
Notification failure remains a visible warning on a successful native save.
Bridge source reads use `shared_file_read.hpp`: Windows handles share read/write/delete and close
before decoding so imports cannot hold up atomic scene, material, texture or manifest replacement.
