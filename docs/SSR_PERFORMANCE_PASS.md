# SSR performance pass — 2026-10-23

The shared compute path materially reduced SSR cost in two frozen room
diagnostics. With the editor closed, whole-frame GPU elapsed means are
**17.3790→12.3866 ms** at the floor pose and **15.9162→11.0052 ms** at the saved
horizontal pose, reductions of **28.73%** and **30.86%** respectively.
These static results do not establish a reduction of the original moving
gameplay capture from approximately 35 ms to 11 ms. Both pairs preserve matching
scene inputs/settings and record editor-process absence before and after each
run. Contact shadows remain a substantial separate bottleneck.

## Original gameplay evidence

The official Tracy 0.14.1 capture is
`captures/profiling/gameplay-20261023-081123-945-95228-0/capture.tracy`, SHA256
`2fe6904b503ff36689b15e62706473f79471224e26309148a47e01380b97a8b8`.
Its official MCP analysis is retained in `analysis-latest/summary.json`, with
the reconstructed GPU hierarchy in `analysis-gpu/budget-summary.json` beside
the capture. The manifest identifies Engine
`943b2079b2652bd685a634cd3fe9b22143054955` with a dirty worktree and clean renderer
`82607bba928830e530dabc0f5780893c7c171d03`.

Across 697 complete GPU submissions, nonoverlapping view-root intervals
average 34.8465 ms. The reconstruction contains 19,921 view-root intervals with
zero root overlaps; it does not add nested inclusive timestamps together.

| GPU work in the gameplay trace | Mean per complete submission | Share of view-root budget |
|---|---:|---:|
| Main opaque SSR graphics view 372 | 22.7100 ms | 65.17% |
| Complete SSR family, including hierarchy, reconstruction and composition | 23.2275 ms | 66.66% |
| Contact-shadow trace | 8.4169 ms | 24.15% |
| Complete SSR family plus contact-shadow trace | 31.6444 ms | 90.81% |

Main opaque SSR occurs twice per submission. Its approximately 11.355 ms
average **per occurrence** is therefore not its total per-submission cost.
The complete SSR family and its main interval are alternative attribution
scopes, not independent quantities to add.

The capture has one 916,712-pixel Game view; Scene pixels remain zero. Medium
SSR, SSGI, SSAO and SMAA are enabled. Queued/running resource plots remain zero,
and the manifest records zero shader compiler invocations. Complete editor
FrameMark intervals average 37.6963 ms, with p50 36.6105 and p95 54.5616 ms;
these are process/frame intervals, not GPU shader execution measurements.
The trace establishes SSR and contact tracing as priorities; asset loading
and the cheap reflection reconstruction stages do not explain this capture.

## Implemented SSR change

Opaque/masked material draws capture exact FP32 world position, evaluated
normal/roughness and geometric plane into Raw, Guide and Filter A. Scene
depth/stencil remains last in the three-color-plus-depth framebuffer and
preserves receiver ownership. Shared `cs_ssr_trace` reads these detached inputs
and immutable unfogged opaque-plus-sky radiance/Hi-Z, writing positive
premultiplied incoming radiance/confidence into Filter B. Three reconstruction
dispatches follow **B→Raw→A→B**. A second exact-material draw composes B using
the receiver's BRDF, signed environment replacement, coverage and foreground
fog attenuation once.

Compute bindings 0–5 retain DX11's limit and the material path's twelve graph
textures. Full-resolution deterministic **1/4/12 visible-GGX rays**,
**64/128/256 hierarchy budgets**, four refinement steps, one-sided leaf
acceptance, conservative odd-size footprints and exhausted-ray fallback remain.
Opaque hits use depth-guided mip-zero donor radiance. Receiver-plane self
rejection stays at mip zero instead of immediately revisiting the occupied
parent; ordinary empty intervals may still ascend.

Alpha/glass and runtime-faded opaque/masked materials retain direct inline
tracing with their own normals and guarded source filter. Transmission and
refraction source order remain intact. No transparent donors, temporal history,
offscreen recovery or lower-resolution tracing were added. Material ABI 35 and
source generator 36 remain unchanged. Reflection fragments retain the complete
inline tracer for runtime fades, so this change establishes no first-time
material-compilation improvement.

