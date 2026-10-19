# Clustered Forward+ implementation checkpoint

Recorded on 15 October 2026. This document preserves the implementation's actual correctness and performance evidence. It is a checkpoint for this renderer slice; it does not declare the engine ready for an unrestricted large ARPG.

## How to read this checkpoint

The measurements, test counts and binary identities below describe the 15 October Forward+ migration. They are retained evidence, not a claim that the latest renderer has been benchmarked again. Character rigs, lighting receiver/caster scopes, Ambient Darkness and Environment Ambient Light subsequently landed. For the current implementation, controls, shader ABI and remaining limits, read [Renderer and character lighting](RENDERER_AND_CHARACTER_LIGHTING.md) and [the authoritative architecture](ENGINE_ARCHITECTURE.md).

## Implemented slice and remaining limits

The production renderer uses GPU cluster culling and fragment bitset iteration for local point lights. Each camera/render-target owner has independent data. The normal grid uses 32-pixel tiles and 24 logarithmic depth slices; very large viewports increase tile size to remain inside the cluster capacity. Direct-light storage accepts 4,096 point-light records. Transparent meshes and lit particles use conservative camera clusters without opaque-depth rejection.

At this checkpoint, point shadows had a separate budget of sixteen eligible lights and directional lighting was limited to four lights. Receiver lighting channels, independent caster masks, the player light rig and ambient attenuation were outside the Forward+ slice; they subsequently landed. Adaptive shadow allocation and world streaming remain future work. The current sixteen resident point maps additionally have a four-complete-map/twenty-four-face update budget shared by Scene and Game.

The BRDF lookup, point-light records, and cluster masks share the existing BRDF texture binding. Existing graph material texture capacity is preserved. A static camera and unchanged light geometry reuse cluster masks; changes to camera, light geometry, viewport, or the compute program invalidate them. Color/intensity-only changes update lighting data without requiring geometric culling again.

Implementation entry points:

- [Renderer integration](../third_party/crimson-renderer/src/forward/ForwardPipeline.cpp), [GPU owner](../third_party/crimson-renderer/src/forward/ClusteredLightingGpu.cpp), [shared fragment access](../third_party/crimson-renderer/src/shaders/clustered_lighting.sh), and [compute culling](../third_party/crimson-renderer/src/shaders/cs_cluster_lights.sc).
- [CPU geometry oracle](../third_party/crimson-renderer/src/forward/ClusteredLighting.cpp) and [independent shadow selection](../third_party/crimson-renderer/include/crimson/renderer/forward/ShadowLightSelection.h).
- [Hidden render checks and synthetic benchmarks](../tests/forward_plus_render_tests.cpp), [frozen-room benchmark](../tests/forward_plus_room_benchmark.cpp), and [direct GPU-owner checks](../tests/clustered_lighting_gpu_tests.cpp).

## Build and correctness evidence

The first incremental Release integration build passed. Fourteen focused CPU checks passed: material graph, scene, directional shadow math, Quader scenes, environment presentation, shader preflight, point shadow math, render scene, scene extraction, clustered lighting, Quader bridge, shader compiler, generated material shaders, and material publication.

Four hidden GPU checks also passed: `native_resource_core` (9.37 s), `point_shadow_render_core` (7.02 s), `material_mapping_render_core` (9.24 s), and `forward_plus_render_core` (3.55 s). That is eighteen successful focused checks for the first integration. Both subsequent synthetic benchmark invocations reran and passed the Forward+ image checks before timing.

The image oracle evaluates every admitted light using the same new light-data transport, with cluster filtering disabled. It therefore checks conservative culling independently from the production cluster lists. It is not the old sixteen-light renderer. Independent nonzero-light and shadow-darkening assertions prevent two uniformly incorrect black images from passing.

Coverage includes zero lights; 1, 16, 17, 31, 32, 33, 64, 256, and 1,024 overlapping lights; source indices beyond sixteen; the final record at the 4,096-light capacity; full 32-bit mask words transported through float texture storage; near-plane and offscreen light volumes; different clip ranges and camera poses; fallback and generated mesh shaders across all blend modes; transparent lit particles without an opaque surface; simultaneous Scene/Game views with distinct colored lights; thumbnails; BRDF rows unaffected by remote light data; and a shadow-casting light at source index 63 after enabled non-shadowed lights.

