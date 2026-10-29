# Measured DX11 / DX12 backend comparison

Measured 2026-10-17 on Windows 11 Pro build 26200, AMD Ryzen 9 7900X (12 cores / 24 threads), NVIDIA GeForce RTX 4090, NVIDIA driver 610.74 (DXGI version 32.0.16.1074). This report covers the first DX12 backend slice, with DX12 as the application default and DX11 available in **Edit → Preferences → Rendering → Renderer (restart required)**. Renderer threading is unchanged.

The measured result is workload-dependent. DX12 increased mean frame wall time by 16.0% in the draw-heavy Game case and 4.8% with Scene and Game together. Effects-heavy mean wall times decreased by 2.5% and 3.4%, but run ranges overlap and DX12 p95 tails are longer. These local results do not establish a general DX12 speed advantage. Use frame wall time and measured renderer cadence to assess this comparison; the GPU timestamp intervals below are not an active-GPU utilization or shader-throughput benchmark.

## Workloads and controls

Both APIs used the same final Release executable, shader family, geometry, material factors, light settings, motion and frame counts. The renderer used production prepared-frame submission with VSync disabled. All views are 1920×1080; the two-view cases render a full Scene and a full Game view, each at that size.

| Fixture | Models per view | Local point lights | Shadow sources | Other work |
| --- | ---: | ---: | --- | --- |
| Draw-heavy | 3,073 cubes / floor | 64 | One directional light | Dense draw submission; GI, SSAO and Glow off |
| Effects-heavy | 433 spheres / floor | 256 | Two moving shadowed point lights and one directional light | Medium SSGI, one bounce; Medium SSAO; Glow; 2,048 additive particles per view |

A small camera orbit, sixteen moving models and two moving lights repeat deterministically. The fixtures use shared built-in geometry, native PBR material factors and no disk textures or authored assets. They are stress fixtures rather than representative authored levels. They exclude editor widgets, simulation, asset IO, animated rigs, texture streaming and visible desktop composition. Scene directional cascades and the Game fitted shadow map retain their normal different policies. Feature-status/resource checks reject silently bypassed GI, AO, Glow, shadows or particles, and nonblank captures verify rendered output.

The six fresh processes ran in this order: DX11, DX12, DX12, DX11, DX11, DX12. Build preparation, shader compilation and other GPU tests finished before timing. The exact reproduction command per process was:

```powershell
& '.\build\Release\bin\renderer_backend_benchmark.exe' --backend dx11 --output 'build/backend-comparison-final/final-trial1-dx11' --warmup 120 --samples 240
```

Change the backend and trial-directory suffix for the six runs. The utility defaults to a longer 720-frame window when `--samples` is omitted; this report uses the explicit 480-frame window above.

## Corrected measurements

Measured on **NVIDIA GeForce RTX 4090**, driver **32.0.16.1074**, at **1920Ã—1080 per view**.

Three fresh processes per API used alternating order. Every phase used 120 warmup frames, 480 measured CPU frames covering two motion cycles, and at least 240 actual distinct GPU timing queries. The table reports the median of the three run medians; p95 columns separately report the median of the three run p95 values. Positive CPU wall changes mean slower observed renderer-frame cadence. GPU columns report timestamp elapsed and cannot establish active GPU execution or shader speedup.

Optional per-view GPU profiling and extra native Flush calls are disabled on both APIs, preserving bgfx's normal primary Present cadence. These final runs follow the pinned bgfx DX12 timestamp-fence repair. Earlier GPU measurements in `build/backend-comparison` are superseded: the old timer could read unresolved timestamp slots under new frame IDs. Earlier CPU measurements are retained as diagnostics and are not mixed into these results.

| Fixture | Views | DX11 CPU wall p50/p95 ms | DX12 CPU wall p50/p95 ms | CPU wall p50 change | DX11 GPU elapsed p50/p95 ms | DX12 GPU elapsed p50/p95 ms | GPU elapsed p50 change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| draw-heavy | 1 | 16.952 / 19.530 | 18.972 / 26.005 | +11.9% | 17.020 / 19.986 | 7.339 / 11.334 | -56.9% |
| draw-heavy | 2 | 33.900 / 39.869 | 35.639 / 43.294 | +5.1% | 33.968 / 39.870 | 14.512 / 18.810 | -57.3% |
| effects-heavy | 1 | 6.937 / 7.611 | 6.354 / 8.955 | -8.4% | 6.857 / 7.563 | 7.212 / 8.726 | +5.2% |
| effects-heavy | 2 | 14.302 / 15.958 | 13.394 / 16.881 | -6.4% | 14.267 / 15.493 | 13.638 / 16.615 | -4.4% |

