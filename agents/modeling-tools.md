<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Modeling Tools

`CommandTool` is the base contract. Every modeling command must provide a descriptor, availability
policy, history policy, and execution path through that contract.

## Tool Families

- Polygon-backed instant operations use catalog-owned `InstantOperationCommandTool` rows in
  `ModelingCommandToolCatalog.cpp`; only commands with custom behavior get a file under
  `src/editor/commands/modeling`.
- Native service-backed instant commands use `CustomInstantCommandTool` or a narrower existing base.
- Polygon context actions use catalog-owned `ContextActionTool` rows.
- Preview/modal operation commands use catalog-owned `PreviewOperationCommandTool` rows unless they
  need custom behavior.

Examples:

- `delete_selection`
- `duplicate_meshes`
- `combine_meshes`
- `dissolve_edges`
- `merge_vertices_to_center`
- `bridge_edges`
- `radial_align_faces`
- `apply_active_material`
- `bevel_edges`
- `thicken_faces`
- `slice_quad`
- `merge_by_distance`

## Rules

- Put real custom behavior in `.cpp` only when there is meaningful behavior.
- Do not create files merely to restate descriptor metadata.
- Register command tools in `ModelingCommandToolCatalog.cpp`.
- Keep catalog implementation out of headers.
- Keep ids unique.
- Update `tests/modeling_tool_catalog_tests.cpp` for new command families or routing rules.
- App-facing execution should go through `QuaderApi`, `ModelingCommandExecutionService`,
  `ModelingCommandDispatcher`, or `EditorCommandService`, not raw modeling operation calls.
