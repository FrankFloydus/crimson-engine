# Glow and Bloom implementation checkpoint

Measured 2026-10-20. Current controls and ownership are documented in [the lighting guide](RENDERER_AND_CHARACTER_LIGHTING.md#glow-and-bloom); this document records implementation evidence and limits.

## Delivered contract

- Glow selects visible Lit emission and Unlit radiance, with the same geometry, coverage, transparency order and fog attenuation as the scene. Bloom selects the original visible scene HDR, including the sun, reflections, direct highlights and emission. Neither processed effect feeds the other or GI.
- Separate artistic controls include intensity, spread and tint. Glow adds core/halo balance, saturation, threshold softness and Additive/Screen/Soft Light/Replace/Mix composition. Bloom adds highlight threshold, threshold softness, anamorphic stretch, Low/High quality and seven normalized contribution weights.
- Level 1 is tightest and Level 7 widest. All-zero Bloom levels and zero-effect paths schedule no effect dispatches; Mix 0 also bypasses Glow. Highest nonzero levels prune unused scales.
- A normalized thirteen-position filter reduces sources progressively at no more than 2:1 per axis. Spread/stretch set the filtered base extent. A normalized nine-position tent reconstructs the weighted scales with one-texel spacing. Up to five source prefilters plus seven weighted down levels and six up levels produce at most eighteen dispatches per effect.
- FP32 source/intermediate storage and explicit bilinear reads preserve supported high radiance without depending on adapter-specific FP32 linear sampling. Exposure/threshold extraction occurs once at the initial source stage; final style occurs once at the final up output.
- Material ABI 29 adds optional visible-emission publication. GI and emission share opaque MRT publication; transparent draws attach only HDR/emission/depth. Existing twelve material texture slots and the mesh vertex layout remain unchanged.
- Former four-field full-scene Glow data converts into Bloom in memory, with emission-only Glow disabled. Loading never rewrites scene files or changes their schema version. Current writers emit both effects and the emission source marker.

## Measurement method

Baseline renderer `29a4c93`; measured implementation `5821abd`, published with documentation as `c140594`. Release, Direct3D 12, NVIDIA GeForce RTX 4090. The static offscreen fixture has three shared-mesh instances (wall, floor, emissive source), with GI, AO, shadows and SMAA disabled. One view is Game; two views are independent Scene/Game owners. The primary hidden window is 320 by 240.

Each phase used 240 warmup frames and 600 distinct completed GPU frame IDs. Every included view timestamp must match the frame ID of the aggregate GPU sample. The comparison verifies all 48 old/new phases at 600 coherent samples. Candidate view targets are recreated before each phase to measure active-only allocation, then warmed before sampling. The old off/two-view phase retains the previous Game scratch; that is a baseline allocation artifact, not disabled GPU work.

The old Gaussian and new multiscale profiles are not pixel-equivalent. Settings, source fixture and resolution are matched to quantify the feature change, not to claim identical images. GPU times are elapsed timestamp intervals, including desktop scheduling; they are not GPU utilization or a prediction of editor/game FPS. P95 tails vary on this shared desktop. CPU submission wall time includes synchronization and GPU waits, so it does not isolate pure CPU execution.

## 1440p, one viewport

All times are milliseconds. Memory is active-only MiB for these post-processing owners, excluding ordinary viewport/lighting/scene allocations.

| Effect | Spread | Filter median | Filter P95 | Whole-frame median | Whole-frame P95 | MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Effects off (old) | 0 | — | — | 0.347 | 0.393 | 0.000 |
| Former scene-wide Glow (old) | 1 | 0.186 | 0.460 | 0.582 | 0.856 | 28.125 |
| Former scene-wide Glow (old) | 8 | 0.793 | 1.402 | 1.188 | 1.804 | 28.125 |
| Effects off | 1 | — | — | 0.339 | 0.482 | 0.000 |
| Bloom High | 1 | 0.227 | 0.459 | 0.611 | 0.841 | 37.495 |
| Bloom High | 8 | 0.161 | 0.386 | 0.552 | 0.829 | 19.044 |
| Glow | 1 | 0.227 | 0.453 | 0.612 | 0.844 | 93.745 |
| Glow | 8 | 0.169 | 0.413 | 0.549 | 0.842 | 75.294 |
| Glow + Bloom High | 1 | 0.429 | 0.701 | 0.834 | 1.406 | 131.240 |
| Glow + Bloom High | 8 | 0.314 | 0.561 | 0.703 | 0.971 | 94.338 |
| Bloom Low | 1 | 0.179 | 0.484 | 0.555 | 0.846 | 23.437 |
| Bloom Low | 8 | 0.167 | 0.427 | 0.514 | 0.878 | 18.825 |

New High Bloom at Spread 1 costs 0.041 ms more filtering and 0.030 ms more whole-frame elapsed time than the former effect. At Spread 8 it costs 0.632 ms less filtering and 0.636 ms less whole-frame elapsed time. Both effects at Spread 1 cost 0.429 ms filtering and 0.834 ms whole-frame elapsed time, compared with 0.339 ms for the new disabled fixture. These are additional artistic/source capabilities, not a universal speedup.

## Two viewports and 1080p

| Resolution | Views | Effect | Spread | Filter median | Whole-frame median | MiB |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 1 | Effects off (old) | 0 | — | 0.288 | 0.000 |
| 1920x1080 | 1 | Former scene-wide Glow (old) | 1 | 0.101 | 0.399 | 15.820 |
| 1920x1080 | 1 | Former scene-wide Glow (old) | 8 | 0.502 | 0.711 | 15.820 |
| 1920x1080 | 2 | Effects off (old) | 0 | — | 0.513 | 15.820 |
| 1920x1080 | 2 | Former scene-wide Glow (old) | 1 | 0.204 | 0.734 | 31.641 |
| 1920x1080 | 2 | Former scene-wide Glow (old) | 8 | 1.011 | 1.394 | 31.641 |
| 2560x1440 | 2 | Effects off (old) | 0 | — | 0.741 | 28.125 |
| 2560x1440 | 2 | Former scene-wide Glow (old) | 1 | 0.376 | 1.144 | 56.250 |
| 2560x1440 | 2 | Former scene-wide Glow (old) | 8 | 1.601 | 2.380 | 56.250 |
| 1920x1080 | 1 | Effects off | 1 | — | 0.288 | 0.000 |
| 1920x1080 | 1 | Bloom High | 1 | 0.144 | 0.412 | 21.093 |
| 1920x1080 | 1 | Bloom High | 8 | 0.117 | 0.381 | 10.715 |
| 1920x1080 | 1 | Glow | 1 | 0.144 | 0.464 | 52.734 |
| 1920x1080 | 1 | Glow | 8 | 0.116 | 0.435 | 42.355 |
| 1920x1080 | 1 | Glow + Bloom High | 1 | 0.274 | 0.547 | 73.827 |
| 1920x1080 | 1 | Glow + Bloom High | 8 | 0.222 | 0.507 | 53.070 |
| 1920x1080 | 1 | Bloom Low | 1 | 0.120 | 0.388 | 13.186 |
| 1920x1080 | 1 | Bloom Low | 8 | 0.119 | 0.369 | 10.590 |
| 1920x1080 | 2 | Effects off | 1 | — | 0.473 | 0.000 |
| 1920x1080 | 2 | Bloom High | 1 | 0.297 | 0.838 | 42.186 |
| 1920x1080 | 2 | Bloom High | 8 | 0.236 | 0.787 | 21.429 |
| 1920x1080 | 2 | Glow | 1 | 0.290 | 0.818 | 105.467 |
| 1920x1080 | 2 | Glow | 8 | 0.233 | 0.792 | 84.711 |
| 1920x1080 | 2 | Glow + Bloom High | 1 | 0.570 | 1.077 | 147.654 |
| 1920x1080 | 2 | Glow + Bloom High | 8 | 0.457 | 0.956 | 106.140 |
| 1920x1080 | 2 | Bloom Low | 1 | 0.242 | 0.735 | 26.371 |
| 1920x1080 | 2 | Bloom Low | 8 | 0.239 | 0.781 | 21.181 |
| 2560x1440 | 2 | Effects off | 1 | — | 0.715 | 0.000 |
| 2560x1440 | 2 | Bloom High | 1 | 0.456 | 1.202 | 74.990 |
| 2560x1440 | 2 | Bloom High | 8 | 0.343 | 1.081 | 38.088 |
| 2560x1440 | 2 | Glow | 1 | 0.459 | 1.204 | 187.490 |
| 2560x1440 | 2 | Glow | 8 | 0.346 | 1.081 | 150.588 |
| 2560x1440 | 2 | Glow + Bloom High | 1 | 0.868 | 1.670 | 262.480 |
| 2560x1440 | 2 | Glow + Bloom High | 8 | 0.641 | 1.402 | 188.677 |
| 2560x1440 | 2 | Bloom Low | 1 | 0.361 | 1.108 | 46.874 |
| 2560x1440 | 2 | Bloom Low | 8 | 0.347 | 1.095 | 37.649 |

## Source cost, memory and CPU limits

Emission capture is an additional MRT output during existing background/opaque/transparent work, with no additional scene geometry pass. Its isolated cost cannot be inferred from the opaque interval: that interval includes ordinary shading and is sensitive to clock/scheduling changes. The fixture records complete frame time as well as filter, opaque, background, transparent and presentation intervals; old recordings contain only opaque/filter/full-frame intervals. Do not label an opaque timing difference as the exact cost of emission capture.

At 1440p, Glow’s full-resolution RGBA32F emission source is 56.25 MiB per view. At Spread 1, the FP32 weighted pyramid is about 37.495 MiB, giving Glow 93.745 MiB and both effects 131.240 MiB per view. Bloom alone requires no emission source. Wide bases use less reconstruction storage: Spread 8 is 19.044 MiB for Bloom, 75.294 MiB for Glow and 94.338 MiB for both. Smaller Spread can raise the base as far as full resolution and therefore cost more memory and GPU time; the minimum footprint is limited by that resolution.

Pyramids retire after three owner frames when disabled or resized. Once allocated, the separate emission source remains resident until its viewport is resized/released, even when Glow is disabled; it is not attached or rendered in the disabled path. The active-only tables use fresh owners and must not be presented as immediate memory reclamation on every toggle. Releasing Scene/Game/preview owners releases their resources.

The complete concrete pass/resource table is validated once after the last allocation and before immutable draw collection. Prepass, clustered-light and GI/AO dispatch boundaries retain their own resource validation. Glare checks the actual declared pass sequence without repeating every prior allocation-alias scan for every level. This avoids redundant algorithmic work, but the measured CPU wall intervals do not support an isolated CPU speedup claim.

| 1440p, one view | Spread | CPU submission wall median | CPU submission wall P95 |
| --- | ---: | ---: | ---: |
| Effects off (old) | 0 | 0.463 | 0.958 |
| Former scene-wide Glow (old) | 1 | 0.690 | 1.358 |
| Former scene-wide Glow (old) | 8 | 1.296 | 2.456 |
| Effects off | 1 | 0.593 | 1.597 |
| Bloom High | 1 | 0.702 | 1.221 |
| Bloom High | 8 | 0.689 | 1.024 |
| Glow | 1 | 0.730 | 2.138 |
| Glow | 8 | 0.660 | 1.346 |
| Glow + Bloom High | 1 | 1.151 | 3.143 |
| Glow + Bloom High | 8 | 0.980 | 2.448 |
| Bloom Low | 1 | 0.706 | 1.885 |
| Bloom Low | 8 | 0.709 | 1.678 |

## Correctness evidence

Focused CPU checks cover settings validation, every blend-mode round trip, all seven weights, legacy conversion without writes, undo/redo, cancellation, duplication and Play isolation; pass checks cover every one-to-seven weighted level count and zero-to-five prefilter count, missing producers, wrong extents, feedback hazards and the 62-pass maximum profile. Material/shader checks validate all five lighting/emission outputs and reject target reassignment.

DX11 and DX12 GPU fixtures check finite FP32 transport of 1e11 radiance at exposure 256, normalized constant energy/channel ratios, odd and 1-pixel extents, source isolation, visible/occluded sunlight, opaque occlusion, transparent emitter energy, additive zero-coverage particles, tint/saturation, core/halo shaping, all blend modes, Mix endpoints and exact bypasses, gray-matte Soft Light, anamorphic axes, independent Scene/Game owners and combined SSGI/Glow/Bloom MRT operation.

The first wide-stride tent prototype produced a visible 3-by-3 grid of displaced sources at Spread 8 despite passing earlier numeric checks. It was replaced before delivery. Maximum-spread images now receive an explicit cardinal-ray regression against detached bright lobes. The final fixture’s Level 1/Level 7 halo radii are about 9/104 pixels at Spread 1; transparent opacity .25 retains about .243 of the opaque halo’s displayed energy. These describe the synthetic fixture, not universal pixel sizes or a physical energy measurement after display clipping.

## Final integration verification

Published renderer `c140594` is pinned by both consumers. Engine `Build.cmd Release` succeeded; project material-cache preparation completed all six topologies with zero failures. Thirteen distinct focused CPU checks passed across the implementation/integration runs. The existing native-resource and atomic shader-reload fixtures passed, as did SSGI, fog-background and SMAA fixtures on DX12 and DX11. The new Glow/Bloom quality fixture also passed both backends.

Quader Full/Public Release builds and their native runtime/binary-policy closures passed against the same pin. Its four Vulkan composition, scene, editor-overlay-parity and depth probes ran sequentially and passed presented/offscreen comparisons. These verify modeling/editor compatibility; they do not add rich Forward+ Glow/Bloom to Quader's Vulkan recipe.

## Artifacts and reproduction

Local artifacts are under `build/glow-bloom/`: `baseline/glow-bloom-benchmark.json`, `candidate-final-5821abd/glow-bloom-benchmark.json`, `comparison.json`, `comparison.csv`, the preserved baseline executable/fixture, and DX11/DX12 quality PNG/JSON directories. Each measurement directory has a separate `provenance.json` with fixture/executable SHA-256 hashes and sample-count verification. Build outputs are intentionally not versioned.

After an ordinary Release build, run the focused tests or explicit benchmark:

```powershell
ctest --test-dir build/Release -C Release --output-on-failure -R '^glow_bloom_render(_dx11)?_core$'
& ./build/Release/bin/glow_bloom_render_tests.exe --benchmark --warmup 240 --samples 600 --artifacts build/glow-bloom/repeat
```

The test backend is DX12 by default; `CRIMSON_TEST_RENDERER=dx11` selects DX11 for the focused fixture. Run native GPU probes sequentially and avoid simultaneous compilation/other benchmark processes when comparing times.

## Primary references

- [Spencer et al., Physically-Based Glare Effects for Digital Images, SIGGRAPH 1995](https://doi.org/10.1145/218380.218466): broad glare/point-spread motivation. Crimson implements an artistic multiscale approximation, not the complete paper’s eye model.
- [Jorge Jimenez, Next Generation Post Processing in Call of Duty: Advanced Warfare](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/): production multiscale post-processing and stable source reduction.
- [Filament bloom downsample](https://github.com/google/filament/blob/main/filament/src/materials/bloom/bloomDownsample.mat) and [upsample](https://github.com/google/filament/blob/main/filament/src/materials/bloom/bloomUpsample.mat): established filtered-pyramid implementation reference.
- [Godot Environment controls](https://docs.godotengine.org/en/stable/classes/class_environment.html): seven levels and familiar artistic blend controls. Crimson uses separate emission Glow and scene-wide Bloom and does not promise pixel parity.
- [W3C compositing and blending, Soft Light](https://www.w3.org/TR/compositing-1/#blendingsoftlight): standard polynomial/square-root blending curve; Crimson maps zero Glow to the neutral source point and blends in linear presentation space.
