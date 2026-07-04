<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Internal Code Map

Internal architecture document. This map describes current private source layout and migration
boundaries for maintainers and agents. It is not public API documentation.

## Start Here

- Root routing: `AGENTS.md`.
- Docs rules: `docs/AGENTS.md` and `agents/docs-public-examples.md`.
- Ownership rules: `agents/domain-ownership.md`.
- API and scripting rules: `agents/public-api-and-scripting.md`.
- Build and test routing: `agents/testing-build.md`.
- Migration backlog: `docs/architecture_migration_plan.md`.

## Source Layout

Phase 1 target areas are the intended split map. Some names do not exist as standalone CMake targets
yet; broad targets such as `quader_core` and `quader_runtime` remain current compatibility and
composition targets until files move.

| Phase 1 Target Area | Primary Source Roots | Current Compatibility / Composition Target |
| --- | --- | --- |
| `core` | `src/core`, shared diagnostics wrappers | Broad `quader_core`; diagnostics are shared by current targets. |
| `math` | `src/math` | Shared headers/source; no separate narrow target yet. |
| `geometry` | `src/mesh/geometry` | Existing `quader_geometry`. |
| `polygon` | `src/mesh/polygon` | Existing `quader_modeling_polygon`. |
| `assets` | `src/assets` | Broad `quader_core`; asset mutations should migrate toward undoable command ownership. |
| `document` | `src/scene` | Broad `quader_core`; `ProjectSession` is a temporary broad compatibility owner. |
| `commands` | `src/editor/commands`, `src/editor/history`, command-facing API constants | Broad `quader_core` plus `quader_api` and modeling command integration. |
| `editor_modeling` | `src/editor/modeling`, `src/editor/interactions`, `src/editor/projection`, `src/editor/metadata`, modeling parts of `src/editor/tools` and `src/editor/selection` | `quader_modeling`, `quader_modeling_runtime`, `quader_editor_metadata`, and related QDR runtime adapters. |
| `viewport` | `src/viewport`, `src/editor/viewport`, viewport-facing `src/editor/input` | Broad `quader_core` and `quader_runtime` until workspace state and runtime interaction split. |
| `render` | `src/render`, `src/editor/render` | Broad `quader_runtime`; render facades compose focused render owners. |
| `io_qdr` | `src/io/qdr` | Existing `quader_qdr_shared_io`, `quader_qdr_document_io`, and `quader_qdr_runtime_io`. |
| `api` | `src/api` | Existing `quader_api`; `QuaderEditorApi` is currently composed through runtime. |
| `ui` | `src/ui`, UI models/settings currently near UI-facing services | Broad `quader_runtime`, with some settings/models still in `quader_core`. |
| `app` | `src/app`, `src/platform`, `src/main.cpp` | Broad `quader_runtime` plus the `quader` executable. |

| Path | Current Responsibility | Primary Target Area | Notes |
| --- | --- | --- | --- |
| `src/api` | Public C++ API surfaces, command IDs, result types, object handles, scene batches, preferences, tools, UI, viewport, render access, and C++ automation. | `api` | Public intent belongs here. Headers should avoid backend/internal owner headers; `QuaderEditorApi` can remain runtime-composed while Phase 1 splits land. |
| `src/app` | App loop, startup composition, frame clock, window settings. | `app` | Owns runtime wiring, not reusable document/modeling invariants; currently built through `quader_runtime`. |
| `src/assets` | Asset registry, material documents, bake service, typed asset state. | `assets` | Currently built through `quader_core`; user-visible asset edits should move toward undoable asset command ownership. |
| `src/core` | Localization, logging, strings, undo/redo manager, user config paths. | `core` | Keep generic utilities small and dependency-light; move domain owners out as target splits progress. |
| `src/diagnostics` | Profiling and diagnostic helpers. | `core` | Diagnostic-only hooks should stay behind wrappers or explicit app/runtime integration. |
| `src/editor/commands` | Editor command registry, command services, modeling command dispatch, document undoable commands, command tools. | `commands` | Currently spans `quader_core`, `quader_api`, and `quader_modeling`; prefer typed `quader::quader_command::*` in C++ call sites. |
| `src/editor/history` | Editor history, modeling mementos, preview command service, scene transform preview transaction. | `commands` | Owns undo/redo integration and preview exclusion rules. |
| `src/editor/input` | Keybinding service and viewport binding adapters. | `viewport`, `commands` | UI must not parse or persist shortcut JSON directly; viewport binding adapters should follow viewport target ownership. |
| `src/editor/interactions` | Tool interaction math and runtime tool behavior for box, cut, mirror, poly, transform, selection, and related tools. | `editor_modeling`, `viewport` where interaction state is viewport-owned | Tool math belongs in focused modeling or viewport/tool owners, not in UI panels. |
| `src/editor/metadata` | Host contract metadata and tool descriptor catalog. | `editor_modeling` | Metadata projects native defaults; it should not duplicate runtime owner state. |
| `src/editor/modeling` | Modeling runtime state, operations, services, mesh sync, hierarchy duplicate, previews, and operation settings. | `editor_modeling` | `ModelingSessionService` is a facade; add focused owners instead of widening it. |
| `src/editor/projection` | Mesh, picking, preview, cut, drawn-plane, and selection overlay projection. | `editor_modeling` | Projection helpers should be shared rather than copied into UI/render/API paths. |
| `src/editor/render` | Internal render API runtime access glue. | `render`, `api` adapter glue | Keep raw render access constrained to render/runtime composition. |
| `src/editor/selection` | Editor selection service and selection projection service. | `commands`, `editor_modeling` | Object selection intent belongs in command/service owners; component selection stays runtime-owned. |
| `src/editor/tools` | Viewport tool descriptors, runtime wrappers, and base tool helpers. | `editor_modeling`, `viewport` | Tool catalog and descriptors should remain authoritative for tool identity. |
| `src/editor/transform` | Scene transform drag service. | `viewport`, `commands` | Scene-only transform previews belong to `SceneTransformPreviewTransaction`; runtime composition currently lives in `quader_runtime`. |
| `src/editor/viewport` | Runtime viewport interaction controller, selection fallback, transform gizmo interaction, viewport API runtime access. | `viewport` | Owns per-frame editor viewport interaction, not long-lived workspace state. |
| `src/io/qdr` | QDR root/shared IO, document IO, runtime compatibility IO, project codecs, atomic write. | `io_qdr` | Durable project IO should be scene/entity/component based and deterministic; runtime compatibility stays in adapter targets. |
| `src/math` | Shared math value helpers. | `math` | Keep independent from UI/render/app concerns. |
| `src/mesh/geometry` | Geometry algorithms. | `geometry` | Pure algorithm owner; currently `quader_geometry`. |
| `src/mesh/polygon` | Polygon document model, operations, topology, picking, compilation, overlays, serialization support. | `polygon` | Topology invariants and future validation belong here; currently `quader_modeling_polygon`. |
| `src/platform` | SDL/Win32 windowing, input/event adapters, file dialogs, fatal errors. | `app` | Keep platform types inside app/platform adapters where practical; currently built through `quader_runtime`. |
| `src/render` | Filament integration, render system, scene extraction/sync, materials, meshes, textures, overlays, viewport renderer. | `render` | Render algorithms and resources live here. UI submits presentation data; it does not own render truth. |
| `src/scene` | Scene document, components, scene bounds, transforms, entity naming, project document/session. | `document` | Scene graph and project document truth live here; `ProjectSession` remains a broad compatibility owner to narrow. |
| `src/ui` | ImGui panels, main menu, viewport panel, console, UI settings, style, popup helpers, panel manager. | `ui` | UI assembles presentation and routes intent to public APIs or owner services; currently built through `quader_runtime` with some models/settings in `quader_core`. |
| `src/viewport` | Workspace service, camera controller, input router, layout, settings, grid placement, host viewport adapter. | `viewport` | `ViewportWorkspaceService` owns pane layout, views, cameras, focus, grid, and shading state. |

