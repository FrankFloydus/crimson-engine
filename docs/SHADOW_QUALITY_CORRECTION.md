# Directional shadow quality correction

Recorded 15 October 2026. This corrects the bright-surface acceptance failure
of the [second renderer migration step](SHADOW_ARCHITECTURE_CHECKPOINT.md).
The old dark-room benchmark established work and memory changes, but did not
establish directional shadow quality on broad, brightly lit surfaces. The
reported floor and pillar acne was a renderer defect at the default light
biases.

This document preserves the correction's original measurements and artifact identities. For today's ordinary scene lighting, first-person cascades, point-source softness and controls, read [Renderer and character lighting](RENDERER_AND_CHARACTER_LIGHTING.md), [Shadows diagnostics](SHADOW_DIAGNOSTICS.md) and [the authoritative architecture](ENGINE_ARCHITECTURE.md). No timings or image metrics below were rerun for this documentation update.

## Reproduction and retained baseline

The saved authored scene was copied, without editing the original project,
to `build/verification/shadow-quality-baseline/project`. The diagnostic never
imports into or writes back to the authored project. Unsaved editor state is
outside that snapshot.

The copied scene is
`assets/scenes/362f37bb40b1d7cd500d7e0a3c438552.crimson-scene`, named
`New Scene`. It contains ten model instances, one directional light and no
point lights. The floor and seven narrow pillars have white material factors
and a gray tiled texture. The player subtree contains the hero model. All
these models cast and receive shadows.

The saved directional light uses Cast Shadows, intensity 2, Shadow Distance
100 m, Depth Bias 0.005 m and Normal Bias 1 texel. Its extracted direction is
approximately `(-0.354550, -0.611821, -0.707085)`. The biases are the engine
defaults. The Game camera is at `(32, 24, 24)` with vertical FOV 15 degrees;
its view direction is approximately `(-0.612372, -0.5, -0.612372)`. Environment
lighting intensity is zero, with fog and glow disabled. Both saved Scene and
Game views expose the defect.

`build/verification/shadow-quality-results/saved-scene-inspection.json`
retains the exact serialized settings and transforms. The completed Step 2
binary and original sources/shaders are retained under
`build/verification/shadow-quality-baseline`. Its identities are:

| Artifact | SHA256 |
| --- | --- |
| Release renderer | `41DB0BD0DF45ECE0CF04C983DF2CDB67CB8F7DE0525AAB514DA82E951B0A1F4E` |
| Production room benchmark | `707346112D9A161A787BC7840D981D10EACA5BE1E21F27AF84F1294DA7B10948` |
| Old-renderer quality probe | `EFCC18E8E0B9E65EC657E8917FC64860FF9E6AF650BAEBD82B57B1AA280FDE93` |

The renderer was built at 14:39:52 local time. The baseline identity and
`sha256-files.json` record the precise source and artifact set; repository
HEAD alone does not identify this build because existing uncommitted work
was present.

## Defect and correction

The directional receiver shader already attempted receiver-plane correction
for its PCF samples. Its direct matrix indexing extracted the wrong
mathematical basis under the shader's GLSL/HLSL portability conventions.
That corrupted the plane depth gradient and the texel-sized normal offset
for oblique lights. Changing a user's light biases could conceal parts of
the result without correcting the calculation.

The correction extracts the mathematical rows with explicit row-vector
`mul` operations. The shadow lookup's normal offset and receiver-plane
gradient therefore use the same orthographic projection basis as the
rendered depth map, independent of source-language matrix indexing.
Geometric plane normals remain separate from smooth or normal-map shading
normals.

The single Game world map also needs to spend resolution on visible
receivers, rather than the empty volume of the entire bounded camera
frustum. The accompanying receiver-aware fit crops the bounded projection
to relevant receiver coverage while retaining upstream caster coverage.
The independent character projection and the Scene inspection cascades
remain part of the architecture.

`DirectionalShadowMath.cpp` intersects each receiver box with the bounded
camera frustum using both contained corners and edge/plane intersections
from both shapes. Merely retaining receiver corners inside the frustum
would miss a large floor crossing the view with no contained corners. The
intersection is computed relative to the camera with double intermediates.
Its light-space XY envelope determines the quantized square extent and
texel-snapped center. Intersecting upstream casters still extend the depth
range independently of XY receiver fitting. Missing or invalid receiver
metadata falls back conservatively to the complete bounded-frustum fit.