The retained current pre-migration material-mapping executable also passed before rebuilding, in 9.22 s, against frozen shaders and the copied smoke texture. This checks the pre-existing material/fog behavior independently of the new binaries.

The final incremental Release repair build passed. `streaming_core` and all four existing hidden GPU suites passed again. The new `clustered_lighting_gpu_core` passed in 1.02 s after its completed-frame counter observation was drained correctly. There are **twenty unique focused checks passed** across the integration and repairs. The additional checks cover non-power-of-two BRDF row packing, mask transport, static reuse, failed-update recovery, and immutable mip-cache winner publication while a reader remains open. The measured synthetic shader files and normal LUT paths are unchanged; the complete 48-phase matrix was measured with the pre-repair candidate identified below. The final real-room replay used the repaired binary and completed every readiness and timing phase.

## Hardware and measurement limits

Adapter: NVIDIA GeForce RTX 4090, vendor `0x10DE`, device `0x2684`, driver 610.74. Before the synthetic run, the adapter reported P0, 2,565 MHz graphics / 10,501 MHz memory, 4% utilization, and 13,059 MiB used. After the synthetic run it had returned to P8, 210 / 405 MHz, 0% utilization, and 13,037 MiB used. The adapter clocks and external desktop GPU load were not locked or controlled. These snapshots do not measure per-phase clocks or the engine's exclusive VRAM use.

All rendering used hidden D3D11 windows, VSync disabled, and an explicit borrowed immediate-context `Flush` after submitting a frame. The flush queues complete timestamp-query pairs; it is diagnostic code, not a production renderer wait. No editor controls or user applications were automated.

The synthetic fixture renders one lit plane at 960 × 540, with shadows disabled. The dual-view case renders the plane in both independent Scene and Game targets. Distributed lights use a deterministic spatial lattice with finite ranges. The overlapping fixture puts all lights at one local position and normalizes their total intensity. These are renderer stress cases, not production dungeon art or full-game frames.

Each phase warms at least 300 frames and measures at least 1,000 frames **and** five seconds. The static run recorded 423,713 measured frames over 24 phases; the moving run recorded 292,440 over 24 phases. Reference is followed by clustered for each pair. This is one matrix per motion mode, not three randomized repetitions.

Raw rows preserve submitted-frame and GPU-query-frame IDs. GPU summaries accept only unique, nonzero queries belonging to the measured phase. The recorded main-thread submission duration includes `bgfx::frame` and any queue backpressure; it must not be labeled pure CPU computation time. Per-phase p50/p95/p99/max remain in the raw summary CSVs. Five-second phase summaries on this desktop do not establish gameplay hitch guarantees.

`lighting_bytes` is the **last rendered camera owner's** allocation, including its retained allocation state. It is not the total for both views. `Renderer::environmentResidentBytes()` separately sums current renderer contexts. Never multiply or sum this diagnostic together with driver usage and label it total VRAM.

## Static camera and light geometry

The following compares unculled reference lighting against clustered lighting using the same light records and material shader. A ratio above 1 means clustered lighting was faster. Static cluster construction is outside these steady samples because unchanged inputs reuse their masks.

| Arrangement | Views | Lights | Unculled reference GPU ms | Clustered GPU ms | Reference / clustered |
|---|---|---:|---:|---:|---:|
| Distributed | Game | 64 | 0.1024 | 0.0298 | 3.43x |
| Distributed | Game | 256 | 0.3598 | 0.0836 | 4.31x |
| Distributed | Game | 1024 | 1.3705 | 0.2555 | 5.36x |
| All overlap | Game | 64 | 0.1965 | 0.1982 | 0.99x |
| All overlap | Game | 256 | 0.7215 | 0.7324 | 0.99x |
| All overlap | Game | 1024 | 2.7618 | 2.8612 | 0.97x |
| Distributed | Scene + Game | 64 | 0.2019 | 0.0568 | 3.56x |
| Distributed | Scene + Game | 256 | 0.7252 | 0.1696 | 4.28x |
| Distributed | Scene + Game | 1024 | 2.7774 | 0.5196 | 5.35x |
| All overlap | Scene + Game | 64 | 0.3980 | 0.4025 | 0.99x |
| All overlap | Scene + Game | 256 | 1.4849 | 1.5004 | 0.99x |
| All overlap | Scene + Game | 1024 | 5.6678 | 5.8412 | 0.97x |

