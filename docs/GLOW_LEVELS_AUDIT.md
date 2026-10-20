# Glow levels audit and consolidation

Audited 2026-10-20. Current controls are described in [the lighting guide](RENDERER_AND_CHARACTER_LIGHTING.md#glow). The canonical renderer code is `05050d3`, published with documentation as `54fa5b7`. This supersedes the two-effect implementation measured in [the historical Glow/Bloom checkpoint](GLOW_BLOOM_CHECKPOINT.md).

## Source comparison

The inspected Godot checkout is 4.7.0 beta at `C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/godot-master`. Godot groups seven levels under one full-scene **Glow** effect. Its Bloom parameter changes how much below-threshold scene color contributes; it is not an independent emission or sunlight effect. `scene/resources/environment.cpp` groups the controls at lines 1462–1471, and `_update_glow` at line 732 normalizes levels only when the optional normalization setting is enabled. `scene/resources/environment.h:168` defaults that setting to false. `servers/rendering/renderer_rd/shaders/effects/tonemap.glsl:371` combines the authored weighted levels; the input is the internal HDR scene texture in `renderer_scene_render_rd.cpp:607`. See also the [official Environment reference](https://docs.godotengine.org/en/stable/classes/class_environment.html).

The inspected Unreal installation is 5.8.0, CL 55116800, branch `++UE5+Release-5.8`, under `C:/Program Files/Epic Games/UE_5.8/Engine`. Standard desktop Bloom uses the visible HDR scene and six Gaussian scales, with individual sizes and tints. `Source/Runtime/Engine/Classes/Engine/Scene.h:1648` begins the six scale properties. `Source/Runtime/Renderer/Private/PostProcess/PostProcessBloomSetup.cpp:225` combines broad-to-tight scales; line 253 applies a fixed scale factor based on six stages, rather than dividing by the sum of authored contributions. `Shaders/Private/FilterPixelShader.usf:102` adds the accumulated layers. The final contribution is added before tone mapping in `Shaders/Private/PostProcessTonemap.usf:523`. Unreal's alternative convolution Bloom is a different optical-kernel technique, not a sunlight-specific effect. See the [Bloom overview](https://dev.epicgames.com/documentation/unreal-engine/bloom-in-unreal-engine) and [six-scale settings API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FGaussianSumBloomSettings).

Both implementations distinguish normalized blur kernels from artistic scale gains. A kernel should preserve a constant input; the artist's gain should then control how much of that filtered image is added. Crimson follows that distinction without claiming pixel parity with either renderer.

## Reproduced defects and fixes

The previous renderer divided every level by the sum of all level values. With only one nonzero level, every positive setting became a gain of one. In the FP32 constant-field fixture, Level 1 at 0.01 produced energy 2786.875 instead of 27.86875: a 100-fold error. This explains the apparent on/off slider behavior.

Reconstruction also changed its fine-scale filter when a coarser level first became active. A single active level received an additional tent filter, but the same level combined with an arbitrarily small coarser gain used an unfiltered center sample. Turning Level 2 from zero to 0.0001 changed the image by 0.13582586 in the impulse fixture, far beyond the 0.00008831 tolerance.

The current implementation uses direct authored gains and a consistent fine-scale center sample. The source down filters and coarser reconstruction filters still normalize their samples. Empty levels are pruned without changing the surviving filter shapes. The allocation loop also bounds the six reconstruction targets independently of the seven down levels.

| Check | Previous implementation | Corrected implementation |
| --- | ---: | ---: |
| Constant Level 1 gain 0.01, energy | 2786.875 | 27.868749 |
| Constant Level 1 gain 0.5, energy | 2786.875 | 1393.4375 |
| Constant Level 1 gain 1, energy | 2786.875 | 2786.875 |
| Level 1 to Level 2 epsilon-transition error | 0.13582586 | 0.00000000372529 |
| Contribution/continuity checks | 75 of 114 failed | 114 of 114 passed on DX12 and DX11 |

Response is measured in FP32 HDR before tone mapping. The final displayed result may compress bright values or clip in display space, so equally spaced slider changes need not look equally bright.

## Delivered renderer contract

There is one full-scene HDR **Glow** effect. It includes visible emission, sunlight/sky, reflections and directly lit highlights. Seven independent 0–1 gains run from tightest to widest. Default gains are `[.20, .25, .20, .15, .10, .06, .04]`; Intensity defaults to .1. Levels 6–7 emphasize broad haze. A multiscale Unreal-like result generally combines broad levels with tighter levels; enabling only the widest levels is an artistic choice, not an Unreal mode.

Glow retains Additive, Screen, Soft Light, Replace and Mix composition, plus Tint, Saturation, Highlight Threshold, Threshold Softness, Spread, Anamorphic Stretch and Low/High Quality. Main controls and Levels remain directly accessible; technical/artistic refinements are grouped under Advanced. Blend modes add no filtering passes. Existing Scene/Game and independent preview ownership remain intact.

The single `GlowGpu` owner reads the existing HDR target. The separate Bloom owner and emission capture texture/framebuffers are removed. Material ABI v30 restores four surface color outputs and removes `u_glowSource`; the twelve material samplers and mesh vertex layout are unchanged. Up to five source prefilters, seven down levels and six reconstruction passes give a maximum of eighteen compute dispatches. Disabled, zero-strength, zero-spread, all-zero levels and Mix-zero settings schedule no Glow work. Numeric physical view IDs remain stable.

Old scenes convert only in memory. Current saves contain `glow` with `source: "Scene"`; they contain no separate Bloom or Core/Halo Balance. For the previous two-effect representation, active full-scene Bloom takes precedence, otherwise active Glow is retained, otherwise inactive Bloom tuning is retained if present. Old Bloom weights are normalized once to preserve their previous effective gains. Old emission Glow's Core/Halo Balance converts to seven gains. If both old effects were active, the consolidated effect cannot preserve two independently selected sources; the full-scene tuning wins. Loading does not write files, dirty scenes or change schema versions. Current source markers prevent repeated conversion.

## Verification evidence

The corrected contribution suite covers all seven levels at zero, .01, .1, .25, .5 and one; constant and low-energy impulse inputs; mixed-level additivity, scaling and independence; pruning continuity; and disabled-resource retirement. It passes all 114 checks on DX12 and DX11. The full scene suite passes on both APIs, including visible-source eligibility, actual procedural-sky sun, covered-source rejection, all blends, tint, saturation, extreme HDR, alpha coverage, maximum Spread, SSGI compatibility and independent Scene/Game resources. Horizontal Anamorphic measured 37.15 pixels horizontal RMS versus 10.43 vertical RMS, with axes swapped for the vertical setting.

The focused CPU suite checks scene serialization/migration/history/Play isolation, material generation v30, exact four-output shader preflight, shader compiler contracts, resource/pass declarations and tone mapping. The hidden native resource fixture additionally checks texture streaming, shader compilation, cutout/normal handling, thumbnails, live material/particle previews and platform windows.

Raw artifacts are under `build/glow-bloom/levels-red-c140594` for the failing baseline and `build/glow-levels/green-05050d3`, `quality-05050d3-dx12` and `quality-05050d3-dx11` for the corrected renderer. The historical checkpoint retains its original measurements; those describe the replaced two-effect pipeline.

## Performance measurement

Release, Direct3D 12, NVIDIA GeForce RTX 4090 (10DE:2684), driver 32.0.16.1074. The final executable and fixture hashes, source provenance and adapter inventory are captured under `build/glow-levels/benchmark-54fa5b7/provenance.json`. The three-instance hidden offscreen fixture matches the previous checkpoint: wall, floor and emissive source; GI, AO, shadows and SMAA disabled. It uses Intensity .8 to match the previous measurement, rather than the new UI default .1. One view is Game; two views are independent Scene/Game owners.

Each of twenty phases uses 240 warmup frames and 600 distinct coherent completed GPU samples. Per-view timestamps must belong to the aggregate sample's frame. Targets are recreated outside each measured phase, so the memory figures describe active allocations. The run covers 1920×1080 and 2560×1440, one/two views, disabled Glow, and High/Low at Spread 1/8. All twenty phases met their sample count. Raw samples are in `benchmark-54fa5b7/glow-benchmark.json`; `comparison-54fa5b7.json` and `.csv` retain 28 comparisons against the unchanged historical dataset.

| Resolution/views | Quality/spread | Filter median / P95 (ms) | Whole-frame median / P95 (ms) | Active MiB | Dispatches |
| --- | --- | ---: | ---: | ---: | ---: |
| 1080p / 1 | High / 1 | .143 / .145 | .403 / .464 | 21.093 | 13 |
| 1440p / 1 | Off | — | .416 / .577 | 0 | 0 |
| 1440p / 1 | High / 1 | .224 / .230 | .459 / .613 | 37.495 | 13 |
| 1440p / 1 | High / 8 | .161 / .171 | .511 / .640 | 19.044 | 16 |
| 1440p / 1 | Low / 1 | .173 / .184 | .527 / .663 | 23.437 | 14 |
| 1440p / 1 | Low / 8 | .166 / .176 | .513 / .623 | 18.825 | 17 |
| 1440p / 2 | High / 1 | .455 / .458 | .896 / 1.199 | 74.990 | 26 |

The corresponding historical 1440p single-view Spread 1 figures were:

| Previous configuration | Filter median / P95 (ms) | Whole-frame median / P95 (ms) | Active MiB | Dispatches |
| --- | ---: | ---: | ---: | ---: |
| Full-scene Bloom High | .227 / .459 | .611 / .841 | 37.495 | 13 |
| Emission-only Glow plus Bloom High | .429 / .701 | .834 / 1.406 | 131.240 | 26 |

The corrected single full-scene filter costs essentially the same as the previous single full-scene filter (.224256 versus .227328 ms, a 1.35% difference). Consolidating the previous two-effect combination removes one pyramid and the full-resolution emission capture: .429056 to .224256 ms filtering, and 131.240 to 37.495 MiB active effect memory. The removed 2560×1440 RGBA32F emission target alone is 56.25 MiB per view. These are different source/style capabilities; the consolidated image is not equivalent to two independently tuned effects.

GPU timestamps measure elapsed intervals, including desktop scheduling. Source-phase and whole-frame medians vary across the historical/current runs and even among current phases; subtraction of those medians does not isolate Glow cost. The table therefore reports filter intervals directly and does not claim an editor FPS gain or universal whole-frame speedup. Low quality reduces default filtering time and allocations, but broad Spread may have similar cost because progressive prefiltering still processes the source. CPU submission wall time includes synchronization and is retained only as diagnostic data.

The final `Build.cmd Release` integration build passed, including six of six material topologies and zero material compiler failures. Ten post-build CPU checks and all seven shader-reload/SSGI/fog/SMAA GPU integration checks passed, including DX11 and DX12 coverage. Quader's Full/Public Release builds, recursive runtime closure checks and all four native Vulkan composition/scene/editor/depth probes passed at the same renderer pin. The rich Glow pass remains a DX11/DX12 Forward+ feature; this compatibility result does not claim that Quader's borrowed Vulkan modeling path now exposes it.