The production preparation freezes receiver bounds before caster/residency
filtering, including non-casting receivers. `RenderWorldSnapshot` carries
those same values into execution. Valid lit particles also contribute one
conservative aggregate box, respecting submission limits and generated
material shading. A particle that receives direct lighting must not lose
shadow coverage merely because it is not a mesh caster. Direct renderer
submissions derive equivalent model and lit-particle bounds when prepared
metadata is absent.

`directional_shadow_receiver_plane_tests.cpp` checks the portable projection
basis independently of shader execution. `directional_shadow_math_tests.cpp`
checks receiver clipping, conservative fallback, stabilization and caster
coverage. The GPU quality test below checks the visible material result.
These layers are complementary: an apparently plausible shadowed frame did
not reveal the old shader's matrix-basis defect in the dark-room fixture.

## Regression test and fail-before proof

`tests/directional_shadow_quality_tests.cpp` renders a white floor and
four-metre pillar through both fallback and generated material programs.
It uses the saved Game camera and Scene orbit camera. Its matrix covers:

- Scene and Game views.
- 256 and 1,024 texel maps.
- Default Normal Bias 1 and Normal Bias 0, with Depth Bias 0.005 m.
- The saved oblique sun, a grazing sun and another oblique sun.
- Built-in fallback and generated white material shaders.

Those 48 configurations compare lit floor interiors and the two lit pillar
faces against an otherwise identical Receive Shadows off reference. Every
accepted sample must lose at most 3/255 in any RGB channel. Floor sampling
requires at least 100 visible lit samples; pillar sampling requires at
least 20. Pillar samples are at least 0.5 m above the floor and away from
the face edges. A separate near-contact check requires genuine pillar
occlusion 0.15 m beyond its footprint in both views, both resolutions and
both normal-bias settings. It prevents an acne fix that simply erases or
detaches nearby shadows.

Only the new probe translation unit was compiled for the fail-before
check. It linked the retained Step 2 production objects and libraries,
including their original embedded fallback shaders. The source and compile
and link response files are retained under
`shadow-quality-baseline/quality-proof`; old production artifact provenance
is recorded in `baseline-identity.json`. No engine source was rebuilt for
that proof.

The old renderer failed the first configuration: fallback Scene, 256 texel
map, saved sun direction and default Normal Bias 1. Across 2,398 lit floor
samples it produced maximum darkening 40/255, mean darkening 7.849/255 and
4,632 RGB channels exceeding the 3/255 tolerance. The expected failure log is
`build/verification/shadow-quality-results/quality-before.log`; reference
and shadowed PNGs are under `quality-before/`.

The separate actual-scene baseline at its normal map resolution reproduces
the visible floor/pillar pattern in `before-saved-a/room_static_shadows-*.png`.
The corresponding `room_no_shadows-*.png` controls are smooth. This makes
the old test's failure relevant to the reported scene rather than only to
a synthetic fixture.

The corrected renderer passes all 48 configurations and eight near-contact
cases. Across 116,664 lit floor samples, maximum and mean darkening are both
zero. Across 5,280 lit pillar samples, maximum darkening is 1/255; no sampled
channel exceeds the 3/255 tolerance. That one-level difference occurs
only in the 256-texel Scene case with the saved sun and default normal bias,
in both material paths. The direct-run log, parsed metrics and 192 PNGs are
retained as `quality-after.log`, `quality-after-summary.json` and
`quality-after/` under the results directory.

The final incremental Release build passed. All twelve focused CTest checks
passed in 46.67 seconds, including the directional math, portable receiver
plane, material shader/compiler/preflight, prepared frame/pass plan, shadow
scheduler, point shadow math/render and Forward+ render checks. The final
quality CTest took 2.63 seconds; the point render check took 7.13 seconds and
Forward+ render check 3.86 seconds. `repair-build.log` and `final-tests.log`
retain the results. The CPU receiver-plane oracle covers 288 projections,
1,104 planes and 26,496 PCF footprints; the prepared-frame check includes
lit-particle receiver coverage. The separate direct quality run adds the
captured image evidence and does not replace those checks.

## Matched actual-scene benchmark

The retained old and corrected production benchmark use the same frozen
saved project and the production `prepareEditorRenderFrame` /
`Renderer::renderFrame` path. The required native Quader filename and scene
ID arguments supply metadata only in `--saved` mode: the workload loads
the copied saved combined scene, without reimporting its native file.