With 1,024 distributed lights, clustered GPU time was 0.2555 ms for Game and 0.5196 ms for Scene + Game, versus 1.3705 and 2.7774 ms for the reference. With all lights overlapping the surface, clustering provided no general reduction in lighting work: the measured 1,024-light static case was 3.6% slower for Game and 3.1% slower for both views. Preserve this tradeoff when interpreting the distributed-light gains.

## Moving cameras, including cluster construction

Every measured frame changes the camera position and view direction. The trajectory has a deterministic 240-frame period, resets for each phase, and is applied to both owners in dual-view cases. Thus these samples include repeated point-data upload and cluster compute work rather than static mask reuse.

| Arrangement | Views | Lights | Unculled reference GPU ms | Clustered GPU ms | Reference / clustered |
|---|---|---:|---:|---:|---:|
| Distributed | Game | 64 | 0.1070 | 0.1144 | 0.94x |
| Distributed | Game | 256 | 0.2917 | 0.1721 | 1.70x |
| Distributed | Game | 1024 | 1.1374 | 0.3331 | 3.41x |
| All overlap | Game | 64 | 0.1631 | 0.2626 | 0.62x |
| All overlap | Game | 256 | 0.5755 | 0.5313 | 1.08x |
| All overlap | Game | 1024 | 2.4172 | 1.8206 | 1.33x |
| Distributed | Scene + Game | 64 | 0.1665 | 0.1513 | 1.10x |
| Distributed | Scene + Game | 256 | 0.5798 | 0.2690 | 2.15x |
| Distributed | Scene + Game | 1024 | 2.3051 | 0.5117 | 4.51x |
| All overlap | Scene + Game | 64 | 0.3269 | 0.3535 | 0.92x |
| All overlap | Scene + Game | 256 | 1.1781 | 0.9604 | 1.23x |
| All overlap | Scene + Game | 1024 | 5.1529 | 3.4656 | 1.49x |

The 1,024-light distributed case improved from 1.1374 to 0.3331 ms for Game and 2.3051 to 0.5117 ms for both views. The small 64-light Game case shows the fixed culling/upload cost: distributed GPU time rose from 0.1070 to 0.1144 ms, while all-overlap time rose from 0.1631 to 0.2626 ms. The all-overlap dual-view 64-light case rose from 0.3269 to 0.3535 ms.

For moving 64-light overlapping Game frames, CPU setup averaged about 0.0085 ms reference and 0.0079 ms clustered. The extra 0.0994 ms is therefore not explained by this CPU setup counter; repeated GPU culling and submission work add a fixed cost when little or no per-pixel lighting can be rejected. For moving 1,024-light distributed Game frames, CPU setup averaged about 0.0926 / 0.0909 ms reference / clustered. Setup is one owner, not a combined dual-view CPU measure.

Some larger moving overlap pairs measured faster with clustering. Do not generalize that result into a promise that overlapping lights become cheaper: shader branch/cache behavior and uncontrolled desktop clocks/load can affect these single-run numbers. Every light that reaches a surface still contributes shading work.

## Frozen real-room replay

The managed saved scene is `assets/scenes/9d9791ec48beb7022d3a7e925edbb4f1.crimson-scene`, SHA-256 `B8A447E51FF73338B240CABEAF70A20096883D550C925FF3D619E6DDFB3EAE63`, inside the frozen fixture. `world_origin` is `saved_main`; the live Quader project is not reimported to define the workload.