Implementation ownership is in the canonical renderer's
`src/forward/SsrGpu.cpp`, `src/forward/ForwardPipeline.cpp`,
`src/forward/RenderPassPlan.cpp`, `src/shaders/cs_ssr_trace.sc` and
`src/shaders/ssr_trace.sh`, with named-program registry/reload integration in
the Engine's `src/resources/ShaderCompiler.cpp` and `ShaderReloadSession.cpp`.
See [the current SSR contract](ENGINE_ARCHITECTURE.md#screen-space-reflections)
and the canonical renderer's `docs/GPU_RAY_TRAVERSAL.md`. The measurements
identify preserved baseline and measured implementation payloads. Canonical
renderer commit `7b3c01e92c578dcde8b40be0084cfa2026781b0d` contains the shared
SSR path and the exact atlas-address helper described below and is published
on shared `main`; the matching Engine pin is recorded by the integration owner.

The four detached FP32 images use **64 bytes/pixel**, about **126.6 MiB** at
1920×1080. With the source pyramids and reduction scratch, SSR storage totals
about **205.7 MiB per active 1080p view**, excluding optional GI/AO/refraction
readable depth.

## Matched static measurements

Evidence lives in
`build/diagnostics/gpu-hotpaths-20261023-081737/paired/`. The preserved baseline
payload is in the sibling `baseline/bin`; the new payload is
`build/Release/bin`. Both read the disposable `cc-gpu-3a421ab1` project.
Timed work freezes authored geometry and material time at zero and excludes
Play physics, movement, editor UI and particle simulation. Resource preparation
finishes before measurement.

Both pairs use Direct3D 12 on the same RTX 4090, a Game-only **1304×703** view,
SMAA, point-shadow resolution 1024, 31 world models, three point lights and
one directional light. Each timed frame prepares 16 Game models and submits
**66 draws and 24,908 triangles**. Compute count changes from 84 to 85 because
tracing becomes an explicit dispatch. Medium SSR retains Strength 1, Max
Distance 50 m, Max Roughness .6, Depth Tolerance .077 m and Ray Bias .002 m.
The eye is `(7.191358, 1.65, 9.860723)`, FOV 90°, near .05 m and far 2048 m.
Floor looks equally toward -X/-Y; saved looks horizontally toward -X.
Camera and recorded controls match exactly within each before/after pair.

| Pose and report directory | GPU elapsed mean / p50 / p95 (ms) | SSR family mean sum 372+373+374 (ms) | Unique GPU samples |
|---|---:|---:|---:|
| Floor — `verified-baseline-floor` | 17.3790 / 17.2739 / 18.4504 | 7.7064 | 480 |
| Floor — `verified-compute-floor` | 12.3866 / 12.3986 / 13.6806 | 2.5150 | 480 |
| Saved — `verified-baseline-saved` | 15.9162 / 15.5156 / 17.6282 | 6.6103 | 480 |
| Saved — `verified-compute-saved` | 11.0052 / 10.7295 / 12.1569 | 1.9983 | 480 |

The floor pair reduces recorded whole-frame elapsed mean by **4.9924 ms
(28.73%)** and SSR-family mean sum by 5.1915 ms. The saved pair reduces it by
**4.9110 ms (30.86%)** and SSR-family mean sum by 4.6120 ms. These current cases
include the shared SSR path and exact atlas-address helper; the separate helper
measurements below establish no large contact gain. Earlier floor and saved
pairs showed a similar architecture improvement but lacked per-run editor-state
records and are retained as exploratory evidence. Shared-compute controls using
the old versus adjusted traversal were close; no separate meaningful speedup
is assigned to the small traversal adjustment.

Baseline view 372 contains pyramids and inline raster tracing. New view 372
contains pyramids and receiver capture; new view 373 contains shared tracing
plus three filters. View 374 remains exact-material composition. Comparing
view 372 alone omits work moved to 373. The ordered SSR-family mean sum is
descriptive attribution, not a per-frame distribution: view queries publish
independently, so their p50/p95 values must not be summed.

All four reports have zero timed asset loads, compiler processes,
queued/running resources and resource failures. Raw GPU CSV frame IDs were
checked for uniqueness, complete measured-window coverage, matching counts
and means. Each pair preserves matching SHA256 manifests for **all 56 Content
files**, including scene/environment/material inputs. Compiler bytes have
identical SHA256
`19dc7dff8ecfd4945cd983d5b611fda0ab61ddb0074cd30d7b50345da4fbb298`,
and all ten compiler include files match. See `paired/summary.json`'s
`verified_matched_pairs` and each run's `inputs-sha256.json`. All eight
`editor-before.json`/`editor-after.json` files contain explicit empty process
arrays. An earlier aborted attempt's state is separately retained in
`verified-baseline-floor/editor-before-aborted-first-attempt.json`.

These runs are serial with different warmup durations. Before/after clock
and temperature snapshots are retained: warmed snapshots are 2685 MHz graphics,
10251 MHz memory and 48–52°C; the first baseline pre-launch snapshot is
750/5001 MHz at 47°C. They do not continuously measure or control clocks during
timing. Editor-process checks bracket each verified run; they are not a
continuous process monitor. Earlier hard-sun work was found to compete with
the reopened editor; `contact-hard-reference-saved`,
`contact-hard-specialized-saved-repeat` and `contact-hard-specialized-floor`
are compromised and excluded from performance conclusions. The primary table
above uses only the subsequent editor-closed confirmation cases.

Whole-frame timestamp elapsed includes any queue-feeding gaps inside its
bracket and is not active GPU instruction time. Frozen results cannot replace
a comparable moving-gameplay capture or a startup/material-compilation test.

## Contact-shadow status

The SSR results retain the existing contact implementation, all **32**
source-coverage samples, camera-visibility checks and seam protection.
No change to contact coverage, tracing or visibility rules is claimed by this
report. The canonical `shadow_ray_geometry.sh::shadowRayFetch` helper replaces
division/modulo with exact shifts/masks for the existing power-of-two atlas
width and nonnegative integer geometry addresses. It fetches identical texels;
the shared general atlas accessor and data layout remain unchanged. The private
SM5 compilation removed 44 integer-division instructions, which is compiler
evidence rather than a measured hardware occupancy or frame-time improvement.

Full-cover-first and camera-packet candidates gave no useful end-to-end gain.
No-camera, packet-only and forced-normal gates were diagnostic controls with
altered correctness or fallback behavior. The exact atlas-address candidate
preserves fetched texels and geometry predicates, but repeated floor results
are effectively flat: 11.7827 ms reference versus 11.7965 ms candidate.
The nearby saved-pose comparison is 10.3981 versus 10.2774 ms, with pre-opaque
compute view 369 at 6.1855 versus 6.0523 ms. That view also includes other work.
These small serial differences establish no broad contact performance claim.
Contact tracing remains the next unresolved target; improvements must preserve
physical coverage and the agreed one-pixel seam limit.

## Integration validation

The final incremental Release build completed with zero errors. Six focused
CPU checks covering the renderer/profiling contracts passed in 0.67 s. Final
GPU quality checks passed for `directional_shadow_quality_core`,
`clustered_lighting_gpu_core` and `directional_shadow_quality_dx11_core`
(151.18 s total), exercising the exact lookup helper on both native backends.
The focused SSR rendering fixtures had already passed on DX12 and DX11 with
the same SSR source; the later helper changes shadow geometry addressing only.
The earlier complete material graph shader check passed in 1604.79 s.

Final warm material-cache validation found 12/12 expected bundles, 64 cached
programs, zero shader compiler processes and zero failures in 557.602 ms.
That checks reuse of this prepared cache; it does not establish a cold material
compile-time or real editor startup improvement. Native trace save/analysis
and user gameplay acceptance remain separate evidence.

## Official after-change trace verification

The first new diagnostic capture,
`captures/profiling/verification/ssr-compute-after-20261023-115352-199896-91176`,
saved CPU data but contained **zero native GPU zones** and is excluded from
GPU evidence. A successful file save was not treated as GPU coverage.
The diagnostic enabled profiling after constructing `Renderer`, whereas bgfx
creates its native Tracy GPU contexts during initialization. The harness now
enables profiling before `Renderer` construction, requires a connected client
and positive native-GPU-zone count with zero dropped zones, and writes those
counters into its report. The ordinary application already used the correct
initialization order.

This correction affects the diagnostic's explicit `--tracy` path; the four
paired measurements above used Tracy disabled and their behavior is unchanged.

The replacement trace is
`captures/profiling/verification/ssr-compute-after-20261023-115800-939032-30156/capture.tracy`,
SHA256 `a4bc185514eba64432ff6c79b03e98d3c0c3d8e456a588f9ad56c0099cfe9a7c`.
Official MCP analysis reports **499,013 CPU events and 308,047 native GPU
events** in one Direct3D 12 graphics/compute context with 41 GPU labels.
The diagnostic report separately records 270,592 native zones during its
measurement accounting, zero native zones dropped and 2,400 unique GPU-query
samples. These counters have different scopes and are not additive.

The messages `SSR diagnostic measurement begin` and
`SSR diagnostic measurement end` bound the calibrated analysis window. Within
it, **2,400 completed `SSR ray tracing` occurrences average 1.0453 ms**, and
**2,399 completed `Shadow contact trace` occurrences average 5.4533 ms**.
These are per-occurrence native intervals in this frozen saved pose, not a
moving-gameplay comparison or an estimate of active shader execution. GPU work
near the CPU marker boundaries can belong to adjacent submissions because of
queue latency. Named intervals are inclusive children of their numeric views
and must not be added to those parent intervals.

The whole-capture dropped-zone plot has maximum zero. Timed resource loads,
compiler processes and queued/running/failing resources are zero; Content
hashes remain unchanged and the editor-absence monitor observes no reopening.
See the [official MCP analysis](../captures/profiling/verification/ssr-compute-after-20261023-115800-939032-30156/analysis-mcp/analysis.md)
and [bounded-window analysis](../captures/profiling/verification/ssr-compute-after-20261023-115800-939032-30156/analysis-bounded/analysis.md)
with their JSON/raw exports beside them. Recurring `415 Game: sRGB` anchors
identify **2,399 complete GPU timestamp cycles**, with **zero numeric-root
overlaps** and a disjoint view-root budget averaging **10.7611 ms**. The
initial one-shot procedural-sky view is not used as a recurring frame anchor.
This is an independent static Tracy budget: it is neither the benchmark's
whole-frame bracket nor a paired performance comparison. Connected profiling
also adds instrumentation overhead, so its values are not substituted into
the disconnected comparison table. The manifest records
`official_analysis_verified: true` after CPU/native-GPU presence, trace SHA256,
measurement markers, complete cycles and zero-drop plots were checked.

## Research and source comparison

The local Godot snapshot is
`C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master`.
`version.py` identifies 4.7 beta; the snapshot has no Git metadata.
`build/diagnostics/godot-ssr-audit/source-manifest.json` records inspected file
hashes. Its SSR trace shader SHA256 is
`2cb54ee45a9c405365ec168dce49b2e2c419ecd93f1926ccd18ef4b830f615a1`.
Companion `assessment.md`, `scheduling.md`, `traversal.md` and
`reconstruction.md` retain verified file/line anchors.

Godot separates tracing from material evaluation, defaults to half-size
receivers, traces one mirror ray, and reconstructs rough reflection through
a filtered output mip chain and geometry-aware resolve. Its donor is previous
completed color with different transparent/history ownership. This supported
investigating shared tracing; copying its sampling or previous color policy
would change Crimson's estimator and source contract. This pass adopts the
separation and retains full-resolution current-frame rays. It makes no
cross-engine performance comparison.

- McGuire and Mara, [Efficient GPU Screen-Space Ray Tracing, JCGT 2014](https://jcgt.org/published/0003/04/04/), supplies perspective-correct traversal and reference code. Crimson retains its min/max hierarchy rather than adopting the complete DDA loop.
- Heitz, [Sampling the GGX Distribution of Visible Normals, JCGT 2018](https://jcgt.org/published/0007/04/01/), supplies the visible-normal sampling mathematics retained for rough reflections.
- AMD's [FidelityFX SSSR documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/) separates classification, intersection and reconstruction. This pass implements no compact ray lists, variance-guided rate or blue-noise schedule.
- AMD's [reflection denoiser documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/) describes reprojection and reconstruction for reduced sampling. No temporal denoiser was added; its CS6 requirements also prevent a direct drop-in replacement for the shared SM5/DX11 family.

Initial native traces, matched static reports and source comparisons remain
distinct evidence. A connected after-change official Tracy diagnostic must be
labeled frozen static work, checked for complete CPU/native-GPU zones and
retained with its manifest/logs. Moving-gameplay acceptance is a separate
comparison.