Each run renders hidden D3D11 Scene and Game targets, each 909 by 684, with
vsync disabled and an explicit immediate-context Flush. Four phases cover
the saved non-Quader records, the full scene with shadows disabled, static
cached shadows, and the player subtree plus following camera moving through
the same deterministic sixty-frame sequence. The sun remains stationary.
The submitted material clock is zero. Each phase waits for resource
readiness and at least 300 warmup frames, then measures 2,000 frames. Eight
PNG captures replay the final measured pose after timing; readback frames
are excluded from timing.

This is renderer/resource evidence. It excludes full editor widgets,
browser work, Quader watching, Play physics/AI and animated crowds. GPU
query samples are asynchronous and are not synchronized with each CPU
CSV row. GPU clocks and desktop background activity are uncontrolled.

Before/after evidence is retained under
`build/verification/shadow-quality-results/before-saved-a` and
`after-saved-a`. The runs used the NVIDIA GeForce RTX 4090, driver 610.74.
Before-run snapshots reported P8, 210/225 MHz graphics and 405 MHz memory;
after-run snapshots reported P0, 1,935/1,755 MHz graphics and 10,501 MHz
memory. These are point observations, not controlled clocks throughout
the runs.

All values below are phase means except the explicitly labeled wall p95.

| Phase | Wall before | Wall after | Wall p95 before | Wall p95 after | GPU before | GPU after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Non-Quader records | 0.2654 ms | 0.2620 ms | 0.3496 ms | 0.3286 ms | 0.1955 ms | 0.1436 ms |
| Full scene, shadows off | 0.4293 ms | 0.4268 ms | 0.5060 ms | 0.4873 ms | 0.1204 ms | 0.1142 ms |
| Full scene, cached shadows | 0.6551 ms | 0.6759 ms | 0.7161 ms | 0.7920 ms | 0.1537 ms | 0.1514 ms |
| Moving player/camera | 0.7074 ms | 0.7012 ms | 0.7966 ms | 0.7837 ms | 0.2550 ms | 0.2163 ms |

| Phase | Shadow preparation before/after | Receiver preparation before/after | Renderer submission before/after |
| --- | ---: | ---: | ---: |
| Non-Quader records | 0.0552 / 0.0570 ms | 0.0790 / 0.0791 ms | 0.0241 / 0.0246 ms |
| Full scene, shadows off | 0.0001 / 0.0001 ms | 0.2774 / 0.2775 ms | 0.0322 / 0.0320 ms |
| Full scene, cached shadows | 0.2086 / 0.2185 ms | 0.2577 / 0.2641 ms | 0.0668 / 0.0674 ms |
| Moving player/camera | 0.2141 / 0.2167 ms | 0.2615 / 0.2607 ms | 0.0793 / 0.0764 ms |

The cached phase has a measured wall increase of 0.0209 ms, or 3.19%, and
the moving phase a decrease of 0.0063 ms, or 0.89%. CPU receiver clipping
adds preparation work. GPU timing varies even in the unchanged shadows-off
control, which fell 5.17%; the non-Quader phase fell 26.54%. Those observations
do not establish a GPU or overall speedup. This is a correctness and
resolution-use repair, with the absolute measured costs above.

The full saved scene submits eight Scene receivers and six Game receivers
in both runs. Static warmed shadows require no depth writes. The old moving
phase wrote four Scene cascades, one Game world map and one character map
per sample, with 49 shadow draws. The corrected phase averages 5.367 total
projection writes and 41.202 shadow draws: the tighter world projection
stays unchanged through part of the motion and can reuse its cached depth.
Character projection writes remain one per sample. These work counts are
independent of clock variation.

Shadow allocation stays 33,817,088 bytes. Full-scene model allocation stays
1,349,016 bytes; tracked texture/program bytes rise from 5,831,128 to
5,836,224 due to the changed programs. This fixture contains no point
lights, so it does not measure the local shadow budget.

Both shadows-off PNGs are exact RGBA matches. All six shadowed PNGs change
as expected; static Scene and Game change 66.89% and 55.98% of pixels,
respectively, as widespread false darkening is removed. These image
differences quantify the changed output, not residual error. Visual
inspection confirms smooth lit floors and pillar faces, with real pillar
and hero shadows retained in both views. Scene retains its cascaded
coverage/detail tradeoff; Game's separate character map provides sharper
hero detail.

`comparison-saved-a.json` contains the complete timing percentiles, work,
memory and decoded PNG comparisons. It is generated with
`scripts/Compare-FrameArchitecture.py`. The raw CSVs retain 8,000 measured
frames per run.

