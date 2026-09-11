<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Project material organization

`ProjectMaterialCatalog` derives materials and folders from the physical `Materials` tree.
Empty folders participate in its revision. The Qt browser displays this projection and sends
organization intent through `QuaderProjectAssetsApi`; it never edits files or owns a second catalog.
Imports select a material destination while retaining the independent `Textures` storage policy.

`ProjectMaterialOrganizationService` validates complete folder/material moves through
`ProjectFileSystem`, preserves material UUIDs and compensates failed publication. Moving an open
material retargets its draft path without discarding authored properties, pending images or previews.
The native reader resolves moved UUIDs when a saved scene contains an obsolete material path.

## Dependencies and deletion

`ProjectAssetDependencyService`, owned by `ProjectSession`, derives scene-to-material and
material-to-texture references. Saved and live versions of a dirty scene remain separate: closing
with discard can restore the saved references. All stored primary and slot bindings count, including
hidden, locked, definition and dormant bindings. Surface-only material-use queries are insufficient
for deletion. Material drafts contribute their pending texture dependencies.

Retained reads use owner revisions. Destructive previews additionally validate exact disk sources;
incomplete reads are errors, not evidence that a material is unused. The confirmation revision binds
folder deletion to the inspected graph. Shared texture files are not garbage-collected.

`ProjectMaterialDeletionService` prepares saved changes from disk and resident binding changes
separately. It must never use `save_all()` to implement deletion: that would persist unrelated dirty
edits and would not supply a coordinated transaction. Files and folder contents remain recoverable
until the scene/manifest/catalog transaction commits. Opening a project recovers interrupted
deletions before scanning the material catalog. Notifications describe committed sources only.

Deletion is permanent, while existing per-scene undo histories remain available. Manifest v3 records
deleted material UUIDs; `ProjectMaterialReferenceResolver` maps these identities to Default. Raw IDs
retained by old undo records resolve at consumer boundaries, and future scene saves normalize them.
Do not rewrite old history commands, allocate replacement assets, create arbitrary redirect chains,
or make exact catalog lookup pretend that deleted assets still exist. Clipboard, recovery, material
usage/Find-Replace, renderer extraction and the native reader share this resolution contract.

Behavior coverage lives in the project material organization/deletion/dependency/API tests,
deleted-material resolution tests, QDR metadata tests and bridge-reader tests.
