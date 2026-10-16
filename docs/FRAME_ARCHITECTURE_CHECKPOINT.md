# Frame architecture checkpoint

Recorded 15 October 2026. This checkpoint covers the first renderer architecture
step: immutable frame input, explicit prepared-resource ownership and pass
planning while preserving the existing image. It does not implement the later
ARPG shadow policies, ambient attenuation, player light rig, lighting channels,
new transparency composition or scene antialiasing.

## How to read this checkpoint

This is the Step 1 implementation and measurement record. Its statements about absent effects and write-only depth describe that build. Subsequent steps added shadow policies, readable depth/receiver targets, Ambient Darkness and character lighting without replacing the prepared-frame ownership boundary. Read [Renderer and character lighting](RENDERER_AND_CHARACTER_LIGHTING.md) and [the authoritative architecture](ENGINE_ARCHITECTURE.md) for the current pass/resource contracts. Retained measurements and hashes below have not been refreshed by this documentation update.

## Implemented boundaries

- `RenderWorldSnapshot` freezes light, particle, material-time and caster values
  for one presented world. Scene/Game receivers use camera-local vectors and
  explicit logical view identities. Exact prepared model/material versions and
  GPU leases are retained by the complete frame owner.
- The editor's `prepareEditorRenderFrame` performs asset lookup, visibility and
  resource preparation before submission. `Renderer::renderFrame` accepts the
  immutable prepared frame, validates it, and submits Scene/Game through the
  same world generation. It does not read scene documents or publish assets.
- Scene and Game share one procedural-environment owner, published once per
  owner frame. The production producer supplies the same world environment
  and material clock for both requests. Independently authored environments
  inside those two requests are outside this API's current usage contract.
- `RenderPassPlan` names implemented passes, their resource accesses and owners,
  and validates producers, access contracts, enabled work and physical order.
  Renderer execution uses the plan's pass views; the post chain is executed
  by iterating the planned Glow, tone map, grid and encode entries. Picking and
  thumbnail readbacks use their planned adjunct slots.
- Logical view identity is separated from physical bgfx slots. This migration
  centralizes and preserves existing numeric IDs and execution order. It does
  not introduce a dynamic view allocator, general graph compiler or transient
  resource aliasing.

At this checkpoint, the scene-color stage retained its existing background, opaque/masked
geometry and shared sorted transparent mesh/particle composition. Scene depth
was write-only; depth attachment tests were distinguished from sampled depth
reads in the resource contract. Later readable-depth and ambient stages added
real producers, resources and consumers. Step 1 did not represent them as empty
passes, and its saved pass-plan output must not be treated as the current plan.

See [engine architecture](ENGINE_ARCHITECTURE.md) for ownership contracts and the
[large-scene guide](LARGE_SCENE_PERFORMANCE_GUIDE.md) for remaining scaling work.

## Measurement workload

The maintained `forward_plus_room_benchmark` diagnostic uses a hidden D3D11
window and production asset/resource preparation, Scene and Game submissions.
Each viewport is 909 × 684. Vsync is disabled; the diagnostic explicitly flushes
the D3D11 immediate context because these offscreen views never call Present.
It does not control GPU clocks or background desktop applications.

The frozen fixture contains 86 combined model instances, fourteen point lights
and one directional light. It uses retained managed/native source copies under
`build/verification/frame-architecture-baseline`, never the current authored
project. Every successful performance run uses `--saved`, 300 minimum warmup
frames and 2,000 measured frames for each phase:

1. `main`: authored non-Quader scene records.
2. `room_no_shadows`: saved combined room, all shadow flags disabled.
3. `room_static_shadows`: saved combined room, fully prepared static shadows.
4. `room_moving_player`: same room; player subtree and Game camera replay the
   same sixty-frame horizontal translation pattern.

The room normally has 32 visible Scene models and 49 visible Game models. The
moving Game count averages 49.413 as objects cross the frustum. This diagnostic
does not include editor widgets, browser thumbnails, Quader watching, gameplay
physics, AI or an animated crowd. These numbers are renderer workload results,
not full editor or game FPS claims.

