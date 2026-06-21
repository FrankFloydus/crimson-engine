# Modeling Tools

`CommandTool` is the base contract. Every modeling command must provide a descriptor, availability
policy, history policy, and execution path through that contract.

## Tool Families

- SDK-backed instant operations use `SdkInstantOperationTool` and live under
  `src/modeling/tools/commands`.
- Native service-backed instant commands use `CustomInstantCommandTool` or a narrower existing base.
- SDK context actions use `ContextActionTool` and live under `src/modeling/tools/context`.
- Preview/modal operation commands use `PreviewCommandTool` or `SdkPreviewOperationTool` and live
  under `src/modeling/tools/preview`.

Examples:

- `DeleteSelectionTool`
- `DuplicateMeshesTool`
- `CombineMeshesTool`
- `DissolveEdgesTool`
- `MergeVerticesToCenterTool`
- `BridgeEdgesTool`
- `RadialAlignFacesTool`
- `ApplyActiveMaterialTool`
- `EdgeBevelTool`
- `ThickenTool`
- `SliceQuadTool`
- `MergeByDistanceTool`

## Rules

- Put real custom behavior in `.cpp` only when there is meaningful behavior.
- Do not create `.cpp` files merely to call a base constructor.
- Register command tools in `ModelingCommandToolCatalog.cpp`.
- Keep catalog implementation out of headers.
- Keep ids unique.
- Update `tests/modeling_tool_catalog_tests.cpp` for new command families or routing rules.
- App-facing execution should go through `QuaderApi`, `ModelingCommandExecutionService`,
  `ModelingCommandDispatcher`, or `EditorCommandService`, not raw SDK operation calls.
