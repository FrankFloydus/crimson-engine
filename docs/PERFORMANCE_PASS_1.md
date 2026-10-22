# First renderer performance pass: SSR receiver admission

Measured 2026-10-22 on the local Windows machine, NVIDIA GeForce RTX 4090, Direct3D 12. This pass starts from the reported observation that disabling SSR recovers about 19 ms in interactive Play. It investigates that feature directly rather than changing the scene's quality settings.

## Finding and change

SSR previously admitted every Lit, non-additive receiver to its geometry redraws. The reflection fragment shader rejected receivers above the authored roughness cutoff only after vertex and, on DX12, hull/domain processing had already run. An opaque receiver incurred both the raw reflection trace draw and the final reflection composition draw.

The frozen scene contains a displaced sphere with constant roughness 1, above its SSR cutoff .6, and tessellation factor 37. Its two SSR redraws could not contribute a reflection, but repeated its expensive displaced geometry. Other constant rough receivers also incurred unnecessary draws.

The renderer now carries an optional exact constant-roughness proof in the generated CPU material description and its immutable prepared packet. It admits receiver work conservatively before drawing:

- Unlinked Output roughness and direct Scalar/Color parameter links can prove a constant value.
- Arithmetic, texture samples, time and other runtime expressions remain unknown and retain receiver work.
- A bound fallback roughness texture remains unknown because it can reduce the authored scalar factor.
- A small conservative margin retains work near the cutoff. The shader's specular AA can raise effective roughness, so it cannot turn a rejected rough constant into an eligible receiver.

Visible surfaces, displacement, shadow casters and SSR donors keep their ordinary rendering. The tracing equations, ray/step budgets, reconstruction, material ABI and source generator are unchanged. The optimization removes provably neutral receiver draws rather than reducing reflection quality. Current metadata is published with value-only material edits even when their shader programs reuse an existing topology.

## Measurement scope

The explicit `forward_plus_room_benchmark --ssr-performance-project` mode loads a disposable copy through the production resource and prepared-frame path. The primary workload is one 1280×720 Game image, using the saved Game camera, HDRI, material definitions, SMAA and 1024-pixel point-shadow preference. Other authored effects stay enabled. The saved scene had SSR switched off; the frozen diagnostic copy explicitly restores SSR on, and the `--ssr off` counterfactual changes only that flag.

Each trial uses at least 120 warmup frames overall and requires eight consecutive fully ready observations before beginning 240 timed CPU frames. At least 120 distinct completed GPU queries must belong to that timed submission window. Sixteen untimed tail frames expose delayed results. The diagnostic rejects timed asset loads, compiler activity, resource failures and loss of preparation readiness.

GPU values are timestamp elapsed intervals, including possible feeding/queue gaps; they are not measurements of active hardware execution alone. Wall time includes normal primary Present and queue backpressure. Separate runs enable bgfx view profiling for stage attribution; those timings are not mixed into the unprofiled cadence comparison, and independently published view queries are not assumed to belong to the accompanying whole-frame query.

This is a stationary render workload with authored pose/material time zero. It excludes physics, gameplay simulation, advancing animation, particle simulation, editor UI composition and visible desktop presentation. It does not measure interactive Play FPS or moving-camera shadow updates. The input-triangle counter also excludes hardware-generated tessellation triangles.

Initial cold-cache preparation and exploratory runs while the editor was open are excluded. Formal comparisons run with the editor closed and without a simultaneous build or another diagnostic GPU workload.

## Results

The completed local comparison and raw samples are under `build/diagnostics/performance-pass1/`. [comparison.json](../build/diagnostics/performance-pass1/comparison.json) records frozen durable-input and executable SHA-256 hashes, distributions, per-trial values and the selected formal runs. Each table entry below is the median of three per-process values, with separate profiled runs for stage attribution.

| Workload | Before GPU p50 / p95 | After GPU p50 / p95 | Before wall p50 / p95 | After wall p50 / p95 | Draws before → after |
| --- | --- | --- | --- | --- | --- |
| SSR on | 11.090 / 12.816 ms | 9.259 / 10.284 ms | 11.495 / 13.562 ms | 9.514 / 10.664 ms | 75 → 65 |
| SSR off | 1.270 / 3.345 ms | 1.270 / 2.114 ms | 3.006 / 3.934 ms | 2.818 / 3.289 ms | 44 → 44 |

The first fix saves **1.831 ms / 16.51%** at the SSR-on GPU median and **1.981 ms / 17.23%** at the wall-frame median. The three SSR-on GPU medians are 11.090/11.269/11.025 ms before and 9.266/9.064/9.259 ms after. SSR-off GPU medians remain 1.264–1.271 ms before and 1.270–1.271 ms after. All selected timed windows have zero resource loads, compiler processes and resource failures, and empty error statuses. SSR-on retains 82 compute dispatches; SSR-off retains 38. No authored reflection or other quality settings were lowered for the before/after comparison.

