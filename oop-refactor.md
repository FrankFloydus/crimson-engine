# Quader OOP Refactor Source Of Truth

Last updated: 2026-06-21

This document is the acceptance contract for the active OOP refactor. It replaces the old progress ledger.

The refactor is complete only when every requirement row is `ACCEPTED`, every required test and guardrail has passing evidence, the app and SDK test suites pass, and the boot smoke evidence is recorded.

## Acceptance Definition

The approved plan is implemented exactly. There are no mid-plan deferrals, rejected buckets, workaround phases, or incomplete acceptance claims.

A class counts as part of the architecture only when it owns behavior, state, policy, lifetime, or a real contract. User-facing discoverability classes are valid only when they provide concrete overrides or participate in a real family contract.

Forbidden outcomes:

- Empty `.cpp` files.
- Constructor-only `.cpp` files.
- Pass-through user-facing OOP classes.
- Inline giant tool catalogs.
- UI/app code directly executing SDK/modeling/document mutation APIs that have command/tool/service owners.
- `ViewportPanel` owning modal tool, selection-picking, or transform-gizmo implementation.
- `ModelingSessionService::Impl` owning service state that belongs to session services.
- False `Done`, `Verified`, or `Accepted` status without evidence.

## Status Legend

Allowed statuses only:

- `TODO`: work has not started or current evidence is not enough.
- `CODED`: production code exists in the intended owner files.
- `INTEGRATED`: production code is wired through CMake and call sites.
- `TESTED`: required tests and guardrails pass locally.
- `ACCEPTED`: build, app tests, SDK tests, boot smoke, and evidence are recorded.

`ACCEPTED` requires evidence in the row and in the Verification Log.

## Requirement Matrix