## Current CMake Targets

| Target | Current Role | Migration Note |
| --- | --- | --- |
| `quader_geometry` | Geometry helper library under `src/mesh/geometry`. | Already a narrow modeling-domain target. |
| `quader_modeling_polygon` | Polygon document and operations. | Future mesh validation and strong element IDs belong here. |
| `quader_modeling_runtime` | Modeling runtime, operations, and interaction tools. | Runtime tool command construction should become more typed/focused. |
| `quader_editor_metadata` | Host metadata and tool descriptor catalog. | Keep native defaults projected from owner constants. |
| `quader_qdr_shared_io` | Shared QDR root, atomic write, polygon document codec. | Should remain UI-free and deterministic. |
| `quader_qdr_document_io` | Document-level QDR IO. | Current-schema parsing should become stricter where migration allows. |
| `quader_qdr_runtime_io` | Runtime compatibility IO adapter. | Compatibility belongs here, not in core durable project IO. |
| `quader_modeling` | Editor projection/modeling service integration. | Should be split by focused editor modeling owners over time. |
| `quader_core` | Broad core app library for assets, scene, commands, history, selection, QDR project IO, UI models, and viewport services. | Known broad target; split after dependency guardrails exist. |
| `quader_api` | Public document/editor intent API wrapper. | Should stay the public automation and scripting front door. |
| `quader_runtime` | App, platform, render, UI, viewport runtime interaction, and `QuaderEditorApi`. | Candidate for platform/render/UI/app target split. |
| `quader` | Native standalone Windows executable. | Links `quader_runtime`. |
| `quader_guardrail_tests` | Source-policy, architecture, dependency, naming, docs, CMake, and tooling invariants. | Preferred home for docs/architecture guardrails. |
| `quader_tests` | App/runtime/public API behavior tests. | Use for editor behavior visible through APIs/services. |
| `quader_modeling_domain_tests` | Modeling-domain behavior tests. | Use for polygon/runtime operation and metadata contracts. |

## Intent Flow

1. UI, shortcuts, tests, scripts, or command palette express editor intent through
   `QuaderEditorApi`, `QuaderApi`, or `editor.commands().execute(...)`.
2. Public API routes to command/service owners such as `DocumentCommandService`,
   `EditorSelectionService`, `ModelingCommandExecutionService`, `ViewportWorkspaceService`, or
   project/asset services.
3. Owner services mutate the canonical document, asset, modeling runtime, viewport, or preference
   state and record history when the behavior is undoable.
4. Projection and sync services derive runtime/render/overlay state from canonical owners.
5. UI and render layers present the derived state without becoming sources of truth.

## Test And Guardrail Map

Use `tests/*guardrail_tests.cpp` for internal docs manifests, include direction, naming, CMake, and
source-policy checks. Use root `tests/*_tests.cpp` for public API/editor behavior. Use
`tests/modeling_domain/**` for polygon and modeling runtime contracts.

For docs-only changes, keep edits under `docs/**`, update `docs/public-docs-examples.txt` when a doc
is added or reclassified, and avoid adding public executable Lua examples until Lua exists.