| Profiled work | Before p50 | After p50 | Old-renderer trace-rejection control p50 |
| --- | --- | --- | --- |
| View 372: hierarchy preparation + opaque raw tracing | 7.090 ms | 5.501 ms | .297 ms |
| View 373: all three reconstruction stages | .092 ms | .092 ms | .049 ms |
| View 374: opaque reflection composition | .174 ms | .048 ms | .159 ms |
| View 384: sorted transparency, including direct SSR | 1.957 ms | 2.152 ms | .039 ms |

The diagnostic control retains the **original renderer, 75 draws, 82 compute dispatches, hierarchy generation and tessellation**, but sets the disposable scene's Max Roughness to .00001, below the BRDF's .045 minimum. Every reflection shader therefore exits before tracing. Its whole-frame GPU median is 1.903 ms. This control changes reflection appearance deliberately to isolate cost; it is not the delivered optimization or a proposed scene setting.

That control separates the original large bottleneck from the redundant-draw finding: hierarchy/geometry alone do not explain the 7.090 ms raw-stage or 1.957 ms transparent-stage cost. Reflection tracing/fragment work dominates. The factor-37 sphere is a proven unnecessary-work case, rather than the sole cause of the reported slowdown. After the fix, raw tracing/preparation and direct transparent SSR remain the largest costs; the .092 ms reconstruction stage is a low-priority target. The transparency increase in the one after-profile run is not enough to establish a stable regression, and stage percentile differences must not be summed into a precise hardware budget.

The next measured experiments should target ray traversal arithmetic and specialization of opaque raw tracing versus transparent filtering. Existing DXBC has 58–59 declared temporary vectors and eleven loop sites, with both paths retained in one runtime-mode program. These are compiler-level observations, not hardware register/occupancy measurements. FXC already hoists many receiver invariants; source-level hoisting alone is not an established improvement. Reusing unchanged-depth work during Hi-Z descent or specializing programs requires its own paired timing and geometric image checks before publication.

## Source comparisons

The local reference checkouts were read without modification:

- Godot: `C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl`, receiver roughness rejection; `servers/rendering/renderer_rd/effects/ss_effects.cpp`, compute dispatch. Godot reads an existing normal/roughness buffer and rejects ineligible receivers before tracing, avoiding another tessellation of those surfaces.
- Unreal: `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Renderer/Private/ScreenSpaceRayTracing.cpp`, SSR stencil classification and indirect tile paths. These classify receiver work from scene textures before expensive tracing.

Crimson retains its material-local receiver shading and uses exact CPU material proofs as the smallest compatible first step. Half-resolution tracing, temporal reuse, a new G-buffer and a threading rewrite are separate design decisions and were not needed for this change.

The audit also found later profiling candidates: hidden Scene receiver preparation, repeated CPU extraction/material preparation, rough-surface reconstruction bandwidth and transparent reflection footprint validation. They remain secondary until their cost is measured; this pass does not claim speedups from them.

## Reproduction

Build Release first. Use the retained frozen diagnostic input for the same local comparison, with SSR enabled in its scene:

```powershell
$root = (Get-Location).Path
& '.\build\Release\bin\forward_plus_room_benchmark.exe' --ssr-performance-project `
  "$root/build/diagnostics/performance-pass1/warmed-project" `
  "$root/build/Release/bin/shader-tools/shaderc.exe" `
  "$root/build/Release/bin/shader-tools/shaders" `
  "$root/build/Release/bin/shader-tools/include" `
  'build/diagnostics/performance-pass1/repeat-game-on' `
  --backend dx12 --warmup 120 --samples 120
```

Use a fresh output directory and append `--ssr off` for the paired counterfactual. `--profile-views` enables a separate attribution run; `--views both` requests a full Scene image as well as Game at the selected dimensions. Each process copies its input before opening stores, and never changes the source project or drives desktop input. A source/include/compiler path change can invalidate the shader cache and make initial preparation substantially longer than a warmed run.

## Correctness

The Release integration build passed. Material preparation retained 12/12 topologies and 64 cached stages with zero compiler processes or failures. Sixteen focused checks passed: nine CPU CTest checks, the material-graph CPU executable, and six DX12/DX11 GPU checks covering SSR, presentation effects and SMAA.

Focused material tests exercise exact and unknown roughness classification, scalar/color conversion, retained immutable packets and value-only edits with reused programs. SSR GPU tests compare the forced-unknown path with the proven-ineligible path, requiring byte-identical displayed RGBA and the expected removal of two opaque or one translucent reflection draws. DX12 includes tessellated geometry; DX11 retains its ordinary geometry path. Dither checks cover stable ordered patterns, Posterization palette distribution, pixel-block preservation, independent view settings, optional persistence/history, zero-strength identity and diagnostics bypass.