| ID | Requirement | Production Owners | Required Tests | Guardrails | Status | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| R-001 | Replace this file with the strict source-of-truth matrix and keep all statuses evidence-backed. | `oop-refactor.md` | `oop_source_of_truth_tests.cpp` | Forbid old ledger completion words and partial-completion language outside evidence log. | ACCEPTED | `ctest --preset vs2026-debug --output-on-failure` passed on 2026-06-21; `OopSourceOfTruthTests` passed. |
| R-002 | Enforce global source rules: no empty `.cpp`, no constructor-only `.cpp`, no pass-through user-facing OOP classes, no uncompiled `.cpp`. | `tests/oop_architecture_guardrail_tests.cpp`, `CMakeLists.txt` | Architecture guardrail tests | Source scans for empty/constructor-only/pass-through classes and missing CMake entries. | ACCEPTED | `OopArchitectureGuardrailTests` passed; CMake source manifest audit found all `src/**/*.cpp` listed. |
| R-003 | Refactor modeling command tools to the final contract hierarchy. | `src/modeling/tools/**` | `tests/modeling_tool_catalog_tests.cpp` | No `SdkOperationTool`, `BridgeInstantTool`, `BevelEdgesTool`, `ThickenFacesTool`; no constructor-only tool files. | ACCEPTED | `ModelingToolCatalogTests` passed; `quader_modeling` and full app build passed. |
| R-004 | Add `ModelingCommandDispatcher` and command-owner preview helpers as UI-facing modeling command execution paths. | `src/modeling/tools/ModelingCommandDispatcher.*`, `src/editor/ModelingPreviewCommandService.h`, UI/viewport call sites | Dispatcher and UI routing tests | UI/app/viewport cannot call raw `executeInstantOperation`, `executeContextAction`, preview commit/cancel APIs. | ACCEPTED | Production `rg` over `src/ui src/viewport src/app` returned no forbidden raw command/preview calls; app tests passed. |
| R-005 | Move `ModelingCommandToolCatalog` implementation out of the header. | `src/modeling/tools/ModelingCommandToolCatalog.h/.cpp` | Catalog coverage tests | Header declarations only; compiled catalog owns tool instances and unique-id validation. | ACCEPTED | `ModelingToolCatalogTests` and catalog header guardrails passed. |
| R-006 | Rename viewport enum to `ViewportToolId` and add real `ViewportTool` class hierarchy. | `src/viewport/tools/**` | `tests/viewport_tool_catalog_tests.cpp` | No `ViewportToolBase`; no `ViewportTool::` enum usage. | ACCEPTED | `ViewportToolCatalogTests` and `ViewportToolGuardrailTests` passed. |
| R-007 | Add viewport tool event/result/context contracts and route tool behavior through them. | `src/viewport/tools/Tool.h`, runtime/controller files | Viewport runtime event-contract tests | Modal/select/transform tool behavior cannot live in `ViewportPanel`. | ACCEPTED | `ViewportToolRuntimeTests` and `ViewportPanel` source guardrails passed. |
| R-008 | Move `ViewportToolCatalog` implementation out of the header. | `src/viewport/tools/ViewportToolCatalog.h`, `src/viewport/tools/base/ViewportToolCatalog.cpp` | Catalog/runtime tests | Header declarations only; catalog objects live in `.cpp`. | ACCEPTED | `ViewportToolCatalogTests` passed; `ToolCatalogHeadersAreDeclarationsOnly` passed. |
| R-009 | Make `ViewportPanel` orchestration-only. | `src/ui/ViewportPanel.*`, viewport controllers | ViewportPanel routing and source guard tests | No modal branches, transform gizmo implementation, selection picking, box preview state, or direct modal modeling API calls in `ViewportPanel`. | ACCEPTED | `ViewportPanelTests.*` and `OopArchitectureGuardrailTests.ViewportPanelDoesNotOwnToolImplementations` passed. |
| R-010 | Keep final viewport controllers and remove merged controllers. | `ViewportPaneController`, `ViewportShortcutController`, `ViewportHoverController`, `ViewportSelectionController`, `TransformGizmoController`, `ModalToolOverlayRenderer`, presenters/builders/cache | Controller behavior tests | Remove `ViewportGizmoController`; remove `BoxPreviewController`; CMake includes every remaining `.cpp`. | ACCEPTED | Controller sources compile; source manifest audit clean; app tests passed. |
| R-011 | Split `ModelingSessionService` into real session services under `src/modeling/session`. | `src/modeling/session/**`, `ModelingSessionService.*`, `ModelingSessionServices.*` | `tests/modeling_session_service_split_tests.cpp` | `ModelingSessionService.cpp` does not own picking caches, overlay caches, sync flags, repeat state, or direct SDK command calls. | ACCEPTED | `ModelingSessionServiceSplitTests` and `ModelingSessionServiceTests.*` passed. |
| R-012 | Enforce one SDK-to-scene sync flow through command executor, mesh sync, scene sync, and overlay invalidation. | `ModelingCommandExecutor`, `ModelingMeshSyncService`, `SceneModelingSyncService`, `ModelingOverlayService` | Facade/service integration tests | Raw SDK mutation paths are service-owned only. | ACCEPTED | Modeling service split tests and full SDK tests passed. |
| R-013 | Replace undo/redo core with direct `IUndoableCommand` ownership. | `src/core/UndoRedoManager.*`, `src/core/IUndoableCommand.h`, `src/core/CommandResult.h` | Undo manager tests | No `UndoRedoCommand`, `executeExternal`, `toUndoRedoCommand`, `LambdaUndoableCommand`. | ACCEPTED | `UndoRedoManagerTests` passed; production `rg` found no removed undo API strings. |
| R-014 | Make `BatchDocumentCommand` transactional. | `src/document/commands/BatchDocumentCommand.h` | Batch rollback tests | Failed redo/undo restores prior state. | ACCEPTED | `DocumentCommandTests.BatchDocumentCommandRollsBackFailedRedoAndUndo` passed. |
| R-015 | Unify transform commands and add preview transaction. | `ApplyEntityTransformCommand`, `SceneTransformPreviewTransaction` | Transform command and preview transaction tests | No `SetTransformCommand`; preview mutation only through transaction owner. | ACCEPTED | Transform command/preview transaction tests passed; production `rg` found no `SetTransformCommand`. |
| R-016 | Add editor/user-action command service and outliner commands. | `src/editor/EditorCommandService.h`, `src/editor/ModelingHistoryCommand.h`, `src/editor/ModelingPreviewCommandService.h`, `OutlinerPanel` | Outliner delete/duplicate undo/redo tests | UI does not compose document/modeling mutations directly. | ACCEPTED | `EditorCommandServiceTests`, document command tests, and UI routing guardrails passed. |
| R-017 | Refactor render ownership around Filament material, buffer, mesh, preview, overlay, and render target owners. | `src/render/**` | `tests/render_extraction_tests.cpp` | `SceneRenderBridge` does not own overlay proxy construction, overlay slots, preview proxy storage, or buffer uploads. | ACCEPTED | `render_extraction_tests.cpp` coverage passed; `RenderFacadeDoesNotOwnOverlayProxyInternals` passed. |
| R-018 | Move render algorithm bodies from large headers to `.cpp`. | Overlay builders/projectors/policy/style/pass coordinator files | Render source organization tests | Headers contain declarations, constants, small structs, and trivial accessors only. | ACCEPTED | Render extraction tests and full app build passed with extracted `.cpp` owners. |
| R-019 | Add `FilamentBufferDescriptorFactory` and replace local buffer descriptor helpers. | `src/render/FilamentBufferDescriptorFactory.*` | Descriptor helper tests | Local `makeBufferDescriptor`, `makePixelBufferDescriptor`, or raw copied buffer helpers only live in factory. | ACCEPTED | Render extraction tests passed; buffer descriptor ownership compiles through app and SDK-test configs. |
| R-020 | Keep UI menus/panels catalog-driven and routed through tool/command contracts. | `MainMenuBar`, `ToolSettingsPanel`, `ViewportContextMenuPresenter`, `OutlinerPanel` | UI routing tests | No raw modeling or document mutation bypasses in UI/app. | ACCEPTED | `UISystemTests`, `ViewportPanelTests`, and UI bypass guardrails passed. |
| R-021 | Preserve asset/project ownership architecture with typed stores and project session service. | `src/assets/**`, `src/document/ProjectSession.*` | Asset/project behavior tests | Asset stores own revisions and typed access; no scattered asset mutation. | ACCEPTED | `AssetProjectArchitectureTests`, `ProjectDocumentTests`, and `ProjectSessionTests` passed. |
| R-022 | Add boot smoke script and evidence capture. | `tools/boot_smoke.ps1` | Boot smoke script execution | Fails on early exit, fatal modal, empty screenshot, or critical log entries. | ACCEPTED | `tools/boot_smoke.ps1` passed; screenshot `C:\tmp\quader-oop-refactor-boot.png` length 72886 bytes; log copied to `C:\tmp\quader-oop-refactor-boot.log`. |
| R-023 | Run and record final app verification. | Build/test commands, `C:\tmp` evidence | Build/ctest/boot evidence | Commands must pass exactly as listed in Final Acceptance Commands. | ACCEPTED | `cmake --preset vs2026`, app build, app ctest, SDK configure/build/ctest, and boot smoke all passed on 2026-06-21. |