CPU measurements include resource pumping, shadow preparation, receiver
preparation and renderer submission. Wall time also includes `bgfx::frame()` and
submission backpressure. GPU timestamps are asynchronous phase samples; a raw
row does not imply that its CPU/GPU measurements belong to the same submitted
frame. View timing sums and complete frame timing have different scopes.

## Baseline identity

The original baseline A/B retained the Release build from 15 October at 12:48.
A Quader reimport fix subsequently landed, so baseline C retains and measures
the actual current Release build from 13:46 immediately before integration of
the frame architecture change. Baseline C is the primary timing comparison.

The current pre-change `Renderer.cpp.obj` SHA256 exactly matches the original
retained renderer object:

`59651A574FCFE93C1047D45B36EC6A1EC6EBFEDEC34C585252D08EA633095643`

There is extensive existing uncommitted work. A Git commit alone does not
reproduce either baseline. The retained executable, object/library, shader,
compiler, source and fixture hashes are the authoritative identities:

- Original: `build/verification/frame-architecture-baseline/baseline-identity.json`
  and `sha256-files.json`.
- Current pre-change build:
  `build/verification/frame-architecture-baseline/postbridge/baseline-identity.json`
  and `sha256-files.json`.

The renderer sources in `postbridge/source/crimson-renderer` reuse the original
pre-change source snapshot. `postbridge/working-tree-after-restore` records a
later working-tree snapshot containing restored implementation source; it is
explicitly not the source of the baseline executable. The retained production
objects establish which renderer was actually measured.

## Baseline performance

Each entry is mean milliseconds. A/B represent additional uncontrolled-clock
baseline runs. C is the current pre-integration build. All runs recorded 2,000
frames in each phase.

| Phase | A wall / GPU | B wall / GPU | C wall / GPU | C wall p95 |
| --- | ---: | ---: | ---: | ---: |
| Main | 0.8633 / 0.4523 | 1.0887 / 0.4687 | 0.7655 / 0.3420 | 0.8751 |
| Room, shadows off | 2.6023 / 0.5443 | 2.7040 / 0.5495 | 2.1725 / 0.7998 | 2.4907 |
| Room, cached shadows | 6.1074 / 1.2353 | 7.3740 / 1.3578 | 5.4132 / 1.7091 | 6.0000 |
| Room, moving player/camera | 8.5573 / 2.6217 | 9.5399 / 2.7406 | 6.7147 / 2.1742 | 7.2921 |

The hardware is an NVIDIA GeForce RTX 4090, driver 610.74. C started at P8,
210 MHz graphics/405 MHz memory, and ended at P0, 1,755 MHz graphics/10,501 MHz
memory. A/B/C variation is substantial. Small timing differences cannot
honestly establish a performance win or regression without considering that
variation.

Baseline C room counters and memory:

| Phase | Draws | Shadow draws | Point faces / directional projections written | Shadow bytes | Texture/program bytes | Model bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Main | 34 | 0 | 0 / 0 | 101,451,264 | 10,761,674 | 1,289,016 |
| Room, shadows off | 101 | 0 | 0 / 0 | 0 | 13,794,830 | 1,492,548 |
| Room, cached shadows | 101 | 0 | 0 / 0 | 507,256,320 | 41,101,494 | 2,137,830 |
| Room, moving player/camera | 1,028.089 | 888 | 36 / 8 | 507,256,320 | 41,101,494 | 2,137,830 |

The static phase explicitly asserts that cached point/directional shadow maps
are not rewritten. Existing point-shadow allocation dominates the room shadow
memory. Redesigning that policy belongs to the next shadow architecture step.

## Visual comparison method

Baseline B contains eight decoded RGBA8 PNG snapshots: Scene and Game for each
phase. The old room harness lacked image export, so only its translation unit
was recompiled with an optional `--capture` switch and linked against the
retained pre-change production objects/libraries. The renderer implementation
was not rebuilt for that image baseline. Its source and link/compile response
files are retained alongside `bin/room_baseline_images.exe`.