The world contains 86 models, fourteen point lights and one directional light, with two 909 × 684 views. Static visibility is Scene 32 / Game 49 models. The static phase submits 101 draws, thirteen point-cache hits, two directional-camera cache hits, and no shadow writes. Moving the player visual subtree and follow camera produces 888 shadow draws, 36 point faces and eight cascades. Shadow allocation is 507,256,320 bytes; material textures/programs are 41,056,470 bytes and model buffers 2,137,830 bytes in the shadowed phases.

The retained legacy room executable was built at 09:40 on 15 October, before the later Receive Fog changes. Its exact binary, frozen shaders, dependency payload and scene hashes are preserved. Git HEAD alone does not reconstruct that dirty working tree. The current pre-migration material GPU executable was retained and independently checked, but there is no claim that the older room executable exactly matches every subsequent non-Forward+ code change.

| Frozen-room phase | Retained old A wall / GPU ms | Retained old B wall / GPU ms | Final Forward+ wall / GPU ms |
|---|---:|---:|---:|
| No shadows | 2.3012 / 0.9900 | 2.1990 / 1.1441 | 2.1773 / 0.9959 |
| Cached static shadows | 6.0612 / 2.3634 | 5.8488 / 1.6865 | 5.3944 / 1.6406 |
| Moving player subtree + camera | 7.4829 / 1.5854 | 7.0904 / 1.4477 | 6.9156 / 2.0238 |

The candidate completed with the same model visibility, draw counts, shadow writes and 507,256,320-byte shadow allocation. Its shadowed texture/program counter is 41,101,494 bytes, 45,024 bytes above the retained binary, which includes generated shader-program changes. This counter excludes the separately owned Forward+ data atlases.

The final moving-room GPU mean, **2.0238 ms**, is higher than the old A/B range of **1.4477–1.5854 ms**, while measured wall time is 6.9156 ms versus 7.0904–7.4829 ms. Do not describe this as a room GPU improvement. A last-frame compute-view sample was about 0.0225 ms; point-shadow, post-process and surface-view means also varied, so the whole GPU difference cannot be assigned solely to cluster culling from these thirty-sample asynchronous query summaries. A controlled in-editor profile is needed for a precise small-scene GPU cost.

Both old runs used the same retained binary and fixture. Their static-shadow GPU means range from 1.6865 to 2.3634 ms despite identical work, illustrating why this short replay must not support a precise GPU speedup claim. Each old phase uses only thirty measured samples, with at least thirty warm-up frames and readiness required before measuring. The room harness excludes editor widgets/browser work, the live Quader watcher, actual Play physics/AI and a representative animated crowd.

Two pre-repair candidate attempts are excluded from performance conclusions. The first failed readiness rather than completing a timed workload: publication of a texture mip-cache file hit Windows sharing error 32 while an immutable cache winner was open for reading. A second clean-derived-cache attempt was stopped after the same persistent non-ready behavior. Neither failed run is included as a successful or faster candidate. The failure led to using the existing immutable byte-equal cache-winner helper for texture mip publication; authored asset writes are separate. The final run using the repaired cache publisher reached complete/refined resources in every phase and passed its static-shadow reuse assertion.

## Reproduction and artifact identity

Run from the repository root in PowerShell. Close the running renderer before rebuilding, as required by the repository instructions. Tests and benchmarks use hidden windows and disposable copies rather than modifying the authored project.

```powershell
.\Build.cmd Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

$shaderCompiler = Join-Path (Get-Location) 'build/vcpkg_installed/x64-windows/tools/bgfx/shaderc.exe'
$shaderSources = Join-Path (Get-Location) 'third_party/crimson-renderer/src/shaders'
$shaderIncludes = Join-Path (Get-Location) 'build/vcpkg_installed/x64-windows/include/bgfx'
$results = Join-Path (Get-Location) 'build/verification/forward-plus-results'

& './build/Release/bin/forward_plus_render_tests.exe' $shaderCompiler $shaderSources $shaderIncludes
& './build/Release/bin/forward_plus_render_tests.exe' $shaderCompiler $shaderSources $shaderIncludes '--benchmark' (Join-Path $results 'static')
& './build/Release/bin/forward_plus_render_tests.exe' $shaderCompiler $shaderSources $shaderIncludes '--benchmark-moving' (Join-Path $results 'moving')
```