## Test Matrix

| ID | Test File | Coverage | Status | Evidence |
| --- | --- | --- | --- | --- |
| T-001 | `tests/oop_source_of_truth_tests.cpp` | `oop-refactor.md` structure, statuses, matrix columns, evidence rules. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-002 | `tests/oop_architecture_guardrail_tests.cpp` | Empty `.cpp`, constructor-only `.cpp`, pass-through tools, inline catalogs, missing CMake entries, forbidden API bypasses. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-003 | `tests/modeling_tool_catalog_tests.cpp` | Modeling tool hierarchy, registration, dispatcher, SDK/context/preview/custom execution coverage. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-004 | `tests/viewport_tool_catalog_tests.cpp` | Viewport tool hierarchy, event contracts, runtime dispatch, `ViewportPanel` absence guardrails. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-005 | `tests/modeling_session_service_split_tests.cpp` | Session service owners, facade delegation, SDK/scene sync, selection, overlay, transform, modal workflow. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-006 | `tests/core_tests.cpp` | Scene/document/undo behavior that remains genuine behavior coverage. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-007 | `tests/render_extraction_tests.cpp` | Render ownership, overlay cache/factory behavior, overlay payload parity, descriptor factory. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-008 | `tests/build_guardrail_tests.cpp` | Existing dependency/license/local path/Filament validation checks. | ACCEPTED | Passed in `ctest --preset vs2026-debug --output-on-failure`. |
| T-009 | `tools/boot_smoke.ps1` | Startup screenshot/log evidence and crash modal detection. | ACCEPTED | Passed with screenshot/log under `C:\tmp`. |

