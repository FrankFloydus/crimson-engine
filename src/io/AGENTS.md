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
| User save/open/close lifecycle | `src/editor/session/project_session.*`, `src/api/quader_scene_documents_api.*` |

Keep format validation deterministic and version-aware. Parse/validate candidates before replacing
live documents; failed writes must not silently replace valid on-disk state. Use `ProjectFileSystem`
for project-relative containment, case-collision, and link-escape checks. Portable modeling document
IO belongs to `src/sdk/core/modeling`.

QMAT reads legacy v1, Standard v2, and the experimental v3 named-image schema. Map supported v3
sRGB images to Standard albedo with their identities and UV transform intact; opening never rewrites
them. Authoring still writes v2. Reject v3 data that cannot be represented without loss.

Update focused IO tests and compatibility fixtures with format changes; cover user save/load and
project identity through the existing project-session/API tests. Root verification rules apply.
