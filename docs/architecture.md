<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Internal Architecture

Internal architecture document. This file is for Quader maintainers and agents. It may name private
services, native runtime owners, and migration risks that must not appear as public API examples.

## Architectural Authority

The repo root `AGENTS.md` is the routing entry point for agent work. Read it first, then read the
domain files it names before editing code. Current source, tests, and nested `AGENTS.md` files are
more authoritative than this overview when they disagree.

The native standalone Windows app is the product boundary. Former SDK geometry, polygon, editor-core,
host-contract, and QDR IO code is now app-owned code. Do not recreate a separate SDK boundary or make
old copied SDK structure the authority for new behavior.

## Public Intent Boundary

`QuaderEditorApi` is the front door for whole-editor user intent in the native app. Use it for UI,
command palette, shortcuts, automation, viewport intent, tool settings, preferences, and behavior
tests that describe real editor workflows.

`QuaderApi` is the document/modeling API for workflows that do not need runtime editor domains.
Public workflows should prefer `QuaderObjectHandle`, `QuaderSceneBatch`, typed
`quader::quader_command::*` IDs, and `QuaderApiResult` helpers such as `ok()`, `changed()`,
`value()`, and `error_message()`.

`QuaderScript` is the current C++ automation DSL. Lua scripting is planned but not implemented in the
native app. Do not add executable Lua examples, and future Lua bindings must expose only public API
types and approved value structs.

## Owner Layers

| Domain | Current Owner | Notes |
| --- | --- | --- |
| Scene entity truth | `SceneDocument` | Owns entities, hierarchy, components, selection records, revision stamping, and scene bounds inputs. |
| Project truth | `ProjectDocument`, `ProjectSession` | Owns live project document state and project lifecycle composition. `ProjectSession` is currently broad and is a migration target. |
| Asset truth | `AssetRegistry`, typed asset stores | Owns material, texture, and mesh asset records. User-visible asset mutation should move behind undoable asset commands. |
| Undoable document edits | `DocumentCommandService`, `src/editor/commands/document/**` | User-triggered scene mutations should be `IUndoableCommand` implementations. |
| Editor history | `EditorHistoryService`, `UndoRedoManager` | Coordinates editor transactions, document commands, modeling mementos, and preview exclusion rules. |
| Object selection intent | `EditorSelectionService`, `SelectEntitiesCommand` | Object and group selection should route through the command/service owner. Component selection remains modeling-runtime owned. |
| Modeling operations | `ModelingCommandDispatcher`, `ModelingCommandExecutionService`, `ModelingPreviewCommandService` | User-triggered modeling commands run through command services. `ModelingSessionService` is a compatibility facade and should not gain new ownership. |
| Polygon and geometry algorithms | `src/mesh/geometry`, `src/mesh/polygon` | Pure modeling math and topology belong here, away from UI/render/app dependencies. |
| Modeling runtime state | `src/editor/modeling/runtime/**`, `src/editor/modeling/runtime_state/**` | Owns runtime selection, preview, operation, and tool execution state. |
| Scene/modeling synchronization | `ModelingMeshSyncService`, projection services | Owns authored polygon to scene/asset synchronization and selection/overlay projections. |
| Viewport state | `ViewportWorkspaceService` | Owns layout, focused pane, pane view, shading, grid preset, render size, visibility, and camera controllers. |
| Viewport interaction | `src/editor/viewport/**`, `src/viewport/**` | Owns transform gizmo, selection fallback, input routing, camera navigation, and pane interaction state. |
| Render resources | `src/render/**` | Owns Filament resources, render extraction, renderable caches, overlays, materials, textures, and viewport render targets. `SceneRenderBridge` is a facade/composition point. |
| UI presentation | `src/ui/**` | Assembles ImGui presentation and delegates editor intent to public APIs or owner services. UI must not become the source of scene, modeling, asset, render, or viewport truth. |
| Platform | `src/platform/**`, `src/app/**` | Owns SDL/Win32 windowing, file dialogs, fatal error handling, app loop, and runtime composition. |

## Diagnostics Exceptions

Diagnostic globals are allowed only where they are part of the current runtime boundary. `Logger` and
the `LOG_*` macros are a shared-core facade owned by `src/core/logger.*`; they are not a precedent for
new global editor services. Startup may initialize the logger, runtime/domain code may emit log
messages through the macros, and the console panel may present the logger memory sink. New UI/app
diagnostics should use that facade or an owner-specific service, not ad-hoc `spdlog` globals,
diagnostic registries, or crash sinks.

Fatal-error handling stays platform-owned. `src/platform/fatal_error_handler.*` owns terminate,
abort, invalid-parameter, unhandled-exception, crash artifact, and fatal message-box behavior.
`src/main.cpp` installs that boundary during executable startup. Do not add parallel fatal handlers
inside UI panels, the app loop, or runtime composition; if fatal reporting changes, change the
platform owner and keep the startup installation point explicit.

## Current Build Shape

The current CMake graph is a baseline, not the final target architecture:

- `quader_modeling` contains editor projection, selection projection, modeling services, and tool
  runtime integration.
- `quader_core` currently mixes assets, scene/project documents, document commands, history,
  selection, QDR project IO, UI models, and viewport services.
- `quader_api` wraps the public document/editor intent APIs around `quader_core`.
- `quader_runtime` contains app, platform, render, UI, runtime viewport interactions, and
  `QuaderEditorApi`.