## Architecture Guardrails

These searches must have the expected result before acceptance.

```powershell
rg "executeInstantOperation|executeContextAction|commitActiveOperationPreview|cancelActiveOperationPreview" src/ui src/viewport src/app
rg "SdkOperationTool|BridgeInstantTool|BevelEdgesTool|ThickenFacesTool" src/modeling/tools
rg "ViewportToolBase" src tests
rg "executeExternal|UndoRedoCommand|toUndoRedoCommand|LambdaUndoableCommand" src
rg "SetTransformCommand" src tests
rg "document\.(setTransform|setMaterial|clearSelection|setSelected|destroyEntity|createEntity|setParent)" src/ui src/app
rg "OverlaySlot|OverlayProxyGroup|rebuildOverlayProxies|destroyOverlayProxy|toolPreviewMeshProxies_" src/render/SceneRenderBridge.*
rg "makeBufferDescriptor|makePixelBufferDescriptor|new std::byte\[" src/render src/ui
```

Allowed matches are documented in tests only when the test is asserting absence or a designated owner.

## Final Acceptance Commands

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader -- /m:1
ctest --preset vs2026-debug --output-on-failure
cmake --preset vs2026-sdk-tests
cmake --build --preset vs2026-sdk-tests-debug -- /m:1
ctest --preset vs2026-sdk-tests-debug --output-on-failure
.\tools\boot_smoke.ps1 -Exe ".\build\vs2026\Debug\quader.exe" -ScreenshotPath "C:\tmp\quader-oop-refactor-boot.png" -LogCopyPath "C:\tmp\quader-oop-refactor-boot.log"
```

## Verification Log

| Time | Command Or Evidence | Result | Notes |
| --- | --- | --- | --- |
| 2026-06-21 | `cmake --preset vs2026` | PASS | Configure completed and generated `build/vs2026`. |
| 2026-06-21 | `cmake --build --preset vs2026-debug --target quader -- /m:1` | PASS | Built `build/vs2026/Debug/quader.exe`. |
| 2026-06-21 | `ctest --preset vs2026-debug --output-on-failure` | PASS | `quader_tests` passed, 245 tests from 64 suites. |
| 2026-06-21 | `cmake --preset vs2026-sdk-tests` | PASS | Configure completed and generated `build/vs2026-sdk-tests`. |
| 2026-06-21 | `cmake --build --preset vs2026-sdk-tests-debug -- /m:1` | PASS | Built app and copied SDK test executables in SDK-test config. |
| 2026-06-21 | `ctest --preset vs2026-sdk-tests-debug --output-on-failure` | PASS | 138/138 SDK tests passed. |
| 2026-06-21 | `.\tools\boot_smoke.ps1 -Exe ".\build\vs2026\Debug\quader.exe" -ScreenshotPath "C:\tmp\quader-oop-refactor-boot.png" -LogCopyPath "C:\tmp\quader-oop-refactor-boot.log"` | PASS | Screenshot length 72886 bytes; log length 3152 bytes; no fatal/critical matches. |

## Final Checklist

- [x] Every requirement row is `ACCEPTED`.
- [x] Every test matrix row is `ACCEPTED`.
- [x] Architecture guardrails pass.
- [x] `cmake --preset vs2026` passes.
- [x] `cmake --build --preset vs2026-debug --target quader -- /m:1` passes.
- [x] `ctest --preset vs2026-debug --output-on-failure` passes.
- [x] `cmake --preset vs2026-sdk-tests` passes.
- [x] `cmake --build --preset vs2026-sdk-tests-debug -- /m:1` passes.
- [x] `ctest --preset vs2026-sdk-tests-debug --output-on-failure` passes.
- [x] `tools/boot_smoke.ps1` records screenshot and log under `C:\tmp`.
- [x] No false completion claim remains in this file.