## Final artifact identities

The corrected artifacts, complete sources/shaders, libraries, production
benchmark objects and test sources are retained under
`build/verification/shadow-quality-candidate`. Its `candidate-identity.json`
and `sha256-files.json` identify the exact build:

| Artifact | SHA256 |
| --- | --- |
| Release renderer | `0250C91292A0D680B4E4E146DF090A4513B75181559EAFDC08CE9D8B8C69B049` |
| Production room benchmark | `F618E4A84B6AAA8DBA82D9E872D137437FC510D8B73CD48B8B6F518463AED843` |
| Quality regression test | `614C048248607842017DF36353656F421A73044B68700A1BBEDFCE1E4553AAA3` |

The renderer build time is 15:35:10 local time. No renderer application was
running during the matched diagnostics, and no UI control or authored
asset edits were performed. The frozen project predates the correction;
only executable/shader/compiler paths change between matched runs.

## Reproduction commands

Run from the repository root in PowerShell, using the retained baseline
paths or the final candidate paths recorded with the results:

```powershell
$qualityBase = (Resolve-Path 'build/verification/shadow-quality-baseline').Path
$frameBase = (Resolve-Path 'build/verification/frame-architecture-baseline').Path
& (Join-Path $qualityBase 'bin/forward_plus_room_benchmark.exe') `
  (Join-Path $qualityBase 'project') `
  (Join-Path $frameBase 'frozen-native/q-test/q-test.qproj') `
  '6aeb7097-5046-450e-a451-a8b4518aa095' `
  (Join-Path $qualityBase 'shader-tools/shaderc.exe') `
  (Join-Path $qualityBase 'shaders') `
  (Join-Path $frameBase 'bgfx-includes') `
  --saved --warmup 300 --samples 2000 --capture `
  --output 'build/verification/shadow-quality-results/reproduction-before'
```

The quality probe accepts `shaderc`, the engine shader-source directory and
the bgfx shader-include directory. Add `--capture output-directory` to
retain every reference/shadowed image. The before probe is intentionally a
failing regression artifact.

For the corrected benchmark, replace the executable, shader-tool and shader
source paths with `shadow-quality-candidate`, keep the project under
`shadow-quality-baseline/project`, and choose a new result directory. For
the corrected quality probe:

```powershell
$qualityCandidate = (Resolve-Path 'build/verification/shadow-quality-candidate').Path
& (Join-Path $qualityCandidate 'bin/directional_shadow_quality_tests.exe') `
  (Join-Path $qualityCandidate 'shader-tools/shaderc.exe') `
  (Join-Path $qualityCandidate 'shaders') `
  (Join-Path $frameBase 'bgfx-includes') `
  --capture 'build/verification/shadow-quality-results/reproduction-quality'
```

## Scope and acceptance

This correction addresses directional self-shadow acne and resolution use
for the bounded Game map. The existing local-light slot budget, PCF filter
width and lack of scene antialiasing are separate constraints. Smooth,
antialiased shadow silhouettes at every scale are not established by the
lit-interior regression checks. Ambient attenuation, player light rigs and
lighting channels were later stages relative to this correction and have since
landed. Current point-source PCSS is also separate from this directional-PCF
quality evidence.

The receiver fit uses conservative model boxes, rather than exact visible
triangle footprints. Large or loose bounds can therefore still spend map
resolution on empty space. The aggregate particle box can likewise span
space between distant sprites. Invalid/missing metadata deliberately falls
back to the broader frustum, preserving coverage at lower precision. These
choices favor correct receiving over silently dropping shadows and must
remain explicit in future spatial/culling work.

For manual acceptance, reopen the saved bright floor/pillar scene and keep
its original directional settings: Cast Shadows on, Shadow Distance 100 m,
Depth Bias 0.005 m and Normal Bias 1. Inspect both Scene and Game views.
Broad lit surfaces should stay smooth while pillar and hero contact shadows
remain visible. Toggle Cast Shadows or a receiver's Receive Shadows for a
direct visual control, then restore the original setting. Use the existing
Play control and F1 metrics overlay for the gameplay view; this benchmark
does not substitute for Play-mode acceptance.

## Eight-point-light follow-up

The subsequent saved scene adds eight enabled colored point lights, each
with intensity 35, radius 7 m, Depth Bias 0.005 m and Normal Bias 1. All
eight saved lights have **Cast Shadows off**. Lighting eight point lights
therefore does not by itself exercise eight point-shadow cubemaps.