Captures replay sample 1,999, including its material evaluation time and camera
pose, after that phase's timing loop. GPU readback-only frames occur after
timing and before the next phase's warmup. Readback destinations remain alive
through renderer lifetime, including exception paths. Baseline images were
inspected to confirm that they contain the lit room and its shadows.

The B images predate the intervening Quader reimport fix. The production
renderer object is identical, and `--saved` uses the same already-imported
world, but this provenance must accompany the comparison rather than implying
that C itself exported images.

All thirteen non-Quader Crimson libraries are byte-identical between those
builds, as are all retained room production objects except
`QuaderSceneIntegration.cpp.obj`. The harness object also differs. The audit is
retained as `postbridge/library-comparison.json` and `object-comparison.json`.
The fixture's twenty material graphs contain no Time or Panner nodes. The
submitted material context remains fixed at zero for the baseline and
candidate. Baseline resource preparation observed trajectory time; the new
producer consistently prepares resources with the captured zero context.
Because this fixture has no time-dependent material nodes, that preparation
context difference cannot change its image. Dynamic material behavior is
covered by the focused material checks, rather than claimed from this room.

`scripts/Compare-FrameArchitecture.py` compares summary workload identity, raw
CPU/GPU means and percentiles, counters/memory and decoded PNG pixels. Image
results include exact equality, changed-pixel count, mean/maximum absolute
channel difference and RGBA SHA256. Its same-directory self-check finds eight
of eight captures exactly identical.

## Candidate results

The Release integration build succeeded. Candidate executable, harness,
compiler, shader and source identities are retained at
`build/verification/frame-architecture-candidate/candidate-identity.json` and
`sha256-files.json`. The benchmark executes the actual production
`prepareEditorRenderFrame` producer and `Renderer::renderFrame` submission API,
including frame construction and validation cost in wall timings.

The candidate completed all four phases with 300 minimum warmup frames and
2,000 measured frames each. Baseline C and candidate measurements:

| Phase | Before wall mean | After wall mean | Wall change | Before GPU mean | After GPU mean | Before / after wall p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Main | 0.7655 ms | 0.7917 ms | +0.0263 ms / +3.44% | 0.3420 ms | 0.3455 ms | 0.8751 / 0.8959 ms |
| Room, shadows off | 2.1725 ms | 2.2398 ms | +0.0673 ms / +3.10% | 0.7998 ms | 0.7976 ms | 2.4907 / 2.5785 ms |
| Room, cached shadows | 5.4132 ms | 5.4166 ms | +0.0034 ms / +0.06% | 1.7091 ms | 1.6908 ms | 6.0000 / 5.9178 ms |
| Room, moving player/camera | 6.7147 ms | 6.8181 ms | +0.1035 ms / +1.54% | 2.1742 ms | 2.1768 ms | 7.2921 / 7.4601 ms |

Moving-phase renderer submission increased from 1.8927 to 1.9405 ms
(+0.0478 ms). The complete moving-phase wall mean increased by 0.1035 ms.
These measurements do not establish a speedup. The observed wall increases
are smaller than the preceding A/B/C run-to-run variation, so the retained data
cannot separate a small true preparation cost from desktop scheduling/clock
variation. The architecture now has explicit ownership and pass dependencies;
performance remains measured rather than assumed.

The candidate began at P8, 210 MHz graphics/405 MHz memory, and ended at P3,
795 MHz graphics/5,001 MHz memory. GPU means remained within 0.019 ms of C in
every phase. All per-phase mean draw/shadow counters and tracked resident
shadow/texture/program/model bytes exactly match C. This is not a claim that
total process or driver memory is unchanged; CPU frame storage and unrelated
desktop GPU allocations have different measurement scopes.

**All eight candidate Scene/Game snapshots are exactly identical in decoded
RGBA bytes to baseline B.** Every changed-pixel count and maximum channel
difference is zero. This includes shadows disabled, cached static shadows and
the replayed moving Game camera. The frozen fixture and the automated render
checks establish preservation for those workloads; in-editor acceptance remains
the user's check.

### Focused checks

Nineteen distinct focused checks ultimately passed:

