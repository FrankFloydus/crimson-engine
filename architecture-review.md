<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Architecture Map

Current ownership and dependency direction. Read [AGENTS.md](AGENTS.md) for working rules,
[tasklist.md](tasklist.md) for verified serious defects, and [memory.md](memory.md) for
regression traps. Completed refactors are historical; this document is not an execution plan.

## Product and dependency direction

The native Windows editor is the shipping product. Its app-owned portable modeling core,
`src/sdk/core/modeling`, owns modeling state. The native host adapts that state to scene entities,
assets, project persistence, input, Qt presentation and renderer-neutral snapshots.

```text
Qt presentation / scripts / automation
        -> native public APIs and semantic viewport interaction
        -> editor command, tool and transaction owners
        -> portable ModelingSession + native scene/asset owners
        -> owner-derived snapshots -> render resources / retained UI
```

This describes behavior flow, not a license for arbitrary package includes.
Actual target dependencies live in [CMakeLists.txt](CMakeLists.txt) and
[cmake/QuaderModeling.cmake](cmake/QuaderModeling.cmake).

## Owner map

| Package / guide | Owns |
| --- | --- |
| [src/api](src/api/AGENTS.md) | `QuaderEditorApi` whole-editor intent; `QuaderApi` document/modeling facades; typed commands, handles and batches |
| [src/sdk/core/modeling](src/sdk/core/modeling/AGENTS.md) | Portable mesh/curve documents, operations, typed IDs, validation/remaps, sessions, preview/history, QDR document IO and neutral snapshots |
| [src/editor](src/editor/AGENTS.md) | Native commands, tools, modeling publication, history integration, selection, metadata and project/scene sessions |
| [src/editor/modes](src/editor/modes/README.md) | Scene-owned editor context, component-selection participants, transition/generation policy and mode requirements |
| [src/editor/assets](src/editor/assets/project_asset_service.hpp) | Project asset discovery and mixed-asset transactions; delegates scene/material truth to existing owners and OS recycling to an injected port |
| [src/scene](src/scene/AGENTS.md) | `SceneDocument`, live entities/components, hierarchy, transforms and stable editor-object identity |
| [src/assets](src/assets/AGENTS.md) | `AssetRegistry`, mesh/material/texture identity, validation and copy-on-write assets |
| [src/io](src/io/AGENTS.md) | Native QDR projects/scenes, hydration, asset dependencies, durable filesystem writes and recovery |
| [src/viewport](src/viewport/AGENTS.md) | Toolkit-neutral camera/workspace, semantic input routing and viewport host contracts |
| [src/render](src/render/AGENTS.md) | Neutral render facades, scene extraction, semantic overlays, backend adapters, GPU resources and render performance counters |
| [src/ui](src/ui/AGENTS.md) | Qt widgets, retained presenters, localization, settings surfaces and raw-input translation |
| [src/app](src/app/AGENTS.md) | Application composition, frame scheduling, active-screen signals and window/project lifecycle |
| [src/platform](src/platform/AGENTS.md) | Win32 integration, dialogs, raw input, native support and fatal presentation |
| [src/core](src/core/AGENTS.md), [src/math](src/math/AGENTS.md), [src/diagnostics](src/diagnostics/AGENTS.md) | Small shared utilities, math, logging and profiling instrumentation |

## Portable core and native publication

The public SDK namespace is `quader::modeling`; consumers include
[quader/modeling/modeling.hpp](src/sdk/core/modeling/include/quader/modeling/modeling.hpp)
and link `quader::modeling_sdk`. It builds with `QUADER_BUILD_NATIVE_APP=OFF`
without native app dependencies. Native callers share source-only
[neutral declarations](src/sdk/core/modeling/neutral/README.md), never SDK `internal` headers.
Old copied SDK package paths, presence macros, external checkout routing and legacy presets stay
banned. The current SDK is first-party source, not an optional external installation.

[`ModelingSdkProjectionAdapter`](src/editor/modeling/services/modeling_sdk_projection_adapter.hpp)
owns the native workspace's portable `ModelingSession`.
[`ModelingObjectBindingRegistry`](src/editor/modeling/runtime/modeling_object_binding_registry.hpp)
owns SDK/native/entity mappings.
[`ModelingHostPublicationTransaction`](src/editor/modeling/services/modeling_host_publication_transaction.hpp)
coordinates SDK, scene, asset, binding, selection and revision publication or compensation.
Do not put another modeling session or mutable topology mirror in a host facade.

SDK results carry structured errors, diagnostics, dirty state and remaps. Identity allocation must
not alias retired handles after undo branching. Preview participants and lifetime policy are
explicit. See the [SDK compatibility contract](src/sdk/core/modeling/COMPATIBILITY.md).

## Scene, selection and history

[`SceneStructuralLifecycleService`](src/editor/commands/scene_structural_lifecycle_service.hpp)
owns duplicate, destroy, group and ungroup across modeled objects, Curves and mixed hierarchies.
Structural history includes registered sub-selection providers, stable identities and exact sibling
ordering. Failure compensation and ordinary undo have different revision semantics; preserve them.

`EditorSelectionService` owns cross-domain selection publication and edit algebra.
`SceneSession::editor_mode()` owns Object/Edit context and participant membership independently
of component selection. Its `EditorModeService` snapshot scopes native picking, operations and
presentation; SDK documents remain the sole owners of component selection. Structural publication
checkpoints the mode context alongside selection and history, while generation checks reject input
from a retired Edit context. See the [mode contract](src/editor/modes/README.md).
`ViewportSceneHitPolicy` and shared scene-surface/occurrence queries own visibility, backface,
occlusion, group promotion and instance policy. A render-visible surface may occlude while locked
or nonselectable. Host selection can change even when SDK selection reports a no-op.