Average wall cost and measured cadence account for slow-frame tails that a lower p50 can hide. These are medians of the three per-run arithmetic means and, separately, medians of actual measurement-window throughput; throughput is not computed from p50.

| Fixture | Views | DX11 mean CPU wall ms | DX12 mean CPU wall ms | Mean wall change | DX11 measured throughput fps | DX12 measured throughput fps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| draw-heavy | 1 | 17.357 | 20.126 | +16.0% | 57.61 | 49.68 |
| draw-heavy | 2 | 34.667 | 36.324 | +4.8% | 28.84 | 27.53 |
| effects-heavy | 1 | 6.962 | 6.788 | -2.5% | 143.60 | 147.30 |
| effects-heavy | 2 | 14.297 | 13.805 | -3.4% | 69.93 | 72.43 |

Run-to-run median ranges and process-local residency:

| Fixture / views | API | CPU wall p50 range ms | GPU elapsed p50 range ms | DXGI local residency median MiB | Observed GPU queries / 480, trials 1â€“3 |
| --- | --- | ---: | ---: | ---: | --- |
| draw-heavy / 1 | DX11 | 16.102â€“17.064 | 16.097â€“17.044 | 185.05 | 480, 480, 480 |
| draw-heavy / 1 | DX12 | 18.319â€“20.602 | 7.272â€“8.667 | 230.77 | 480, 480, 480 |
| draw-heavy / 2 | DX11 | 33.061â€“33.985 | 33.137â€“34.118 | 253.68 | 480, 480, 480 |
| draw-heavy / 2 | DX12 | 34.735â€“36.408 | 14.082â€“15.193 | 300.08 | 480, 480, 480 |
| effects-heavy / 1 | DX11 | 6.795â€“7.644 | 6.748â€“7.642 | 436.25 | 480, 480, 480 |
| effects-heavy / 1 | DX12 | 6.150â€“7.126 | 6.872â€“7.419 | 493.89 | 447, 422, 432 |
| effects-heavy / 2 | DX11 | 13.437â€“14.650 | 13.360â€“14.590 | 714.04 | 480, 480, 480 |
| effects-heavy / 2 | DX12 | 13.275â€“13.535 | 13.607â€“14.577 | 774.14 | 435, 425, 419 |

Fixed-pose Game capture comparison and per-frame workload parity:

| Fixture / views | Draw/compute/triangle/shadow counts match all trials | Median mean absolute RGB error | Maximum RGB error | Largest fraction of pixels with RGB error > 2 |
| --- | --- | ---: | ---: | ---: |
| draw-heavy / 1 | Yes | 0.000000 | 1 | 0.00000% |
| draw-heavy / 2 | Yes | 0.000000 | 1 | 0.00000% |
| effects-heavy / 1 | Yes | 0.000279 | 2 | 0.00000% |
| effects-heavy / 2 | Yes | 0.000250 | 1 | 0.00000% |

CPU wall timing includes fixture preparation, rendering submission and hidden primary swapchain Present/queue backpressure, with VSync off on both APIs. It excludes editor UI, gameplay simulation, asset IO and visible desktop composition latency. GPU elapsed brackets can include CPU feeding and queue gaps. DX11's normal Present/driver submission cadence and DX12's command-list kicks can bracket those gaps differently, so draw-heavy GPU elapsed ratios must not be described as hardware execution speedups. GPU statistics publish only the latest completed query; unavailable intermediate frame IDs and actual coverage remain in the JSON. These synthetic stress fixtures do not predict gameplay FPS.

Memory figures use DXGI process-local residency. Renderer owner counters and their run ranges are in the JSON; SSGI/SSAO counters are subsets of the environment/screen-lighting counter and must not be added again. Game-only shadow allocation still reserves the renderer's two camera blocks.