The synthetic CLI first runs correctness checks, then benchmarks when requested. Use a fresh output directory when preserving earlier measurements. Do not run benchmarks concurrently with builds, other GPU tests or another benchmark.

For the local frozen-room fixture:

```powershell
$frozen = Join-Path (Get-Location) 'build/verification/forward-plus-baseline'
$roomArgs = @(
    (Join-Path $frozen 'engine-original86'),
    (Join-Path $frozen 'frozen-native/q-test/q-test.qproj'),
    '6aeb7097-5046-450e-a451-a8b4518aa095',
    $shaderCompiler, $shaderSources, $shaderIncludes,
    '--saved', '--output', (Join-Path $results 'room-candidate-final')
)
& './build/Release/bin/forward_plus_room_benchmark.exe' @roomArgs
```

Default `--warmup 30 --samples 30` maintains the retained probe comparison. For a longer future replay use `--warmup 300 --samples 2000`. `--cold` deliberately runs preparation-only phases and does not collect timed steady phases; a normal run from a fixture without derived caches is needed when both cold preparation and subsequent steady timing matter.

The retained old room executable uses the same first three fixture arguments, followed by **its frozen** `shader-tools/shaderc.exe`, `shaders`, and `bgfx-includes` directories, then `--saved`. The candidate must use the **current** compiler/source/include directories. Mixing new generated shader manifests with frozen old material includes is invalid.

Local ignored evidence:

| Location under `build/verification/` | Contents |
|---|---|
| `forward-plus-baseline/` | Old room binary, current pre-migration material test binary, DLLs, shader tools/includes/sources, frozen native and managed fixtures, SHA-256 inventory, baseline identity and first before logs. |
| `forward-plus-results/static/` | Static raw frame CSV, per-phase summary CSV and hardware/method settings. |
| `forward-plus-results/moving/` | Moving raw frame CSV, per-phase summary CSV and hardware/method settings. |
| `forward-plus-results/static.log`, `moving.log` | Correctness and timing stdout. |
| `forward-plus-results/room-old-bracket-a.log`, `room-old-bracket-b.log` | Retained old room replays, with extracted JSON alongside each log. |
| `forward-plus-results/room-candidate-final.log`, `room-candidate-final/` | Successful repaired candidate log, raw per-frame CSV and JSON summary. |
| `forward-plus-results/room-candidate.log` | Failed first candidate readiness replay; not a timing result. |
| `forward-plus-results/room-candidate-clean-cache.log` | Stopped clean-derived-cache retry; not a timing result. |
| `forward-plus-results/candidate-pre-repair-sha256.json` | Exact executable/source/shader identities used for the synthetic matrices, captured before the final repairs. Pre-repair candidate executables were overwritten by the subsequent build before a retained executable copy was taken. |
| `forward-plus-results/candidate-final-sha256.json` | Final real-room executable, current source, test, shader and build-input identities. |
| `forward-plus-results/analysis.json` | Parsed pair results, CPU setup means and old room summaries. |
| `forward-plus-results/cpu-tests.log`, `gpu-tests.log` | Initial fourteen CPU and four GPU check evidence. |
| `forward-plus-results/final-checks.log`, `final-owner-check.log` | Repair checks and the final passing standalone GPU-owner result. The first file preserves an initial completed-counter timing assertion failure; the second records its passing rerun after the test observed the completed frame. |

These ignored machine-local fixtures/results may be cleaned later. The maintained test sources and the measurements above remain the durable checkpoint. A future agent must freeze a new fixture and record new binary/source identities before changing the renderer again.

Final measured room executable SHA-256: `E69EB915AED51CFB2532D7A410D57C84069DA373033CBCA7ED5CACC77A8C0971`. Final `crimson-renderer.exe`: `0B121E74069ED15E09A4E4D45DE7CC76A0A0AF411BDA70287E03BFCF1867C9C3`. Full identities are in the final manifest.
