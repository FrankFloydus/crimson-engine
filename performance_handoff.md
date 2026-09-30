<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Large-scene editing performance handoff

Last updated: 2026-09-29. Internal engineering handoff, not public documentation.

## Current status

**The performance problem remains unresolved.** After the latest implementation and build, the user reported: “nothing has changed.” There is no measured evidence that the latest changes improved the complete interaction. Do not report this work as finished or claim Blender parity.

The user has spent substantial time and tokens on repeated iterations. Their current request is to preserve the state in this document and stop. On resumption, use the existing evidence instead of repeating broad Blender/Unreal research or explaining the same architectural differences again. Keep communication and iteration short.

- Checkout: `C:/Users/Drako/Desktop/quader-windows/quader-app`.
- Branch: `feature/asset-browser`; version: `0.1.67`. Preserve both unless instructed otherwise.
- There is a large, pre-existing, uncommitted change set, including the latest performance work. Preserve it all. This handoff is not a commit request.
- Project: `build/vs2026/bevel-width-probe/project/project.qproj`.
- Scene: `build/vs2026/bevel-width-probe/project/Scenes/one.qdr`.
- Captures: `C:/Users/Drako/Desktop/quader-windows/logs/new_performance_test_<N>.tracy`.
- Last analyzed capture: **14**. `new_performance_test_15.tracy` was not present at the expected location when this handoff was written.
- Latest successful app build: `C:/Users/Drako/Desktop/quader-windows/quader-app/build/vs2026/Debug/quader.exe`.
- Successful build/deployment log: `build/component15-app-build-3.log`.

“Component14” and “component15” below are iteration/artifact labels, not product versions. The Debug executable is the optimized Tracy Profile configuration, not a traditional checked Debug build.

## What has actually been verified

The latest component15 changes compiled, deployed, and were launched with Tracy and `--vulkan`. Startup reached the Qt event loop and initialized the scene render bridge. Only the Profile app was built in that iteration; there is no corresponding current Release handoff.

**No tests, naming audit, controlled benchmark, or correctness validation were run for component15**, following the user's explicit request to change code, build, and let them test. Earlier passing focused checks do not validate these subsequent changes. No post-change timing analysis has been performed. Successful compilation and startup are the only technical validation available for this iteration.

The public manual was not changed: this work is intended to preserve existing behavior, and this file is an internal handoff.

## Last measured bottlenecks: capture14

The detailed analysis is already saved under `build/tracy-user-capture-14/`, especially `diagnosis.md`, `workflow-statistics.txt`, `workflow-statistics.json`, and `critical-frames.txt`. Raw extracted event and plot JSON files are alongside them. These measurements predate component15.

| Interaction | Observed cost |
| --- | --- |
| Dense drag, 22.526–25.061 s | 13 changed frames; mean 190.826 ms, median 180.102 ms |
| Input during that drag | Mean 94.112 ms |
| Viewport preparation during that drag | Mean 88.855 ms |
| GPU during that drag | Mean 1.822 ms |
| Four commits with accepted-coordinate overlay handoff for all 6,274 participants | Frame times 593.506, 608.617, 543.380, and 512.624 ms |
| Native commit API in those accepted cases | Approximately 229–243 ms |
| Fifth commit, with fallback | 883.926 ms; overlay submission 550.35 ms |
| Heavy box-selection frame at 69.394 s | 881.129 ms; input 367.746 ms, viewport preparation 511.711 ms |

Heavy drag frames included roughly 35 ms in retained coordinate projection, 33 ms in editable surface preparation, and 25 ms in coarse batch preparation. Heavy box selection still rebuilt face fill/edge/mask display geometry; its overlay submission alone took about 443 ms. Selection query and witness-resolution work also remained substantial.

These zones are nested: **do not add their durations together**. Different manual gestures/populations are not controlled before/after comparisons. The GPU was generally around 1–2 ms; the measured bottleneck was CPU preparation, publication, selection, and display updates.

The previous accepted-overlay change did work narrowly: persistent semantic-base rebuilding disappeared on four accepted commits. It did not make the complete interaction responsive. Likewise, earlier wire reconciliation improvements did not resolve total drag/commit/box-selection time.

## Changes currently implemented, but unverified

### Retained coordinate projection

Owners: `src/editor/projection/mesh_projection_service.cpp/.hpp` and `src/editor/modeling/services/modeling_sdk_projection_adapter.cpp`.