Source reports, raw CSVs and PNGs are under `C:\Users\Drako\Desktop\_CRIMSON_ENGINE\crimson-engine\build\backend-comparison-final` in `final-trial{1,2,3}-{dx11,dx12}`. The JSON includes input paths and SHA-256 hashes for provenance. No earlier profiled or unrepaired GPU attempt contributes to these results.


## Local and system-memory residency

These are medians of three process-local DXGI residency samples at each phase end. Local residency is GPU-local memory; nonlocal residency is system memory visible to the GPU. Neither column is a claim about total application CPU memory. The process executes all four phases in the same order; these are retained steady-phase allocations, not four isolated cold-start peaks.

| Fixture / views | DX11 local MiB | DX12 local MiB | DX11 nonlocal MiB | DX12 nonlocal MiB |
| --- | ---: | ---: | ---: | ---: |
| Draw-heavy / Game | 185.05 | 230.77 | 28.86 | 376.29 |
| Draw-heavy / Scene + Game | 253.68 | 300.08 | 52.91 | 379.88 |
| Effects-heavy / Game | 436.25 | 493.89 | 55.61 | 406.52 |
| Effects-heavy / Scene + Game | 714.04 | 774.14 | 86.33 | 410.90 |

DX12 used more local and nonlocal residency in these fixtures. The full JSON separates renderer owner allocations from driver residency and bgfx estimates. Driver memory estimates are not combined across APIs because their scopes differ. SSGI and SSAO owner counters are already included in the environment/screen-lighting owner counter and must not be added again.

## Correctness, lifetime and provenance

The final focused CTest run passed **30 of 30 checks**, covering startup options, preference persistence/default compatibility, scene/history, render-pass planning, shader preflight/reload, clustered lighting, shadows, material output, receiver precision, SSGI, SSAO and native resource lifetime. The strict DX12 native-platform test enabled GPU validation and passed its pixel, collapse, resize, close/reopen, accounting and shutdown checks without error or corruption messages. The final executable also passed 12-frame hidden application startup/shutdown with the default DX12 selection and the DX11 command-line override, using disposable project roots.

The [project-owned bgfx overlay](../third_party/crimson-renderer/cmake/vcpkg-ports/bgfx/README.md) keeps the existing pinned version and manifest baseline. It fixes small-mip upload descriptors, staging written ranges and GPU timestamp readiness. The latter waits for the command list containing the query resolve before reading its mapped results. The earlier reported GPU percentages are superseded by this corrected run set. These fixes do not introduce renderer multithreading.

The twelve fixed-pose comparisons above read the Game image, including the Game image in two-view phases. RGB differences are display-encoded 8-bit code values; this is not a proof of HDR bit identity or interactive visual acceptance. All paired frame-by-frame draw, compute, triangle and shadow counts matched. Authored scene/particle data, catalog/session files and `crimson_ui.ini` matched their pre-build preservation hashes. Interactive acceptance remains with the user.

Evidence retained locally:

- [Final analysis JSON](../build/backend-comparison-final/final-analysis.json): per-run distributions, raw-input paths and SHA-256 hashes, query availability, memory and capture comparisons.
- Raw `report.json`, `cpu-frames.csv`, `gpu-frames.csv` and PNG files under `build/backend-comparison-final/final-trial{1,2,3}-{dx11,dx12}`.
- [Corrected Release integration build](../build/dx12-timer-corrected-build.log) and [final benchmark-only incremental build](../build/dx12-benchmark-final-build.log).
- [Final 30-check CTest run](../build/dx12-timer-corrected-checks.log).
- [Binary hashes and timing configuration](../build/dx12-final-executables.json).
- [Authored input preservation hashes](../build/dx12-authored-inputs-final.json).

Renderer SHA-256: `F8991F56D7365CC03F0FBE0F78EBBE3CF9A9236B5CD6FC89ED951DE04C7D6F21`. Benchmark SHA-256: `AE41BD5B527D4FE13C82F537623C92B9C9B8B3B80D986282F170AFE1212279E8`. Both APIs used this same benchmark binary. Results apply to this local machine and workload; they do not establish a lower-end GPU budget or gameplay frame rate.