[`SceneEntityState`](src/scene/document/scene_entity_state.hpp) is the durable component inventory
used by QDR, history, clipboard, bindings and rollback. `SceneDocument` is live project-object
truth; legacy QDR object and CSR topology records are validated reader-only compatibility inputs.
See [QDR ownership and persistence](src/io/qdr/README.md).

## Input and presentation

`QuaderKeybindingService` owns stateful binding dispatch. `ViewportPointerGesture` owns press
identity and release reservation; `ViewportInteractionController` owns semantic precedence.
Native tool lifecycles are registered once. Qt translates events and presents state.
[INPUT_KEYBINDINGS.md](INPUT_KEYBINDINGS.md) names the authoritative catalogs and tests.

The shell uses `QApplication`, `QMainWindow` and `QDockWidget` with Qt Fusion and the shared
accessibility-aware Obsidian theme. `ViewportWorkspaceService` owns one to four panes. Production
Vulkan panes use [`QtVulkanCompositionWidget`](src/ui/qt_vulkan_composition_widget.hpp), a
`QRhiWidget`, so scene textures and the ordinary Qt shell reach one window composition.
`QtViewportSurface` remains the input adapter; its composed-mode `QWindow` has no native handle
or window container. Qt owns the Vulkan device, graphics queue and window swapchain. One
[`RenderHost`](src/render/render_host.hpp) borrows that context for the root generation and
coordinates independent offscreen pane targets. Release every borrower before replacing the
Qt root/device. Retained UI must abandon expired owners during project/scene replacement;
see [UI lifetime rules](src/ui/AGENTS.md).

Application, UI and editor APIs consume `RenderHost`, `RenderSceneSink` and `RenderView`.
Each app executable links exactly one `create_app_render_host()` composition. On `main`,
Full and Public 0.1 both use `quader_render_full_host` and Filament. The `quader-crimson`
branch keeps Full on Filament and composes Public 0.1 with `quader_render_crimson_host`,
adapting to the canonical external `Crimson::Renderer`. Both branches retain the shared
modeling, input, scene, UI and overlay changes. Keep renderer selection in final target
composition when carrying shared application changes between them.
The shared runtime does not select backends or own a second renderer implementation.
Activation, dependency provenance and GPU gate status belong in the
[Crimson integration contract](thirdparty/crimson/README.md).

The Crimson host prepares the complete pane set before recording a shared frame. Scene,
preview suppression and editor coverage carry the same generation; validation and resource
preparation must succeed for every candidate before any pane records commands. Publish all
target candidates together after native checks. Rejection retains accepted scene/overlay
coverage and pixels, and a corrected candidate can retry. GPU completion separately protects
render/copy resource retirement. Exact selection stays in the native query owners: until
Crimson implements the optional selection-depth accelerator, it supplies no depth source and
the existing exact CPU surface-query fallback remains authoritative. The
[scene composition probe](tests/crimson_scene_composition_probe.cpp) protects shared-frame
acceptance; [selection hit policy](src/editor/viewport/viewport_scene_hit_policy.cpp) owns fallback.

[`RenderSourceStamp`](src/render/render_source_stamp.hpp) and dependency masks gate snapshots,
hashing, copies and UI updates.
[`ViewportOverlaySubmissionCoordinator`](src/render/overlay/viewport_overlay_submission_coordinator.hpp)
owns semantic overlay composition/submission;
[`InstancedWireRenderBackend`](src/render/overlay/instanced_wire_render_backend.hpp)
owns Full's shared instanced wire resources.
[`MaterialPresentationService`](src/editor/material/material_presentation_service.hpp)
projects editor material state. Quader render adapters own backend translation and lifetime;
Crimson owns its reusable GPU mechanisms in the pinned
[`thirdparty/crimson-renderer`](thirdparty/crimson-renderer) submodule of
`qvertexworks/crimson-renderer`, shared with Crimson Engine. Native configuration
checks that checkout against the recorded gitlink and verifies the installed bgfx
package against the canonical manifest and overlay hashes. Its dependency overlay
is selected by the native preset. The dependency and optional Crimson composition probes
remain available on `main`, but its Public executable does not link the Crimson host.
Portable SDK builds do not consume renderer
sources or native dependency features. Consumers never mutate
modeling/scene/assets. Snapshot and GPU-upload payload lifetimes must be explicit.

## Diagnostics and verification

[src/core/logger.*](src/core/logger.hpp) owns shared Logger policy.
[src/platform/fatal_error_handler.*](src/platform/fatal_error_handler.hpp) owns runtime fatal
hooks and platform presentation. [src/main.cpp](src/main.cpp) installs both at startup.
The isolated [fatal helper](src/platform/fatal_diagnostics_main.cpp) owns optional DbgHelp work;
the parent writes a text report first and bounds the helper's lifetime.
UI and app services do not install global loggers or fatal hooks.

[tests/AGENTS.md](tests/AGENTS.md) maps behavior and guardrail owners. Portable consumers,
native/SDK parity, installed external-consumer checks and command fingerprints protect ownership;
source-text assertions supplement behavior tests. [tools/AGENTS.md](tools/AGENTS.md) describes
the affected-check and regression gate. Both canonical native artifacts and their deployment/Tracy
contracts are defined in [AGENTS.md](AGENTS.md); dependency provenance lives under
[thirdparty](thirdparty/THIRD_PARTY_MANIFEST.md).