- Added `project_retained_mesh_position_updates(...)`, consuming evaluated position updates and a retained plan without a full SDK coordinate snapshot on every sample.
- Retains source-vertex mappings, compiled ranges, face adjacency, and private compilation scratch. Updates moved ranges and affected faces.
- Compares against the caller's accepted previous projection, so rejected candidates do not silently become the comparison baseline.
- Retains an immutable participant base document and participant-index lookup for the gesture.
- Fully selected UV-mapped polygons use this route only when triangle identities remain valid. Canonical UV/triangulation fallback still exists and can retire the retained plan for the rest of a gesture.

### Lazy accepted mesh publication

Owners: `src/assets/mesh_asset.hpp`, `mesh_geometry_projection.hpp`, `mesh_asset_payload.hpp`, `asset_registry.cpp`, `mesh_wire_geometry.cpp`, and the new `mesh_authored_polygon_document.hpp`; projection-service overloads and adapter commit integration.

- Mesh assets can retain the accepted immutable geometry projection and expose effective vertex/position/index/primitive views.
- Legacy `geometry()`/`geometry_payload()` access materializes lazily; this can still be expensive when a consumer requests it.
- Authored polygon data can pin an SDK read snapshot and provide `.read_view()` / `.read_snapshot()`; legacy pointer access materializes lazily.
- Retained publication avoids eager geometry/authored copying and some repeated validation/hash interning. Content hashing is cached when requested, but its first request can still cost time.
- Neutral header include visibility for `quader_assets` was made PUBLIC in CMake to support the exposed declarations.

### Native read views and overlay projection

Owners: `src/editor/modeling/runtime/cow_polygon_document.hpp`, `runtime/session_selection_overlay.cpp`, adapter selection-projection binding, and SDK neutral document topology/storage implementation.

- Native read owners pin immutable SDK snapshots; read views and vector facades avoid copying polygon storage merely to inspect it.
- Mutation detaches. Explicit legacy `read()` / `topology_payload()` still materialize.
- Overlay lookup, triangulation, and edge enumeration consume pinned read views.
- Some picking helpers still require a materialized document. A full picking API migration was not done.

### Renderer preparation and source consumption

Owners: `src/render/editable_mesh_surface_cache.cpp`, `mesh_renderable_factory.cpp/.hpp`, `mesh_surface_grid_projection.cpp/.hpp`, `mesh_quad_uv_interpolation.cpp/.hpp`, `scene_render_extraction.cpp`, and `src/editor/placement/scene_surface_query.cpp`.

- Stable preview-to-batch mappings bypass repeated effective-map construction, exclusion/material regrouping, and membership lookup when applicable.
- Retains validated topology/index partitions, grid/quad face bindings, and mapping/transform data.
- Unique geometry avoids the canonical-sharing promise/lock route.
- Scene extraction and surface queries read retained geometry streams before requesting legacy materialization.
- Coverage and fallback frequency of these routes have not been measured in the latest build.

### Selection display retention

Owners: `src/render/overlay/overlay_renderable_cache.cpp/.hpp`, `overlay_proxy_factory.cpp/.hpp`, `overlay_renderable_proxy.hpp`, and `overlay_edit_wire_retention.cpp/.hpp`.

- Retains a face triangle arena from source wire/depth geometry, with object/face/vertex mappings.
- Fill passes share buffers; membership updates indices and changed colors rather than recreating every face vertex. Empty selection retains the arena.
- Selected face edges and selected edges use retained wire slots; shared position bindings support the associated passes.
- Ambiguous geometry, non-component inputs, and full source replacement retain fallback paths.
- Pixel/depth correctness, actual retention hit rate, and lifetime/history behavior remain unverified. The field `slots` was renamed `triangle_slots` to avoid Qt's macro during compilation.

### Selection query and integration changes

- Face-crossing selection uses the exact polygon/rectangle intersection rule when a face boundary is available, avoiding centroid-only false hits on concave faces.
- Front-bounded X-ray face crossing can use identity-only collection without world witnesses, sorting, or raycasts. Camera-plane/affine and center-only-provider fallbacks remain.
- Two existing fast-path regressions in `tests/modeling_selection_performance_tests.cpp` still expect FaceCrossing fallback. Their expectations need reconciliation with the new behavior; they were not updated or run in this iteration.
- Ordinary tool-source/volatile-overlay setup no longer forces a complete selection projection when there is no hover geometry requiring it.
- `src/scene/scene_bounds.cpp` uses effective position views; `src/editor/material/project_material_usage_service.cpp` reads resident authored/primitive views rather than forcing full write-source materialization.
- Correction to an earlier diagnosis: `scene_mesh_is_shared` already uses cached `mesh_binding_reference_count`. It is **not** an outstanding quadratic scene scan.

## Remaining work, in priority order