The saved active scene and all managed assets were copied to
`build/verification/point-eight-lights/project`. Its scene SHA256 is
`D488A02CEB3933D0E8CA3D9BADF2029F5FF7A387607BA0301C27E2AB8820EFEB`.
`fixture-identity.json` records the eight persistent IDs, positions, colors,
light settings, cameras and production binary identities; `project-sha256.json`
identifies the complete copied asset set. The active scene still contains
ten models and one directional light.

A second diagnostic-only copy, `project-shadow-enabled`, changes exactly
the eight `point.castShadows` values from false to true. A parsed semantic
comparison proves that every other scene value is equal. Its SHA256 is
`34DDA41C5B7AD5138524190151A10EFA38E355E72109AE49214E4E1698BDBD92`;
`shadow-enabled-diff.json` records the exact changes. No authored scene,
light setting or running application was modified.

Both copies ran the production room benchmark with the same four phases,
300 minimum warmup frames, 2,000 measured frames per phase and eight PNGs.
The executable and shaders are the corrected candidate identified above.
The native-file argument remains metadata-only in `--saved` mode. Results
are under `point-eight-lights/as-saved` and `point-shadows-enabled`.

The existing hidden point render regression also passed in 8.59 seconds,
covering cubemap faces, alpha masks, cast/receive flags, shared view updates,
budgeted admission/fairness, stale-projection rejection and shader reload.
Its log is `point-eight-lights/point-shadow-tests.log`. No further code
change or build was needed for this follow-up.

| Check | As saved | Eight point shadows enabled |
| --- | ---: | ---: |
| Combined point lights | 8 | 8 |
| Maximum point faces written in one warmup frame | 0 | 24 |
| Maximum initially deferred / critical lights | 0 / 0 | 4 / 4 |
| Static measured point cache hits per frame | 0 | 8 |
| Static measured point faces / deferred / critical | 0 / 0 / 0 | 0 / 0 / 0 |
| Moving measured point faces per frame | 0 | 24 |
| Moving point cache hits per frame | 0 | 4 |
| Maximum moving deferred / critical lights | 0 / 0 | 0 / 0 |
| Total shadow allocation | 33,817,088 bytes | 253,628,160 bytes |

All measured frames obey the shared 24-face update budget. Initial new-map
work is deferred when eight lights compete for four updates; it drains
during warmup and all eight lights become cached. The moving player affects
four nearby lights in this fixture. Their six-face updates consume the
24-face budget, while the other four retain cached maps. Static Scene/Game
views reuse coherent maps without rewriting point faces. Warmup face totals
also include later resource publications and are not a count of unique
lights admitted.

The enabled total shadow atlas is approximately 241.88 MiB, including the
directional maps and layout padding. This confirms the functional behavior
for eight shadowed lights; it does not reduce the existing cubemap memory
cost or remove the sixteen resident shadow-light limit. Forward+ light
assignment and shadow-map residency have separate budgets.

Visual inspection of both enabled-copy views shows smooth colored floor,
wall and pillar surfaces, with genuine pillar/hero occlusion. Enabling point
shadows changes 4.21% of static Scene pixels and 2.96% of static Game pixels.
Both shadows-off controls remain exact RGBA matches between copies.
`comparison-settings.json` retains decoded comparisons and raw timing data;
`counter-summary.json` extracts per-phase budget/cache/deferred maxima from
the 8,000-row CSVs.

**These timings are nonisolated observations.** The authored renderer
remained open as PID 87404 throughout the tests, using CPU/GPU resources.
GPU clocks and its workload varied. The two runs also change light shadow
settings rather than renderer implementation. They must not be compared
with the earlier scene without point lights as a performance optimization.

| Phase | As-saved wall / GPU mean | Enabled-copy wall / GPU mean |
| --- | ---: | ---: |
| Full scene, all shadows disabled | 0.6623 / 0.1650 ms | 0.4696 / 0.1511 ms |
| Full scene, static shadows | 0.9602 / 0.2080 ms | 0.7559 / 0.2144 ms |
| Moving player/camera | 1.2708 / 0.4270 ms | 1.0643 / 0.4938 ms |

The substantial timing difference in the identical shadows-off control
demonstrates that contention matters. The lower enabled-copy CPU times do
not establish a speedup. The retained counters, budget assertions, cached
static behavior and images establish this follow-up's functional result.
