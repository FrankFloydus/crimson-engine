# SSGI quality and cost audit — 2026-10-20

The reported symptom was large moving bands near a small bright emissive cube.
This audit separates tracing defects, reconstruction behavior, sampling noise,
default tuning and GPU cost. The implementation lives in the canonical renderer;
the Engine owns Inspector presentation and the measurement fixture.

## Research and implementation findings

- [McGuire and Mara, Efficient GPU Screen-Space Ray Tracing, JCGT 2014](https://jcgt.org/published/0003/04/04/)
  discusses near-plane clipping, perspective-correct stepping and banding from
  sparse strides. Crimson previously distributed its finite budget toward an
  endpoint clipped only to a tiny clip-W value. Rays toward the eye could project
  far beyond the viewport and miss visible intersections. The trace now clips
  the complete segment against near/far/viewport planes before distributing its
  steps. Independent marching-phase jitter replaces the deterministic stride;
  intersection refinement and the terminal endpoint remain covered.
- [Schied et al., Spatiotemporal Variance-Guided Filtering, HPG 2017](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/)
  and [NVIDIA Falcor's temporal reprojection](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/RenderPasses/SVGFPass/SVGFReproject.ps.slang)
  support geometry-validated history reconstruction. Crimson previously selected
  one nearest half-resolution history pixel and rejected by world-point distance.
  The resolve now gathers four history taps, validates each against the receiver
  plane/depth/normal/reactivity and renormalizes valid weights. Actual capture
  centers preserve alignment at odd dimensions. Existing moments and sparse-hit
  history protection remain. This is not a complete SVGF implementation: spatial
  reconstruction still uses two bilateral passes.
- [Unity HDRP's SSGI](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/ScreenSpaceLighting/ScreenSpaceGlobalIllumination.compute)
  uses separate blue-noise-sequence dimensions for cosine-hemisphere sampling.
  Crimson's paired gradient-noise coordinates had pronounced periodic spectral
  peaks. Direction seeds now use independent channels of Christoph Peters'
  [published CC0 blue-noise data](https://www.momentsingraphics.de/BlueNoise.html),
  with temporal low-discrepancy rotations and separate later-path dimensions.
  Integer [PCG scrambling](https://jcgt.org/published/0009/03/02/) remains for
  marching jitter.
- [Wolfe et al., Spatiotemporal Blue Noise Masks, EGSR 2022](https://research.nvidia.com/publication/2022-07_spatiotemporal-blue-noise-masks)
  explains why noise frequency and temporal convergence must be evaluated
  together. The delivered compact table is **2D blue-noise seeds plus temporal
  rotation**, not STBN or an importance sampler for emissive surfaces.

These sources guide the algorithm. The numerical defaults below are measured
Crimson choices, not prescriptions from a paper or another renderer.

The source PNG, pinned mirror revision, exact hashes, CC0 license and offline
generator are retained in the renderer's `third_party/blue-noise` and
`tools/generate_ssgi_blue_noise.py`. The table adds no texture binding, GPU target
or material ABI change. This preserves the existing eight-binding compute path
and twelve material texture slots.

## Moving-emitter follow-up

The initial experiments moved the camera while the emitter remained stationary.
They did not cover continuous donor motion. A subsequent reproduction found a
separate history ownership defect: a ray hitting reactive donor geometry marked
the primary receiving pixel reactive too. Temporal reconstruction then rejected
history on that bright hit and rejected the stored reactive sample on the next
frame. Sparse emissive hits therefore bypassed accumulation and the fixed spatial
filter spread their peaks into visible blotches. Even donor hits subsequently
rejected by capture-geometry validation could invalidate receiver history.

The correction removes only those two donor-to-receiver reactivity assignments.
The primary receiver's own transform/deformation/material reactivity and all
depth/normal history checks remain. Captured donor validity, geometry matching,
scope separation and energy limits also remain. A stationary wall can accumulate
changing illumination without being classified as displaced geometry.
[Falcor's SVGF reprojection](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/RenderPasses/SVGFPass/SVGFReproject.ps.slang)
validates primary-surface history before accumulating illumination.
[NRD's history-confidence guidance](https://github.com/NVIDIA-RTX/NRD#history-confidence)
separates lighting responsiveness from primary-surface disocclusion and requires
matched random samples when estimating lighting changes. A raw sample's RGB
difference is not sufficient evidence to discard its history.

The regression now translates the emitter on **every rendered frame**, including
asynchronous readback waits. Its small 1.5 cm / 1 cm oscillation preserves nearly
the same physical illumination while exercising actual donor transform changes.
At unchanged Medium 4-ray / 16-step settings:

| Moving-emitter receiver metric | Before correction | After correction |
| --- | ---: | ---: |
| Mean exposed linear luminance | .00655380 | .00654468 |
| Relative temporal RMS | 54.65% | 9.32% |
| Relative local coherent RMS | 29.33% | 5.36% |

The temporal RMS reduction is 83%, with a .14% mean difference. The ordinary
default-quality test enforces the same retained-energy and noise limits on this
moving-donor case as on its stationary receiver. This is a correctness change,
with no added rays, steps, passes or GPU allocations.

History still has finite response time: at Stability .90 an all-miss neighborhood
retains about 12% after 20 frames and 4% after 30 frames. The source-removal check
measures that decay rather than claiming instantaneous lighting changes. Donor
capture is from the previous frame and has no object-motion reprojection;
[Unity HDRP's SSGI](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/ScreenSpaceLighting/ScreenSpaceGlobalIllumination.compute)
uses hit-surface motion vectors for that lookup. Crimson conservatively rejects
unmatched captured geometry; fast motion, newly revealed sources and offscreen
transport remain limitations. This fix does not implement motion vectors or
adaptive variance-guided reconstruction.

An explicit `ssgi_scene_motion_capture` diagnostic also renders a disposable
snapshot of the authored project through the real scene extraction/resource path.
It selects the named `ShadowCamera` in memory and translates the existing emitter
on every rendered frame. It never changes the authored active camera or scene.
The test records Lit/Indirect views under both saved settings and global defaults,
covering static, moving, stopped and removed phases. The 960×540 before/after runs
have identical source hashes, camera, settings, readback frame numbers and emitter
trajectories, with no source-file changes. Matching saved-setting images reproduce
the original wall blotches and show the corrected, smoother diffuse bounce. The
source travels up to .35 m along camera right over a 120-rendered-frame period;
this larger-motion image comparison complements the small-motion noise oracle.
Frame differences also include real lighting change and are not a reference-error
metric. The saved scene used Custom 7 rays / 16 steps, 3 bounces, Strength 3.57,
Reach 1 m and Stability .76; those values were preserved for the saved A/B pair.

For the upper-wall ROI `(380,100)–(570,300)`, excluding the moving cube, temporal
RMS fell by 32.9% in saved Lit and 34.9% in saved Indirect. With defaults the
reductions were 43.7% / 46.4%. Static upper-wall measurements were unchanged.
These scene measurements include legitimate motion of the lighting gradient;
the small-motion oracle above isolates the erroneous history resets more directly.

A final closed-editor DX12 RTX 4090 comparison at 2560×1440 used 240 warmup
frames and 600 distinct whole-frame GPU samples per phase, with the emitter
moving continuously. Median GI-stage elapsed time was .4229 ms before / .4219 ms
after for Game, and .8479 / .8417 ms for Scene+Game. Matching-view samples numbered
579/600 and 572/547, respectively. Single-view GI p95 was .4280 / .4270 ms;
dual-view summed p95 was 1.1080 / .8704 ms. These differences are measurement
variation, not an optimization claim. Whole-frame medians were .7578 / .7823 ms
for Game and 1.4705 / 1.4408 ms for both views; the opposite directions likewise
do not establish a renderer-wide FPS improvement. GI owner memory remained
206,438,400 bytes per 1440p view. The correction adds no targets or dispatches.
Earlier repeated 1080p/1440p measurements with the editor open are retained too,
but their noisy timing tails are not isolated cost evidence.

Follow-up shader snapshots, PNGs, raw JSON measurements and the bounded
`Measure-Motion.ps1` comparison are under `build/ssgi-motion`. The timestamp
fixture accepts `--moving-donor` to keep the source transforming throughout both
GI-off and GI-on workloads.

## Controlled quality experiments

`ssgi_render_tests --ssgi-audit` renders a six-face emissive cube beside a diffuse
vertical wall. Direct lights, ambient light, fog, Glow, debanding and AA are absent.
It warms 96 uninterrupted frames, then captures 32 images per stationary,
camera-motion and odd-resize case. Readback continues rendering so histories stay
valid. A close-camera case exercises rays that cross the near plane or viewport.
Light removal is measured after 16/32/64 frames; zero intensity must yield black.

Luminance is reconstructed from the final RGBA8 image with inverse sRGB decoding,
fixed exposure and Linear tone mapping. It is exposed display-linear luminance,
not a raw HDR irradiance readback. The receiver patch avoids emission coverage,
silhouettes and saturation. Noise metrics subtract each pixel's time mean;
long-range covariance additionally removes whole-patch flicker and uses offsets
of 16/24 pixels, beyond the reconstruction footprint. Camera-motion metrics also
contain legitimate changes in the observed lighting gradient, so stationary
cases determine the banding acceptance bound.

| Stationary case | Old defaults: relative temporal RMS | New Medium defaults | Old long-range RMS | New long-range RMS |
| --- | ---: | ---: | ---: | ---: |
| 217 × 163 | 12.01% | 9.31% | 3.33% | 1.65% |
| 321 × 241 | 11.20% | 9.41% | 2.11% | 1.63% |

Mean receiver luminance changed from .006611 to .006650 and from .006907 to
.006871, respectively: within 1%. The improvement was not obtained by removing
most of the reflected light. The modest camera pan measured 13.45% versus 10.31%
total relative RMS. These are local fixture results, not guarantees for every
scene, source size, camera motion or occlusion.

The iteration rejected independent white scrambling as the default: at equal
four-ray settings it removed strong periodic peaks but increased filtered noise.
A Hilbert-mapped gradient-noise candidate also failed to provide a better result.
High quality reduced stationary RMS further to 5.72–5.89%, but cost about twice
as much GI time. Six rays/20 steps provided little quality gain over Medium here.
Stability .93 reduced noise while retaining substantially more stale illumination
after source removal; .90 is the chosen compromise.

The ordinary GPU tests enforce retained brightness, temporal-noise bounds,
stationary long-range covariance, source decay and exact zero-intensity bypass.
The preserved original shaders fail the new banding oracle at High quality;
the corrected shaders pass. Existing transport checks cover fallback/generated
materials, point/directional donors, shadows, scopes, alpha exclusions,
independent Scene/Game histories and one-to-four-bounce paths.

## GPU cost and default selection

The bounded performance fixture renders the same static wall/floor/emissive-cube
geometry with GI off/on in Game and simultaneous Scene+Game views. It uses 240
warmup frames and 600 distinct completed GPU timestamp pairs per case, repeated
twice at 1080p and 1440p. It records effective settings, raw frame IDs, medians,
p95, CPU submission time, matching-view effect timestamps and memory. Native
editor windows and other rendering test processes were closed during measurement.

Representative repeated DX12 medians on **NVIDIA GeForce RTX 4090**:

| Resolution / views | Old Medium GI passes | New Medium GI passes | New High GI passes |
| --- | ---: | ---: | ---: |
| 1920 × 1080 / Game | .239 ms | .251 ms | .49–.50 ms |
| 1920 × 1080 / Scene + Game | .50–.51 ms | .52 ms | .98–.99 ms |
| 2560 × 1440 / Game | .380 ms | .393 ms | .814 ms |
| 2560 × 1440 / Scene + Game | .774–.776 ms | .798–.799 ms | 1.64 ms |

New Medium single-view p95 was approximately .257–.258 ms at 1080p and
.397–.412 ms at 1440p. The 1440p dual-view p95 sum was about 1.02 ms versus
.79–.80 ms previously; report that tail rather than inferring all behavior from
the medians. Whole-frame GI-on-minus-off medians were approximately .22–.24 ms
at 1080p and .44 ms at 1440p for one view. DX11 1440p corroboration measured
whole-frame medians .5366 ms old versus .5356 ms new with GI enabled; the small
difference is not evidence of an optimization.

These are synthetic elapsed timestamp intervals. They are not active GPU
utilization or the editor's gameplay FPS. The fixture omits textured-material
complexity, animated geometry, shadows, asset traversal and editor UI; more
visible intermediate geometry can extend ray paths. The default-change cost is
about .01–.02 ms per view on this hardware, not a promise for other GPUs.

Optimized SM5 disassembly was also inspected. The blue-noise table is a 16 KiB
immediate constant buffer, not a per-thread local array. A zero-weight early-branch
experiment preserved all 131 captured images but did not improve these timings
materially, so it was not adopted. The more complex homogeneous-coordinate ALU
rewrite was deferred rather than introducing numerical risk without a measured
need.

The final default therefore **retains Medium's four rays and sixteen steps**:
Intensity 1, Reach 3 m, one bounce, Stability .90, radius four half-resolution
pixels, Smoothing 1, Thickness .1 m, Ray Bias .02 m and Radiance Clamp 10. High
remains an explicit quality/cost option. Enabled stays off by default.

The basic Inspector exposes Enabled, Intensity, Reach and Quality. Use Defaults
restores the complete recommended tuning in one undoable edit while preserving
Enabled. Advanced retains every authored technical control. Serialized names,
ranges and scene versions are unchanged; existing explicit tuning is preserved
on load and files are never rewritten just to adopt new defaults.

Local reproducibility artifacts are under `build/ssgi-audit`: old shader snapshots,
candidate sources, `Run-Audit.ps1`, `Run-Performance.ps1`, per-case PNG/JSON,
performance summaries, build/test logs and the expected old-shader regression.
The checked-in fixture accepts `--ssgi-shader-dir`, `--ssgi-audit`,
`--ssgi-benchmark-only`, `--benchmark-size`, `--warmup`, `--samples` and explicit
`--ssgi-*` tuning, so comparisons do not require rebuilding each candidate.
The preserved baseline comes from renderer commit `b57e078`; the delivered
initial calibrated renderer was `c37b62c`; moving-donor correction is `094376f`. Force `--ssgi-quality high --ssgi-radius 4 --ssgi-stability .9`
when reproducing the old-shader banding-oracle failure; that check compares the
algorithms at equal High settings rather than changing the new Medium default.
