<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Project material organization

`ProjectMaterialCatalog` derives materials and folders from the physical project tree.
Empty folders participate in its revision. The Qt browser displays this projection and sends
organization intent through `QuaderProjectAssetsApi`; it never edits files or owns a second catalog.
Asset Browser image imports create texture assets in the selected project folder. Explicit material
editor imports continue to stage texture assignments until the material is saved.
Catalog updates, imports and material saves do not select a material for later creation or assignment.
New geometry uses the reserved Default Material unless its creation arguments supply a material;
assignment always carries an explicit reference through the modeling API. Legacy manifest
`activeMaterial` values are validated on read and discarded, and are omitted from future saves.

Face Texture's **Flip Face UVs** actions mirror the selected faces' effective U or V coordinates
using `min + max - coordinate`. Each face supplies its own bounds unless Treat as One combines
the existing selected UV charts. The action retains geometry, winding, normals, material bindings,
scalar texture settings and occurrence sampling. A collapsed UV axis is a no-op. The separate
Flip Texture actions continue to negate the corresponding scale value. The portable
[texture action owner](../../sdk/core/modeling/src/face_texture_actions.cpp) prepares the changes;
the editor publishes them together in one undoable operation. See the
[native mirror regressions](../../../tests/face_texture_mirror_api_tests.cpp).

`ProjectAssetService` validates complete mixed-asset folder moves through
`ProjectFileSystem`, preserves asset UUIDs and compensates failed publication. Moving an open
material retargets its draft path without discarding authored properties, pending images or previews.
The native reader resolves moved UUIDs when a saved scene contains an obsolete material path.

## Actual material usage

`ProjectMaterialUsageService`, owned by `ProjectSession`, supplies the browser's Used/Not Used
classification through `QuaderProjectAssetsApi::material_usage_revision()` and `material_usage()`.
It counts actual authored face/primitive slots, including hidden and locked geometry and the
effective slot zero of solid curves. Dormant bindings and ordinary paths do not count. Resident
scenes replace their saved versions; unsaved scenes and unopened registered scenes also contribute.
Project UUIDs resolve persistent deletion, while embedded IDs remain scoped to the active scene.

Owner revisions gate live inspection before entity traversal. Immutable geometry sources retain
their slot projections; saved sources retain validated file versions. Unchanged frames perform no
filesystem scans, geometry decoding or catalog rebuilding. Explicit refresh retries incomplete
sources. The read-only historical metadata-10/geometry-2 projection supports usage inspection only;
it neither broadens general scene opening/deletion validation nor rewrites the source file.

Inspection issues name their scene source and keep the query successful. Confirmed Used matches
remain available; incomplete inspection prevents a reliable Not Used classification. These issues
are browser status, not assignment errors. See the
[project material usage regressions](../../../tests/project_material_usage_tests.cpp), including the
opt-in `QUADER_TEST_LEGACY_MATERIAL_USAGE_SCENE` assignment reproduction against a copied source.

## Dependencies and deletion

`ProjectAssetDependencyService`, owned by `ProjectSession`, derives scene-to-material and
material-to-texture references. Saved and live versions of a dirty scene remain separate: closing
with discard can restore the saved references. All stored primary and slot bindings count, including
hidden, locked, definition and dormant bindings. Surface-only material-use queries are insufficient
for deletion. Material drafts contribute their pending texture dependencies.

Retained reads use owner revisions. Destructive previews additionally validate exact disk sources;
incomplete reads are errors, not evidence that a material is unused. The confirmation revision binds
folder deletion to the inspected graph. Shared texture files are not garbage-collected.

`ProjectAssetDeletionService` prepares saved changes from disk and resident binding changes
separately. It must never use `save_all()` to implement deletion: that would persist unrelated dirty
edits and would not supply a coordinated transaction. Files and folder contents remain recoverable
until the scene/manifest/catalog transaction commits. Opening a project recovers interrupted
deletions before scanning the material catalog. Notifications describe committed sources only.

Deletion recycles original asset paths through an injected Windows shell port; no permanent
fallback is allowed. Recycle Bin restore recovers files, not assignments cleared in other assets.
Existing per-scene undo histories remain available. Manifest v5 records deleted material and texture
UUIDs; `ProjectMaterialReferenceResolver` maps deleted materials to Default. Raw IDs
retained by old undo records resolve at consumer boundaries, and future scene saves normalize them.
Do not rewrite old history commands, allocate replacement assets, create arbitrary redirect chains,
or make exact catalog lookup pretend that deleted assets still exist. Clipboard, recovery, material
usage/Find-Replace, renderer extraction and the native reader share this resolution contract.

Behavior coverage lives in the project material organization/deletion/dependency/API tests,
deleted-material resolution tests, QDR metadata tests and bridge-reader tests.
