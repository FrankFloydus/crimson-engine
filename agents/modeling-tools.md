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
- Native runtime-backed instant commands use `RuntimeCommandTool` when they execute an
  `EditorRuntimeCommandId` directly, such as mirror and flip operations.
- Polygon context actions use catalog-owned `ContextActionTool` rows.
- Preview/modal operation commands use catalog-owned `PreviewOperationCommandTool` rows unless they
  need custom behavior.

Representative commands include:

- `delete_selection`
- `duplicate_meshes`
- `apply_active_material`
- preview/modal operations such as `bevel_edges`, `thicken_faces`, and `slice_quad`

`delete_selection` is a split-route command. Component-mode deletion belongs to
`DeleteSelectionTool` and `ModelingSessionService::deleteComponentSelection()`. Object, group, and
selected scene-group-root deletion must use the scene-aware document command route. Do not put
delete selection back into `ModelingInstantOperationCatalog` or recreate a generic
`ModelingSessionService::deleteSelection()` path.

## Public Command Bridge

- Descriptor metadata owns the public command bridge: `publicCommandId`, `registersPublicCommand`,
  and `hostShortcutTargetId`.
- Use shared edit commands such as `edit.delete` when the operation is also a global editor command.
- Generated modeling action command IDs come from `QuaderCommandsApi::commandIdForModelingAction()`.
  Viewport tool activation command IDs come from `commandIdForToolActivation()`.
- Do not reintroduce legacy compatibility aliases such as `history.*`, `modeling.action.*`, or
  `modeling.*`, and do not add registry alias/canonicalization APIs unless a task explicitly asks
  for a migration layer.

## Picking Rules

- Selection and hover picking policy belongs in modeling runtime picking owners. Do not duplicate
  source-wire-first testing, surface occlusion, pick-radius normalization, or editor surface pick
  policy in tools, UI, or API call sites.
- Editor surface picking should use the shared editor policy: backfaces are pickable, but authored
  front-facing faces are not preferred over nearer visible surfaces.
- Keep pick options that affect cached face geometry, including `prefer_front_facing_faces`, in
  picking-cache signatures.

## Rules

- Put real custom behavior in `.cpp` only when there is meaningful behavior.
- Do not create files merely to restate descriptor metadata.
- Register command tools in `ModelingCommandToolCatalog.cpp`.
- Keep catalog implementation out of headers.
- Keep ids unique.
- Update `tests/modeling_tool_catalog_tests.cpp` for new command families or routing rules.
- App-facing execution should go through `QuaderApi`, `ModelingCommandExecutionService`,
  `ModelingCommandDispatcher`, or `EditorCommandService`, not raw modeling operation calls.