- CPU/Noop: `directional_shadow_math_core`, `quader_scene_core`,
  `quader_bridge_core`, `environment_presentation_core`, `scene_gameplay_core`,
  `play_runtime_core`, `material_graph_publication_core`, `render_scene_core`,
  `scene_extraction_core`, `render_pass_plan_core`, `editor_render_frame_core`,
  `shader_preflight_core`, `gpu_streaming_core`, `point_shadow_math_core` and
  `preview_dependencies_core` (15/15, 12.10 seconds).
- D3D11: `native_resource_core` (9.18 seconds), `point_shadow_render_core`
  (6.78 seconds), `forward_plus_render_core` (3.36 seconds), and
  `material_mapping_render_core` (successful isolated rerun, 9.63 seconds).

The initial material-mapping CTest invocation failed because the authored
`smoke_07.png` had been deleted from the live project. The same unchanged test
binary was rerun with identical candidate shaders and the retained smoke
fixture under `build/verification/frame-architecture-material-fixture`. That
run passed. No authored file was recreated or modified. Both the initial failed
log and successful isolated rerun log are retained; this limitation must not be
reported as an all-green original CTest invocation.

## Retained evidence and reproduction

Evidence is local and disposable; no authored asset is modified by these
diagnostics:

- `build/verification/frame-architecture-results/before-saved-a/`
- `build/verification/frame-architecture-results/before-saved-b/`
- `build/verification/frame-architecture-results/before-saved-c/`
- `build/verification/frame-architecture-results/after-saved-a/`
- Corresponding `.log` files and `hardware-*.csv` files in the results root.
- `comparison-baseline-variance.json`, `comparison-baseline-b-c.json` and
  `comparison-self-check.json` generated by the comparison script.
- `comparison-candidate-timings.json` compares C with the candidate;
  `comparison-candidate-images.json` compares B captures with the candidate.
- `focused-cpu-tests.log`, `focused-gpu-tests.log` and
  `material-fixture-tests.log` preserve the actual focused check results.

The initial invocation named `before-a.log` omitted `--saved`, remained in
material preparation and was stopped before recording measured phases. It is
retained for provenance and excluded from all timing tables.

Reproduce the current baseline from the repository root in PowerShell:

```powershell
$baseline = (Resolve-Path 'build/verification/frame-architecture-baseline').Path
$current = Join-Path $baseline 'postbridge'
& (Join-Path $current 'bin/forward_plus_room_benchmark.exe') `
    (Join-Path $baseline 'engine-original86') `
    (Join-Path $baseline 'frozen-native/q-test/q-test.qproj') `
    '6aeb7097-5046-450e-a451-a8b4518aa095' `
    (Join-Path $current 'shader-tools/shaderc.exe') `
    (Join-Path $current 'shaders') `
    (Join-Path $baseline 'bgfx-includes') `
    --saved --warmup 300 --samples 2000 `
    --output 'build/verification/frame-architecture-results/before-repeat'
```

Only one diagnostic should own the GPU during measurements. Do not run the
interactive renderer or another GPU check concurrently.

The retained candidate can be reproduced with the same fixture and parameters:

```powershell
$baseline = (Resolve-Path 'build/verification/frame-architecture-baseline').Path
$candidate = (Resolve-Path 'build/verification/frame-architecture-candidate').Path
& (Join-Path $candidate 'bin/forward_plus_room_benchmark.exe') `
    (Join-Path $baseline 'engine-original86') `
    (Join-Path $baseline 'frozen-native/q-test/q-test.qproj') `
    '6aeb7097-5046-450e-a451-a8b4518aa095' `
    (Join-Path $candidate 'shader-tools/shaderc.exe') `
    (Join-Path $candidate 'shaders') `
    (Join-Path $baseline 'bgfx-includes') `
    --saved --warmup 300 --samples 2000 --capture `
    --output 'build/verification/frame-architecture-results/after-repeat'
```

Compare completed runs without launching the renderer:

```powershell
python scripts/Compare-FrameArchitecture.py BEFORE_DIRECTORY AFTER_DIRECTORY `
    --output 'build/verification/frame-architecture-results/comparison.json'
```

The comparison script requires Pillow. It reports missing image pairs instead
of treating their absence as visual equivalence.