- `cmake/QuaderModeling.cmake` already splits `quader_geometry`, `quader_modeling_polygon`,
  `quader_modeling_runtime`, `quader_editor_metadata`, and QDR IO helper targets.

Migration work should split broad targets only after guardrails make dependency direction visible.

## Phase 1 Target Map

Phase 1 defines target areas before files move. Names below are repo-specific target areas, not a
promise that every final CMake target already exists. Existing broad targets can remain as
compatibility or composition targets while sources are moved behind the intended owners.

| Target Area | Intended Responsibility | Current Compatibility / Composition Target |
| --- | --- | --- |
| `core` | Small dependency-light foundations: diagnostics hooks, logging facade, string/localization helpers, undo primitives, paths, and other non-domain utilities. It must not own scene, asset, editor, viewport, render, UI, or platform policy. | Mostly embedded in broad `quader_core`, with diagnostics shared by current targets. |
| `math` | Shared numeric and value helpers that are independent of UI, render, document, and app runtime concerns. | Header/source area under `src/math`; no separate narrow CMake target yet. |
| `geometry` | Pure geometry algorithms and geometric conversions used by polygon/modeling code. | Existing `quader_geometry`. |
| `polygon` | Polygon document model, topology, operations, picking, compilation data, overlay/preview data, serialization support, and future mesh validation/strong element IDs. | Existing `quader_modeling_polygon`. |
| `assets` | Asset registry, material/texture/mesh asset records, typed asset stores, bake helpers, and future undoable asset command integration. | Currently part of broad `quader_core`; user-visible mutations should move toward command-owned paths. |
| `document` | Scene/project document truth: entities, hierarchy, components, selection records, transforms, scene bounds inputs, project document state, and revision stamps. | Currently part of broad `quader_core`; `ProjectSession` remains a broad compatibility owner to narrow. |
| `commands` | Command IDs, command registry, undoable document commands, editor command routing, history integration, and command-shaped scene/modeling/selection intent. | Split across `quader_core`, `quader_api`, and `quader_modeling`; should become a clearer dependency boundary before broad moves. |
| `editor_modeling` | Editor-facing modeling services, runtime sync, selection/projection, previews, operation settings, tool command execution, and compatibility facade behavior. | Existing `quader_modeling`, `quader_modeling_runtime`, and `quader_editor_metadata`; `ModelingSessionService` should stay a facade. |
| `viewport` | Viewport workspace state, pane layout, cameras, navigation/input routing, grid/shading state, focused pane, and viewport tool interaction ports. | Currently split between broad `quader_core`, `quader_runtime`, `src/viewport`, and `src/editor/viewport`. |
| `render` | Render extraction/sync, render snapshots, Filament resource adapters, materials, meshes, textures, overlays, viewport render targets, and render-owned icon/texture lifetimes. | Currently part of broad `quader_runtime`; `SceneRenderBridge` is a facade/composition point. |
| `io_qdr` | QDR shared root IO, durable document/project codecs, atomic write, deterministic serialization, diagnostics, and runtime compatibility adapters. | Existing `quader_qdr_shared_io`, `quader_qdr_document_io`, and `quader_qdr_runtime_io`; runtime compatibility stays adapter-owned. |
| `api` | Public document/editor intent APIs, result types, object handles, scene batches, typed command constants, scripting front doors, and approved value structs. | Existing `quader_api`, with `QuaderEditorApi` currently composed through runtime. |
| `ui` | ImGui presentation, panels, menus, localization use, popups, UI settings/models, and intent routing to public APIs or owner services. UI must not own domain truth. | Currently part of broad `quader_runtime`, with some UI models/settings in `quader_core`. |
| `app` | Native executable composition, startup/shutdown, frame loop, platform adapters, window/event integration, runtime hook wiring, and final target assembly. | Existing `quader_runtime` plus `quader` executable and `src/app` / `src/platform`. |

## Mutation Flow Invariants

User-visible scene edits flow through public API methods or command services, then through the owner
that controls undo/redo, revision stamping, validation, cache invalidation, and selection sync.

Do not add a parallel write path because a nearby UI, test, or render caller needs the same result.
Search first for existing command paths, projection helpers, caches, conversion helpers, and services.
If the owner exists, route the caller to it. If no owner exists, create the narrowest owner in the
domain that owns the data and invariants.

Preserve ordering, undo/redo mementos, revision stamps, cache invalidation, selection sync,
asset/material side effects, mesh projection state, and QDR compatibility when moving code.

## Internal Documentation Rules

Architecture docs can discuss `SceneDocument`, `AssetRegistry`, `ModelingSessionService`,
`SceneRenderBridge`, `RenderSystem`, `ProjectSession`, and other private owners only when the doc is
clearly labeled internal. Public docs and examples should start from public APIs, not private
services.

Do not add executable Lua examples until the native app ships a Lua runtime and binding layer.
Future Lua documentation should remain status or design documentation until bindings exist.

## Migration Risks To Track

- `ModelingSessionService`, `ProjectSession`, `UIContext`, and `RenderSystem` expose broad mutable
  surfaces and should be narrowed through focused owners.
- Object selection still needs one mutation path for command-owned state and runtime projection sync.
- Asset mutations need undoable command ownership for user-visible edits.
- Render and QDR export paths should consume stable snapshots instead of live mutable internals.
- Mesh element IDs and validation need stronger contracts before broad topology refactors.
- Dependency and docs guardrails should make internal/public boundaries executable.
