<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Native Editor Services

| Responsibility | Start here |
| --- | --- |
| Project workspace and scene lifetime | `session/project_session.*`, `session/scene_session.*` |
| Ordered scene, asset, and modeling undo/redo | `history/editor_history_service.*` |
| Recovery copies, interval/dirty gates, retention | `session/project_autosave_service.*` |
| Duplicate, destroy, group, ungroup across owners | `commands/scene_structural_lifecycle_service.*` |
| Scene/component selection and provider publication | `selection/editor_selection_service.*` |
| SDK/scene/asset/binding publication rollback | `modeling/services/modeling_host_publication_transaction.*` |
| Native modeling projection and picking | [runtime guide](modeling/runtime/README.md) |
| Descriptor catalogs | [metadata guide](metadata/README.md) |
| Native tool lifecycle and semantic input | `tools/`, then [input guide](../../INPUT_KEYBINDINGS.md) |
| Face extrusion, pending operation boundaries and remembered mode | `tools/extrude_tool_service.*`; geometry and source snapshots stay in the modeling owner |
| Light range and live scene-color previews | `viewport/point_light_range_interaction.*`, `commands/document_command_service.*` |
| Live material albedo drafts | `material/standard_material_edit_session_service.*`, `render/standard_material_render_snapshot_adapter.*` |
| Material folders and permanent deletion | `material/project_material_organization_service.*`, `material/project_material_deletion_service.*` |
| Saved/live scene and material dependencies | `material/project_asset_dependency_service.*` |
| Retained actual project-wide surface material usage | `material/project_material_usage_service.*` |

- `ModelingSessionService` adapts the portable core; it must not become another modeling owner.
- Structural actions must preserve scene state, SDK bindings, component selection, and chronological
  history together. Failed publication rolls back all participating owners. Normal undo/redo keeps
  monotonic revisions; exact revision restoration is for failure compensation.
- `delete_selection` uses `DeleteSelectionTool`/`deleteComponentSelection()` for components and the
  scene-aware document route for objects, groups, and selected roots other than the scene root.
- Tools own state machines; UI dispatches intent. Cover preview cancel/commit, selection, overlays,
  and undo/redo with focused synthetic behavior tests under the root verification contract.
  Geometry previews must also satisfy the [preview projection contract](modeling/runtime/README.md#live-preview-projection-contract)
  through the retained viewport submission, including when committed assets do not change.
- Scene color previews publish through the existing property-preview checkpoint and record history
  only on acceptance. Material color previews remain drafts; scene saves and inactive-tab closure
  must not cancel the material picker. Preserve saved material texture identity during live tint edits.
- Material folder deletion rewrites saved scenes from disk and resident bindings separately. Keep
  unrelated drafts, selection and history intact; permanent deleted UUIDs resolve through the manifest.
  Recover `.quader/material-deletions` journals before opening catalogs or scenes, rejecting links,
  corrupt baselines and newer document content instead of overwriting them.