1. **Establish what the latest binary actually does.** Obtain the next capture from component15 and identify whether direct projection, stable batch preparation, and retained selection-display routes are reached. The user's report is negative; do not infer success from the amount of code changed.
2. **Locate the remaining expensive boundary crossings.** Track legacy document/geometry materialization, first-use hashes/validation, per-object publication, derived attributes, arena invalidation, and fallback reasons. Check final pointer update, commit, subsequent navigation, and the next gesture so work is not merely deferred.
3. **Resolve the accepted-overlay sharing cliff where supported by evidence.** The commit handoff still has an all-or-nothing proof over participant geometry, exact SDK content, indices, instance restrictions, and population. One participant without a matching projection can reject the whole batch. Capture14 contained four accepted commits and one expensive fallback. Prefer explicit per-participant reasons and independent eligible/fallback handling where ownership and ordering allow it.
4. **Finish selection work based on actual semantics.** Ordinary visible GPU-ID/depth selection is a separate path and previously fell back heavily. The user's profile uses `BackfaceSelection=true` (X-ray), which intentionally needs CPU rules. Do not change that preference to manufacture a faster result or claim the visible GPU path fixes X-ray selection.
5. **Validate the broad changes when the user authorizes verification again.** Required concerns include immutable snapshot lifetime, one authoritative modeling owner, rejected updates, final unsent pointer samples, commit/undo/redo/cancel, save/reload, exact concave/near-plane selection, reflected winding, transparent ordering, overlay depth/pixels, and subsequent navigation. Existing test executables/results may be stale.
6. **Use the real workflow as acceptance.** All 6,274 objects, repeated mixed vertex/edge/face edits, actual UI selection and pointer release, commit, undo, and navigation. A favorable single-mesh/shared-geometry measurement remains a bounded microbenchmark, not evidence of acceptable end-to-end behavior.

The application already retains SDK topology, GPU buffers, batches, and shared overlay positions. It is inaccurate to say that all topology is rebuilt from scratch on every frame. The unresolved issue is which consumers still rebuild or materialize costly data and how often the retained routes fall back. Preserve canonical modeling/CPU semantics while addressing those costs.

## Existing evidence to reuse

- `build/component14-before-untouched.log/.xml`: valid controlled baseline, 63.546 s, all 6,274 objects and repeated mixed editing, including final pointer update, commit, undo/redo, and navigation.
- `build/component14-before-stages.json`: parsed baseline. Drag means approximately 150–246 ms; commit/release stages approximately 628–970 ms; vertex/face marquee release approximately 672/793 ms. There is no corresponding controlled component15 after-run.
- `build/component14-before.log/.xml`: **invalid timing run**, discarded after physical modifier-key interference.
- `build/tracy-user-capture-13/` and `build/tracy-user-capture-14/`: existing manual trace analyses.
- `build/blender-component-comparison/findings.md`: Blender comparison and its limitations; associated scene/results are in the same directory.

The scene contains 6,274 occurrences, about 163,670 authored vertices, 302,242 edges, 151,120 faces, and 604,484 corners. It is many small meshes, not millions of authored vertices. The Blender fixture assigned independent data to each occurrence. Blender's non-modal translate measured about 111–145 ms for these independent meshes, versus 15–16 ms when combined into one disconnected mesh. Those figures exclude modal display/overlay/undo costs and are **not drag FPS or a parity target**.

Blender evidence already identified retained transform records referencing mesh coordinates and partial connectivity/normal/triangulation updates. Sources: `source/blender/editors/transform/transform_convert_mesh.cc` and `transform_mode_translate.cc`, under `C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main`. Unreal is at `C:/Program Files/Epic Games/UE_5.8/Engine`. Further source reading should answer a concrete remaining implementation question.

## Resume/build notes

Use the canonical `build/vs2026` tree. Close only verified running Quader processes from this checkout before rebuilding, as already authorized. Do not bump the version, reset dirty files, clean-rebuild, upgrade Filament, or start a broad audit merely to resume.

For another explicitly requested profiling iteration, build the Profile target and use the existing launch helper:

```powershell
cmake --build --preset vs2026-debug --target quader
# Inspect tools/boot_tracy.ps1 parameters before invoking it.
```

Tracy binaries are under `C:/Users/Drako/Desktop/quader-windows/_SOURCES/windows-0.13.1/` (`tracy-profiler.exe`, `tracy-csvexport.exe`). The last launch used Vulkan. The latest successful app build log is `build/component15-app-build-3.log`; earlier component15 build attempts failed and were repaired before that success.

The user currently wants this handoff only. No further implementation, checks, benchmarks, or builds were performed for the documentation request.
